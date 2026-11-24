#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include <unordered_set>
#include <algorithm>
#include <cstring>


struct alignas(16) InstanceInfo
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 invModel;

	glm::vec4 normalMatRow1;
	glm::vec4 normalMatRow2;
	glm::vec4 normalMatRow3;

	uint32_t materialIndex;
	uint32_t indexOffset;
	uint32_t vertexOffset;

	uint32_t skinned;				// 1 = 蒙皮网格：顶点要去本帧的蒙皮顶点缓冲里取（否则从静态顶点块里取）
	uint32_t skinnedVertexOffset;	// 该网格在本帧蒙皮顶点缓冲里的起始顶点

	alignas(16) glm::mat4 prevModel;	// 上一帧的模型矩阵：命中点上一帧位置（反射时域累积的运动重投影）
};

static_assert(sizeof(InstanceInfo) == 272, "InstanceInfo 布局要和 ClosestHit.rchit / DDGIClosestHit.rchit 里的同名结构保持一致");

// 关掉后：顶点 / 索引块换地址时不再重建全部静态 BLAS（见 UpdateBlasData）。出问题时改成 true 对比原来的行为。
static constexpr bool kRebuildBlasWhenGeometryBufferMoves = false;

static uint64_t AlignUp(uint64_t value, uint64_t alignment)
{
	return (value + alignment - 1) / alignment * alignment;
}

// 每段 scratch 的起始地址对齐（规范要求按 minAccelerationStructureScratchOffsetAlignment，原来写死 256）
static uint64_t AccelerationStructureScratchAlign(const std::shared_ptr<VKCore::VulkanDevice>& device)
{
	uint64_t align = 256;
	if (device)
		align = std::max<uint64_t>(align, device->GetVulkanPhysicalDeviceInfo().accelProps.minAccelerationStructureScratchOffsetAlignment);
	return align;
}

// 实例信息要逐字节和上一帧比较：先整块清零再填字段，结构体末尾的填充字节才是确定的
static InstanceInfo& EmplaceZeroed(std::vector<InstanceInfo>& infos)
{
	InstanceInfo& info = infos.emplace_back();
	std::memset(&info, 0, sizeof(InstanceInfo));
	return info;
}

static bool SameBytes(const std::vector<uint8_t>& last, const void* data, size_t size)
{
	return last.size() == size && (size == 0 || std::memcmp(last.data(), data, size) == 0);
}

// 蒙皮 BLAS 的键：姿势身份（SkinnedPoseHistory 指针）+ 网格 UUID。
// 同一个实体的不同网格、不同实体的网格都会落到不同的键上；
// 指针地址被复用的情况（旧实体销毁、新实体的 history 正好在同一个地址）
// 在 UpdateSkinnedBlas 里用 weak_ptr 校验，对不上就销毁重建。
static std::string MakeSkinnedBlasKey(const VKRenderContext::SkinnedPoseHistory* pose, const std::string& meshUUID)
{
	return std::to_string(reinterpret_cast<std::uintptr_t>(pose)) + '|' + meshUUID;
}

static vk::TransformMatrixKHR toTransformMatrixKHR(const glm::mat4& matrix)
{
	vk::TransformMatrixKHR result;

	// glm 是列主序：matrix[col][row]
	// VkTransformMatrixKHR 是行主序：result.matrix[row][col]
	// 所以直接转置读取即可

	// 第 0 行
	result.matrix[0][0] = matrix[0][0];
	result.matrix[0][1] = matrix[1][0];
	result.matrix[0][2] = matrix[2][0];
	result.matrix[0][3] = matrix[3][0];

	// 第 1 行
	result.matrix[1][0] = matrix[0][1];
	result.matrix[1][1] = matrix[1][1];
	result.matrix[1][2] = matrix[2][1];
	result.matrix[1][3] = matrix[3][1];

	// 第 2 行
	result.matrix[2][0] = matrix[0][2];
	result.matrix[2][1] = matrix[1][2];
	result.matrix[2][2] = matrix[2][2];
	result.matrix[2][3] = matrix[3][2];

	return result;
}

BlasHandleManager::BlasHandleManager()
	:_blasBufferManager(1024 * 1024 * 20)
	, _skinnedBlasBufferManager(1024 * 1024 * 8)
{
	_blasBufferManager.SetAlign(256);
	_skinnedBlasBufferManager.SetAlign(256);
}

void BlasHandleManager::RemoveBlasData(std::shared_ptr<StaticMesh>& mesh, std::shared_ptr<VKCore::VulkanDevice>& device)
{
	auto it = _blasHandles.find(mesh.get());
	if (it == _blasHandles.end())
		return;

	auto& data = it->second;
	if (data.handle != VK_NULL_HANDLE)
		device->GetHandle().destroyAccelerationStructureKHR(data.handle);
	_blasBufferManager.RemoveSegment(data.meshUUID);
	_blasHandles.erase(it);
}

bool BlasHandleManager::FindBlasData(std::shared_ptr<StaticMesh>& mesh, BlasHandleData& data) {
	auto it = _blasHandles.find(mesh.get());
	if (it == _blasHandles.end())
		return false;
	data = it->second;
	return true;
}

uint32_t BlasHandleManager::UpdateBlasData(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	std::vector<std::shared_ptr<StaticMesh>>& meshs,
	std::shared_ptr<VertexBufferBlock>& vertexBlock,
	std::shared_ptr<IndexBufferBlock>& indexBlock,
	std::shared_ptr<VKCore::VulkanDevice>& device,
	std::shared_ptr<StorageBlock>& scratchBlock
)
{
	if (!cmd || !device || !vertexBlock || !indexBlock || !scratchBlock)
		return 0;

	vk::DeviceAddress vertexBufferAddress = vertexBlock->GetDeviceAddress();
	vk::DeviceAddress indexBufferAddress = indexBlock->GetDeviceAddress();

	struct BlasContext {
		std::shared_ptr<StaticMesh> mesh;
		vk::AccelerationStructureGeometryTrianglesDataKHR trianglesData;
		vk::AccelerationStructureGeometryDataKHR geometryData;
		vk::AccelerationStructureGeometryKHR blasGeometry;
		vk::AccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo;
		vk::AccelerationStructureBuildSizesInfoKHR blasBuildSizes;
		vk::AccelerationStructureBuildRangeInfoKHR buildRange;
		BlasHandleData blasData;
		uint32_t blasFirst;
		uint32_t vertexOffset;
		uint32_t indicesOffset;
		uint64_t scratchOffset = 0;
		BlasContext(std::shared_ptr<StaticMesh>& m) :mesh(m) {
			blasData.meshWeakPtr = m;
			blasData.meshVersion = m->GetVerticesIndicesVsrsion();
			blasData.meshUUID = m->GetUUID();
		}
	};

	std::unordered_map<StaticMesh*, std::shared_ptr<BlasContext>> ctxs;
	std::vector<std::shared_ptr<BlasContext>> order;		// 按加入顺序构建，结果可复现

	auto indirectManager = IndirectDrawManager::Instance();

	auto addToCreate = [&](std::vector<std::shared_ptr<StaticMesh>>& toCreateMeshs) -> void
		{

			std::vector<IndirectDrawMeta> indirectDrawMetas;
			indirectDrawMetas.resize(toCreateMeshs.size());
			indirectManager->WithSharedLock(
				{ IndirectDrawManager::SharedLockType::Mesh },
				[&]() {
					for (size_t i = 0; i < toCreateMeshs.size(); i++)
					{
						auto& mesh = toCreateMeshs[i];
						auto& meta = indirectDrawMetas[i];

						indirectDrawMetas[i].vertexOffset = -1;
						indirectManager->GetIndirectDrawMeta_LockFree(*mesh, meta);
					}
				});

			for (size_t i = 0; i < toCreateMeshs.size(); i++)
			{
				auto& mesh = toCreateMeshs[i];
				if (ctxs.find(mesh.get()) != ctxs.end())
					continue;

				auto& meta = indirectDrawMetas[i];
				if (meta.vertexOffset < 0)
					continue;

				// 网格版本变了要重建：旧句柄先销毁（原来直接被新句柄覆盖，旧的一直泄漏）
				if (auto old = _blasHandles.find(mesh.get()); old != _blasHandles.end() && old->second.handle != VK_NULL_HANDLE)
				{
					device->GetHandle().destroyAccelerationStructureKHR(old->second.handle);
					old->second.handle = VK_NULL_HANDLE;
				}

				auto& vertices = mesh->GetVertices();
				auto& indices = mesh->GetIndices();

				auto ctx = std::make_shared<BlasContext>(mesh);
				ctx->indicesOffset = meta.firstIndex;
				ctx->vertexOffset = meta.vertexOffset;

				ctx->trianglesData = vk::AccelerationStructureGeometryTrianglesDataKHR(
					vk::Format::eR32G32B32Sfloat,
					vertexBufferAddress,
					sizeof(Vertex),
					vertices.size() - 1,
					vk::IndexType::eUint32,
					indexBufferAddress
				);

				ctx->geometryData = vk::AccelerationStructureGeometryDataKHR(ctx->trianglesData);

				ctx->blasGeometry = vk::AccelerationStructureGeometryKHR(
					vk::GeometryTypeKHR::eTriangles,
					ctx->geometryData,
					vk::GeometryFlagBitsKHR::eOpaque
				);

				// 静态 BLAS 建一次、追很多次：让驱动多花点构建时间换更快的求交
				// （flag 会影响所需大小，必须在查询大小之前设置）
				ctx->blasBuildGeometryInfo
					.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
					.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
					.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
					.setGeometries(ctx->blasGeometry);

				ctx->blasBuildSizes = device->GetHandle().getAccelerationStructureBuildSizesKHR(
					vk::AccelerationStructureBuildTypeKHR::eDevice,
					ctx->blasBuildGeometryInfo,
					indices.size() / 3
				);

				auto blasSegData = _blasBufferManager.SetSegment(mesh->GetUUID(), (void*)mesh->GetVerticesIndicesVsrsion(), nullptr, ctx->blasBuildSizes.accelerationStructureSize);
				ctx->blasFirst = blasSegData.first;

				ctxs[mesh.get()] = ctx;
				order.push_back(std::move(ctx));
			}
		};

	addToCreate(meshs);

	// 什么时候已有的 BLAS 要全部重建：
	//   * BLAS 存储块换了 VkBuffer（上面 SetSegment 扩容时会新建 buffer 再拷贝）——旧句柄是建在旧 buffer 上的，
	//     而 TLAS 实例按「新 buffer 地址 + 偏移」引用，那里只是普通拷贝过来的字节，规范不保证可用。这是真正必须重建的情况。
	//   * 顶点 / 索引块地址变化：BLAS 构建时会把三角形读进自己的存储，之后不再访问顶点 / 索引缓冲
	//     （命中着色器取顶点用的是 InstanceInfo 里每帧重新取的偏移），所以默认不再因此重建。
	//     加载新网格时顶点块和 BLAS 存储块往往一起扩容，原来按顶点地址判断正好顺带覆盖了上一种情况。
	//     如果关掉后出现问题，把 kRebuildBlasWhenGeometryBufferMoves 改成 true 恢复原来的行为对比。
	bool rebuildAll = false;
	if constexpr (kRebuildBlasWhenGeometryBufferMoves)
	{
		if (_lastVertexAddress != vertexBufferAddress || _lastIndexAddress != indexBufferAddress)
			rebuildAll = _lastVertexAddress != 0 || _lastIndexAddress != 0;
		_lastVertexAddress = vertexBufferAddress;
		_lastIndexAddress = indexBufferAddress;
	}
	const vk::Buffer storageBuffer = _blasBufferManager.GetBuffer()->GetBlock()->GetHandle();
	if (_lastBlasStorageBuffer != VK_NULL_HANDLE && storageBuffer != _lastBlasStorageBuffer)
		rebuildAll = true;
	_lastBlasStorageBuffer = storageBuffer;

	if (rebuildAll)
	{
		// 段的布局不变（扩容只是换 buffer 再整体拷贝），所以原地按原偏移重建即可，不用重新分配
		std::vector<std::shared_ptr<StaticMesh>> rebuildMeshs;
		for (auto it = _blasHandles.begin(); it != _blasHandles.end();)
		{
			auto& data = it->second;
			if (data.handle != VK_NULL_HANDLE)
				device->GetHandle().destroyAccelerationStructureKHR(data.handle);
			data.handle = VK_NULL_HANDLE;

			auto mesh = data.meshWeakPtr.lock();
			if (mesh && ctxs.find(mesh.get()) != ctxs.end())
			{
				++it;									// 本帧本来就要建
				continue;
			}
			if (!mesh || mesh->GetVerticesIndicesVsrsion() != data.meshVersion)
			{
				// 网格已销毁或版本变了（段大小可能不对）：丢掉，回到范围内时按新版本重建
				_blasBufferManager.RemoveSegment(data.meshUUID);
				it = _blasHandles.erase(it);
				continue;
			}
			rebuildMeshs.push_back(std::move(mesh));
			++it;
		}
		addToCreate(rebuildMeshs);					// 同 id 同大小：SetSegment 直接返回原来的段，不会再扩容
		_lastBlasStorageBuffer = _blasBufferManager.GetBuffer()->GetBlock()->GetHandle();
	}

	if (order.empty())
		return 0;

	// 所有 BLAS 共用一块 scratch，各占一段；全部录进同一个命令缓冲，由调用方统一提交
	const uint64_t scratchAlign = AccelerationStructureScratchAlign(device);
	uint64_t scratchTotal = 0;
	for (auto& ctx : order)
	{
		ctx->scratchOffset = AlignUp(scratchTotal, scratchAlign);
		scratchTotal = ctx->scratchOffset + ctx->blasBuildSizes.buildScratchSize;
	}
	if (scratchBlock->GetSize() < scratchTotal)
		scratchBlock->SetSize(scratchTotal, false);			// scratch 不需要保留旧内容
	const vk::DeviceAddress scratchBase = scratchBlock->GetDeviceAddress();

	auto blasBlock = _blasBufferManager.GetBuffer()->GetBlock();
	auto blasBlockHandle = blasBlock->GetHandle();

	std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> buildInfos;
	std::vector<const vk::AccelerationStructureBuildRangeInfoKHR*> buildRanges;
	buildInfos.reserve(order.size());
	buildRanges.reserve(order.size());

	for (auto& ctx : order)
	{
		auto& indices = ctx->mesh->GetIndices();

		vk::AccelerationStructureCreateInfoKHR blasCreateInfo;
		blasCreateInfo
			.setBuffer(blasBlockHandle)
			.setOffset(ctx->blasFirst)
			.setSize(ctx->blasBuildSizes.accelerationStructureSize)
			.setType(vk::AccelerationStructureTypeKHR::eBottomLevel);

		auto [blasHandleResult, blasHandle] = device->GetHandle().createAccelerationStructureKHR(blasCreateInfo);
		if (blasHandleResult != vk::Result::eSuccess)
			continue;

		// 存回构建信息
		ctx->blasBuildGeometryInfo.dstAccelerationStructure = blasHandle;
		ctx->blasBuildGeometryInfo.scratchData.deviceAddress = scratchBase + ctx->scratchOffset;

		ctx->buildRange.primitiveCount = indices.size() / 3;									// 三角形数量
		ctx->buildRange.primitiveOffset = ctx->indicesOffset * sizeof(unsigned int);			// 索引缓冲区字节偏移
		ctx->buildRange.firstVertex = ctx->vertexOffset;										// 顶点缓冲区顶点索引偏移
		ctx->buildRange.transformOffset = 0;

		buildInfos.push_back(ctx->blasBuildGeometryInfo);
		buildRanges.push_back(&ctx->buildRange);

		ctx->blasData.handle = blasHandle;
		ctx->blasData.offset = ctx->blasFirst;

		_blasHandles[ctx->mesh.get()] = ctx->blasData;
	}

	// 一条命令建完全部（录制时就会读走这些结构体，ctx 只需活到这里）
	if (!buildInfos.empty())
		cmd->buildAccelerationStructuresKHR(buildInfos, buildRanges);

	return uint32_t(buildInfos.size());
}

// 蒙皮网格的 BLAS：第一次 eBuild，之后每帧 eUpdate（refit）。
// 顶点每帧都变，但拓扑不变，所以不用重建句柄，只需要把新的顶点地址喂进去。
bool BlasHandleManager::UpdateSkinnedBlas(
	const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	std::vector<SkinnedBlasRequest>& requests,
	std::vector<vk::DeviceAddress>& outAddresses,
	std::shared_ptr<IndexBufferBlock>& indexBlock,
	std::shared_ptr<VKCore::VulkanDevice>& device,
	std::shared_ptr<StorageBlock>& scratchBlock
)
{
	outAddresses.assign(requests.size(), 0);

	if (!cmd || requests.empty() || !device || !indexBlock || !scratchBlock)
		return false;

	++_skinnedFrameIndex;

	const vk::DeviceAddress indexBufferAddress = indexBlock->GetDeviceAddress();

	// 每个 BLAS 一次构建/更新，几何体和构建信息必须一直活着到 build 调用
	struct BuildUnit
	{
		vk::AccelerationStructureGeometryTrianglesDataKHR trianglesData;
		vk::AccelerationStructureGeometryKHR geometry;
		vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
		vk::AccelerationStructureBuildRangeInfoKHR buildRange;
		vk::AccelerationStructureBuildSizesInfoKHR buildSizes;

		SkinnedBlasData* data = nullptr;
		size_t requestIndex = 0;
		size_t segmentOffset = 0;
		uint64_t scratchOffset = 0;
		bool create = false;
	};

	std::vector<std::unique_ptr<BuildUnit>> units;
	units.reserve(requests.size());

	// 先把需要的 BLAS 段分配好（SetSegment 可能让缓冲区扩容，所以必须在取 block 句柄之前全部做完）
	for (size_t i = 0; i < requests.size(); i++)
	{
		auto& request = requests[i];
		if (!request.mesh || !request.poseHistory || !request.vertexBuffer)
			continue;
		if (request.vertexCount == 0 || request.indexCount < 3)
			continue;

		const std::string key = MakeSkinnedBlasKey(request.poseHistory.get(), request.mesh->GetUUID());
		const uint32_t primitiveCount = request.indexCount / 3;

		auto it = _skinnedBlasHandles.find(key);
		if (it != _skinnedBlasHandles.end() && it->second.poseWeakPtr.lock() != request.poseHistory)
		{
			// 地址被复用，但对象已经不是同一个了 —— 旧 BLAS 不能再用
			if (it->second.handle != VK_NULL_HANDLE)
				device->GetHandle().destroyAccelerationStructureKHR(it->second.handle);
			_skinnedBlasBufferManager.RemoveSegment(it->second.key);
			_skinnedBlasHandles.erase(it);
			it = _skinnedBlasHandles.end();
		}

		bool create = false;
		if (it == _skinnedBlasHandles.end())
		{
			SkinnedBlasData data;
			data.key = key;
			data.meshWeakPtr = request.mesh;
			data.poseWeakPtr = request.poseHistory;
			data.primitiveCount = primitiveCount;
			it = _skinnedBlasHandles.emplace(key, std::move(data)).first;
			create = true;
		}
		else if (it->second.primitiveCount != primitiveCount)
		{
			// 拓扑变了（换网格、换 LOD），refit 不合法，只能重建
			if (it->second.handle != VK_NULL_HANDLE)
				device->GetHandle().destroyAccelerationStructureKHR(it->second.handle);
			it->second.handle = VK_NULL_HANDLE;
			it->second.address = 0;
			it->second.primitiveCount = primitiveCount;
			create = true;
		}

		auto& data = it->second;
		data.lastUsedFrame = _skinnedFrameIndex;

		auto unit = std::make_unique<BuildUnit>();
		unit->data = &data;
		unit->requestIndex = i;
		unit->create = create;

		const vk::DeviceAddress vertexAddress =
			request.vertexBuffer->GetDeviceAddress() + static_cast<vk::DeviceSize>(request.vertexStart) * sizeof(Vertex);

		unit->trianglesData = vk::AccelerationStructureGeometryTrianglesDataKHR(
			vk::Format::eR32G32B32Sfloat,
			vertexAddress,
			sizeof(Vertex),
			request.vertexCount - 1,
			vk::IndexType::eUint32,
			indexBufferAddress
		);

		unit->geometry = vk::AccelerationStructureGeometryKHR(
			vk::GeometryTypeKHR::eTriangles,
			vk::AccelerationStructureGeometryDataKHR(unit->trianglesData),
			vk::GeometryFlagBitsKHR::eOpaque
		);

		unit->buildInfo
			.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
			.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate)		// 允许 refit
			.setGeometries(unit->geometry);

		if (create)
		{
			unit->buildInfo.setMode(vk::BuildAccelerationStructureModeKHR::eBuild);
			unit->buildSizes = device->GetHandle().getAccelerationStructureBuildSizesKHR(
				vk::AccelerationStructureBuildTypeKHR::eDevice,
				unit->buildInfo,
				primitiveCount
			);

			auto segData = _skinnedBlasBufferManager.SetSegment(key, (void*)&data, nullptr, unit->buildSizes.accelerationStructureSize);
			unit->segmentOffset = segData.first;
			data.scratchSize = unit->buildSizes.buildScratchSize;
		}
		else
		{
			// 上一次创建失败（或者句柄已失效），没有可 refit 的对象，这一帧先跳过
			if (data.handle == VK_NULL_HANDLE)
				continue;

			unit->buildInfo.setMode(vk::BuildAccelerationStructureModeKHR::eUpdate);
			unit->buildInfo.setSrcAccelerationStructure(data.handle);
			unit->buildInfo.setDstAccelerationStructure(data.handle);
		}

		unit->buildRange.primitiveCount = primitiveCount;
		unit->buildRange.primitiveOffset = request.firstIndex * sizeof(uint32_t);		// 索引字节偏移
		unit->buildRange.firstVertex = 0;												// 顶点地址已经带上了 vertexStart
		unit->buildRange.transformOffset = 0;

		units.emplace_back(std::move(unit));
	}

	if (units.empty())
		return false;

	// 所有 BLAS 共用一块 scratch，各自占一段；只录制，由调用方和 TLAS 一起提交
	const uint64_t scratchAlign = AccelerationStructureScratchAlign(device);
	uint64_t totalScratch = 0;
	for (auto& unitPtr : units)
	{
		auto& unit = *unitPtr;
		const uint32_t need = unit.create ? unit.buildSizes.buildScratchSize : unit.data->scratchSize;
		totalScratch = AlignUp(totalScratch, scratchAlign);
		unit.scratchOffset = totalScratch;
		totalScratch += need;
	}

	if (scratchBlock->GetSize() < totalScratch)
		scratchBlock->SetSize(totalScratch, false);			// scratch 不需要保留旧内容

	const vk::DeviceAddress scratchAddress = scratchBlock->GetDeviceAddress();
	auto blasBlockHandle = _skinnedBlasBufferManager.GetBuffer()->GetBlock()->GetHandle();

	std::vector<std::shared_ptr<VertexBufferBlock>> barrieredBuffers;
	{
		std::unordered_set<VertexBufferBlock*> barriered;
		for (auto& request : requests)
		{
			if (!request.vertexBuffer)
				continue;
			if (!barriered.insert(request.vertexBuffer.get()).second)
				continue;
			request.vertexBuffer->Barrier(cmd, BufferUsage::VertexAttributeRead, BufferUsage::AccelerationStructureRead);
			barrieredBuffers.push_back(request.vertexBuffer);
		}
	}

	for (auto& unitPtr : units)
	{
		auto& unit = *unitPtr;
		auto& data = *unit.data;

		if (unit.create)
		{
			vk::AccelerationStructureCreateInfoKHR createInfo;
			createInfo
				.setBuffer(blasBlockHandle)
				.setOffset(unit.segmentOffset)
				.setSize(unit.buildSizes.accelerationStructureSize)
				.setType(vk::AccelerationStructureTypeKHR::eBottomLevel);

			auto [result, handle] = device->GetHandle().createAccelerationStructureKHR(createInfo);
			if (result != vk::Result::eSuccess)
				continue;

			data.handle = handle;
			data.address = device->GetHandle().getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR().setAccelerationStructure(handle));
			unit.buildInfo.setDstAccelerationStructure(handle);
		}

		unit.buildInfo.scratchData.deviceAddress = scratchAddress + unit.scratchOffset;

		cmd->buildAccelerationStructuresKHR(unit.buildInfo, &unit.buildRange);

		outAddresses[unit.requestIndex] = data.address;
	}

	// 已经录了顶点屏障（以及可能的构建），调用方必须提交，TLAS 也要随之重建
	return true;
}

// 回收长期没用到的蒙皮 BLAS：实体已经销毁（weak_ptr 过期），或者很久没有出现在渲染列表里
void BlasHandleManager::PruneSkinnedBlas(std::shared_ptr<VKCore::VulkanDevice>& device, uint64_t maxUnusedFrames)
{
	for (auto it = _skinnedBlasHandles.begin(); it != _skinnedBlasHandles.end(); )
	{
		auto& data = it->second;

		const bool expired = data.poseWeakPtr.expired() || data.meshWeakPtr.expired();
		const bool stale = _skinnedFrameIndex > data.lastUsedFrame + maxUnusedFrames;
		if (!expired && !stale)
		{
			++it;
			continue;
		}

		if (data.handle != VK_NULL_HANDLE)
			device->GetHandle().destroyAccelerationStructureKHR(data.handle);
		_skinnedBlasBufferManager.RemoveSegment(data.key);

		it = _skinnedBlasHandles.erase(it);
	}
}

RTCoreRayTraceGeneralBuffer::RTCoreRayTraceGeneralBuffer()
{
	_tlasInstancesBlock = std::make_shared<StorageBlock>();
	_tlasBlock = std::make_shared<StorageBlock>();
	_tlasInstancesInfosBlock = std::make_shared<StorageBlock>();
}

vk::AccelerationStructureKHR RTCoreRayTraceGeneralBuffer::GetAccelerationStructure() const { return _tlasAccelerationStructure; }

std::shared_ptr<StorageBlock> RTCoreRayTraceGeneralBuffer::GetInstancesInfosBlock() const { return _tlasInstancesInfosBlock; }

std::shared_ptr<VertexBufferBlock> RTCoreRayTraceGeneralBuffer::GetSkinnedVertexBlock() const { return _skinnedVertexBlock; }

RTCoreRayTraceGeneralPass::RTCoreRayTraceGeneralPass()
{
	_buffers = std::make_shared<RTCoreRayTraceGeneralBuffer>();
	_scratchBlock = std::make_shared<StorageBlock>();
	_blasScratchBlock = std::make_shared<StorageBlock>();
	_skinnedScratchBlock = std::make_shared<StorageBlock>();
}

RTCoreRayTraceGeneralPass::~RTCoreRayTraceGeneralPass()
{}

bool RTCoreRayTraceGeneralPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.rayTraceGIOn || state.option.flags.rayTraceReflectOn || state.option.flags.ddgiOn;
}

void RTCoreRayTraceGeneralPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{}

void RTCoreRayTraceGeneralPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{

	if (!ShouldExecute(registry, state))
		return;

	SetupGeneralBuffer(state);
}

std::shared_ptr<RTCoreRayTraceGeneralBuffer> RTCoreRayTraceGeneralPass::GetGeneralBuffer() {
	return _buffers;
}

bool RTCoreRayTraceGeneralPass::SetupGeneralBuffer(RenderState& state)
{
	float maxDistanceSqrt = state.option.rayTraceGeneralParams.maxDistance;
	float maxCacheClearDistanceSqrt = state.option.rayTraceGeneralParams.maxCacheClearDistance;
	maxDistanceSqrt *= maxDistanceSqrt;
	maxCacheClearDistanceSqrt *= maxCacheClearDistanceSqrt;

	auto indirectManager = IndirectDrawManager::Instance();

	auto vertexBlock = indirectManager->GetVertexBlock();
	auto indexBlock = indirectManager->GetIndexBlock();

	auto& tlasBlock = _buffers->_tlasBlock;
	auto& tlasInstancesBlock = _buffers->_tlasInstancesBlock;
	auto& tlasInstancesInfosBlock = _buffers->_tlasInstancesInfosBlock;

	auto device = VKCONTEXT->GetDevice();

	auto& opaqueItems = state.objects.sceneStaticRenderData.opaqueItems;
	auto& skinnedObjects = state.objects.sceneSkinnedRenderData;

	// 一次 reserve 到位：实例信息要逐字节和上一帧比较，中途扩容时元素的拷贝不保证带上填充字节
	const size_t maxInstanceCount = opaqueItems.size() + skinnedObjects.opaqueOneSideItems.size() + skinnedObjects.opaqueTwoSideItems.size();
	std::vector<vk::AccelerationStructureInstanceKHR> tlasInstances;
	std::vector<InstanceInfo> tlasInstanceInfos;
	tlasInstances.reserve(maxInstanceCount);
	tlasInstanceInfos.reserve(maxInstanceCount);

	uint32_t infosIndex = 0;

	std::vector<std::shared_ptr<StaticMesh>> needUpdateBlasMeshs;
	auto getItem = [&](VKRenderObjectData::SceneStaticRenderData::MeshItem& item) -> bool {
		auto& meshInfo = item.meshinfo;
		if (!meshInfo.mesh || !meshInfo.material)
			return false;

		auto& mesh = meshInfo.mesh;
		AABB aabb = mesh->GetAABB();
		aabb.MakeTransform(item.transform);
		float dis_sqrt = aabb.DistancePointToAABBSqrt(state.camera.position);
		if (dis_sqrt > maxCacheClearDistanceSqrt)
		{
			_buffers->_blasManager.RemoveBlasData(mesh, device);
			return false;
		}

		if (dis_sqrt > maxDistanceSqrt)
			return false;

		BlasHandleManager::BlasHandleData data;
		if (!_buffers->_blasManager.FindBlasData(mesh, data)
			|| data.meshVersion != mesh->GetVerticesIndicesVsrsion()
			|| data.handle == VK_NULL_HANDLE)
			needUpdateBlasMeshs.emplace_back(mesh);

		return true;
		};

	std::vector<VKRenderObjectData::SceneStaticRenderData::MeshItem*> matches;
	for (auto& item : opaqueItems)
	{
		if (getItem(item))
			matches.push_back(&item);
	}

	// 这一帧所有 GPU 工作（静态 BLAS 构建、蒙皮 BLAS refit、实例上传、TLAS 构建）都录进这一个命令缓冲，
	// 最后只提交、等待一次；什么都不用做时它不会开始录制，直接丢掉
	auto cmd = VKCONTEXT->GetCommandBuffer();

	const uint32_t staticBlasBuilt = _buffers->_blasManager.UpdateBlasData(cmd, needUpdateBlasMeshs, vertexBlock, indexBlock, device, _blasScratchBlock);

	auto blasBlock = _buffers->_blasManager._blasBufferManager.GetBuffer()->GetBlock();
	auto blasBlockAddress = blasBlock->GetDeviceAddress();

	std::vector<IndirectDrawMeta> indirectDrawMetas;
	std::vector<uint32_t> materialIndexs;
	indirectDrawMetas.resize(matches.size());
	materialIndexs.resize(matches.size(), std::numeric_limits<uint32_t>::max());

	using SharedLockType = IndirectDrawManager::SharedLockType;

	indirectManager->WithSharedLock(
		{ SharedLockType::Mesh, SharedLockType::Material },
		[&]()
		{
			std::for_each(std::execution::par_unseq, matches.begin(), matches.end(),
				[&](auto& itemptr) {
					size_t index = &itemptr - matches.data();

					auto& item = *itemptr;
					auto& meshInfo = item.meshinfo;
					auto& mesh = meshInfo.mesh;
					auto& material = meshInfo.material;

					auto& meta = indirectDrawMetas[index];
					uint32_t& materialIndex = materialIndexs[index];

					indirectDrawMetas[index].vertexOffset = -1;
					indirectManager->GetIndirectDrawMeta_LockFree(*mesh, meta);
					indirectManager->GetMaterialIndex_LockFree(*material, materialIndex);
				});
		});

	for (auto& itemptr : matches)
	{
		size_t index = &itemptr - matches.data();

		auto& item = *itemptr;
		auto& meshInfo = item.meshinfo;
		auto& mesh = meshInfo.mesh;

		auto& meta = indirectDrawMetas[index];
		uint32_t& materialIndex = materialIndexs[index];
		if (meta.vertexOffset < 0 || materialIndex == std::numeric_limits<uint32_t>::max())
			continue;

		BlasHandleManager::BlasHandleData data;
		if (!_buffers->_blasManager.FindBlasData(mesh, data)
			|| data.handle == VK_NULL_HANDLE)
			continue;

		InstanceInfo& info = EmplaceZeroed(tlasInstanceInfos);
		info.model = item.transform;
		info.invModel = glm::inverse(item.transform);
		glm::mat4 mat = transpose(info.invModel);
		info.normalMatRow1 = mat[0];
		info.normalMatRow2 = mat[1];
		info.normalMatRow3 = mat[2];
		info.materialIndex = materialIndex;
		info.indexOffset = meta.firstIndex;
		info.vertexOffset = meta.vertexOffset;
		info.skinned = 0;
		info.skinnedVertexOffset = 0;
		info.prevModel = item.prevTransform;

		vk::AccelerationStructureInstanceKHR tlasInstance;
		tlasInstance
			.setTransform(toTransformMatrixKHR(item.transform))
			.setInstanceCustomIndex(infosIndex++)											// 设置自定义索引 (用于在着色器中查找实例信息)
			.setAccelerationStructureReference(blasBlockAddress + data.offset)			// 设置 BLAS 设备地址
			.setMask(0xFF)
			.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleCullDisable)
			.setInstanceShaderBindingTableRecordOffset(0);

		tlasInstances.push_back(tlasInstance);
	}

	// ---------------- 蒙皮网格 ----------------
	// 蒙皮顶点是 SkinnedPass 算出来的，算完记在 curFrameSkinnedData 里：
	// pose(实体身份) -> 每个 mesh 在这份共享顶点缓冲里的 (vertexStart, vertexCount)。
	// 这里按同一份记账把 BLAS refit 一遍（拓扑不变，不重建句柄），再把实例挂进 TLAS。
	using SkinnedItem = VKRenderObjectData::SceneSkinnedRenderData::SkinnedMeshItem;

	std::vector<BlasHandleManager::SkinnedBlasRequest> skinnedRequests;
	std::vector<SkinnedItem*> skinnedMatches;
	std::vector<IndirectDrawMeta> skinnedIndirectDrawMetas;
	std::vector<uint32_t> skinnedMaterialIndexs;

	auto* frameSkinnedData = state.curFrameSkinnedData.get();

	// 命中着色器要用本帧的蒙皮顶点取顶点，先记下来给 BindAccelerationStructure 绑 binding 4
	_buffers->_skinnedVertexBlock = (frameSkinnedData && frameSkinnedData->skinnedMesh_VertexBuffer)
		? frameSkinnedData->skinnedMesh_VertexBuffer->buffer
		: nullptr;

	if (frameSkinnedData && _buffers->_skinnedVertexBlock)
	{
		auto collectSkinnedItem = [&](SkinnedItem& item) -> void
			{
				auto& meshInfo = item.meshinfo;
				if (!meshInfo.mesh || !meshInfo.material || !item.poseHistory)
					return;

				auto poseIt = frameSkinnedData->poses.find(item.poseHistory.get());
				if (poseIt == frameSkinnedData->poses.end())
					return;

				auto meshIt = poseIt->second.meshes.find(meshInfo.mesh->GetUUID());
				if (meshIt == poseIt->second.meshes.end())
					return;

				if (meshIt->second.vertexCount == 0)
					return;

				BlasHandleManager::SkinnedBlasRequest request;
				request.mesh = meshInfo.mesh;
				request.poseHistory = item.poseHistory;
				request.vertexBuffer = _buffers->_skinnedVertexBlock;
				request.vertexStart = meshIt->second.vertexStart;
				request.vertexCount = meshIt->second.vertexCount;

				skinnedRequests.emplace_back(std::move(request));
				skinnedMatches.push_back(&item);
			};

		for (auto& item : skinnedObjects.opaqueOneSideItems)
			collectSkinnedItem(item);
		for (auto& item : skinnedObjects.opaqueTwoSideItems)
			collectSkinnedItem(item);
	}

	bool skinnedBlasRecorded = false;
	if (!skinnedRequests.empty())
	{
		skinnedIndirectDrawMetas.resize(skinnedRequests.size());
		skinnedMaterialIndexs.resize(skinnedRequests.size(), std::numeric_limits<uint32_t>::max());

		indirectManager->WithSharedLock(
			{ SharedLockType::Mesh, SharedLockType::Material },
			[&]()
			{
				for (size_t i = 0; i < skinnedRequests.size(); i++)
				{
					auto& item = *skinnedMatches[i];

					skinnedIndirectDrawMetas[i].vertexOffset = -1;
					indirectManager->GetIndirectDrawMeta_LockFree(*item.meshinfo.mesh, skinnedIndirectDrawMetas[i]);
					indirectManager->GetMaterialIndex_LockFree(*item.meshinfo.material, skinnedMaterialIndexs[i]);
				}
			});

		for (size_t i = 0; i < skinnedRequests.size(); i++)
		{
			skinnedRequests[i].firstIndex = skinnedIndirectDrawMetas[i].firstIndex;
			skinnedRequests[i].indexCount = skinnedIndirectDrawMetas[i].indexCount;
		}

		std::vector<vk::DeviceAddress> skinnedBlasAddresses;
		skinnedBlasRecorded = _buffers->_blasManager.UpdateSkinnedBlas(cmd, skinnedRequests, skinnedBlasAddresses, indexBlock, device, _skinnedScratchBlock);
		_buffers->_blasManager.PruneSkinnedBlas(device, 240);

		for (size_t i = 0; i < skinnedRequests.size(); i++)
		{
			if (skinnedBlasAddresses[i] == 0)
				continue;
			if (skinnedMaterialIndexs[i] == std::numeric_limits<uint32_t>::max())
				continue;

			auto& item = *skinnedMatches[i];

			InstanceInfo& info = EmplaceZeroed(tlasInstanceInfos);
			info.model = item.transform;
			info.invModel = glm::inverse(item.transform);
			glm::mat4 mat = transpose(info.invModel);
			info.normalMatRow1 = mat[0];
			info.normalMatRow2 = mat[1];
			info.normalMatRow3 = mat[2];
			info.materialIndex = skinnedMaterialIndexs[i];
			info.indexOffset = skinnedRequests[i].firstIndex;
			info.vertexOffset = 0;										// 蒙皮网格不用静态顶点块
			info.skinned = 1;
			info.skinnedVertexOffset = skinnedRequests[i].vertexStart;
			info.prevModel = item.prevTransform;

			vk::AccelerationStructureInstanceKHR tlasInstance;
			tlasInstance
				.setTransform(toTransformMatrixKHR(item.transform))
				.setInstanceCustomIndex(infosIndex++)
				.setAccelerationStructureReference(skinnedBlasAddresses[i])	// 蒙皮 BLAS 的句柄地址
				.setMask(0xFF)
				.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleCullDisable)
				.setInstanceShaderBindingTableRecordOffset(0);

			tlasInstances.push_back(tlasInstance);
		}
	}

	// ---------------- 没变化就整帧跳过 ----------------
	// TLAS 的输入只有实例数组和各 BLAS 的内容。本帧没有构建 / refit 任何 BLAS，实例数组和实例信息又与上一次
	// 成功构建时逐字节相同，重建出来的 TLAS 完全一样：沿用上一份 TLAS 和两块实例缓冲，不提交任何 GPU 工作。
	// （有 BLAS 被构建或 refit 时，TLAS 节点里存的包围盒会过期，即使实例数组相同也必须重建。）
	const size_t instanceBytes = tlasInstances.size() * sizeof(vk::AccelerationStructureInstanceKHR);
	const size_t instanceInfoBytes = tlasInstanceInfos.size() * sizeof(InstanceInfo);
	const bool blasChanged = staticBlasBuilt > 0 || skinnedBlasRecorded;
	if (!blasChanged
		&& _buffers->_tlasAccelerationStructure != VK_NULL_HANDLE
		&& SameBytes(_lastTlasInstanceBytes, tlasInstances.data(), instanceBytes)
		&& SameBytes(_lastInstanceInfoBytes, tlasInstanceInfos.data(), instanceInfoBytes))
	{
		return true;
	}

	tlasInstancesInfosBlock->WriteDataAsync(cmd, tlasInstanceInfos.data(), instanceInfoBytes);
	tlasInstancesBlock->WriteDataAsync(cmd, tlasInstances.data(), instanceBytes);

	// BLAS 构建 / refit 写完、实例上传完 → TLAS 构建读 BLAS 与实例
	cmd->pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
		vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits::eAllCommands,
		vk::MemoryBarrier(
			vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eAccelerationStructureWriteKHR,
			vk::AccessFlagBits::eAccelerationStructureReadKHR | vk::AccessFlagBits::eShaderRead));

	// 填充 TLAS 几何信息
	vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
	instancesData.arrayOfPointers = vk::False;
	instancesData.data.deviceAddress = tlasInstancesBlock->GetDeviceAddress();

	vk::AccelerationStructureGeometryKHR tlasGeometry{};
	tlasGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
	tlasGeometry.geometry.instances = instancesData;

	// 查询内存需求
	vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{};
	tlasBuildGeometryInfo
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel)
		.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
		.setGeometries(tlasGeometry);

	uint32_t instanceCount = static_cast<uint32_t>(tlasInstances.size());
	vk::AccelerationStructureBuildSizesInfoKHR tlasBuildSizes =
		device->GetHandle().getAccelerationStructureBuildSizesKHR(
			vk::AccelerationStructureBuildTypeKHR::eDevice,
			tlasBuildGeometryInfo,
			instanceCount
		);

	// 分配 TLAS 缓冲区和 scratch 缓冲区，创建 TLAS 句柄 (流程与 BLAS 相同)
	// 两者都会整体重建，扩容时不需要保留旧内容（旧 TLAS 句柄所在的旧 buffer 由 Retire 延迟释放）
	if (tlasBlock->GetSize() < tlasBuildSizes.accelerationStructureSize)
		tlasBlock->SetSize(tlasBuildSizes.accelerationStructureSize, false);
	if (_scratchBlock->GetSize() < tlasBuildSizes.buildScratchSize)
		_scratchBlock->SetSize(tlasBuildSizes.buildScratchSize, false);
	VkDeviceAddress scratchAddress = _scratchBlock->GetDeviceAddress();

	vk::AccelerationStructureCreateInfoKHR tlasCreateInfo;
	tlasCreateInfo
		.setBuffer(tlasBlock->GetHandle())
		.setOffset(0)
		.setSize(tlasBuildSizes.accelerationStructureSize)
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel);

	auto [tlasHandleResult, tlasHandle] = device->GetHandle().createAccelerationStructureKHR(tlasCreateInfo);
	if (tlasHandleResult != vk::Result::eSuccess)
	{
		// 已经录了 BLAS 构建和实例上传，照样提交：BLAS 句柄已经记账，不能留着没建
		if (cmd->IsRecording())
			VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
		_lastTlasInstanceBytes.clear();			// 下一帧不能被判成「没变化」
		_lastInstanceInfoBytes.clear();
		return false;
	}

	// 存回构建信息
	tlasBuildGeometryInfo.dstAccelerationStructure = tlasHandle;
	tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddress;

	// 记录构建命令
	vk::AccelerationStructureBuildRangeInfoKHR tlasBuildRange{};
	tlasBuildRange.primitiveCount = instanceCount;

	const vk::AccelerationStructureBuildRangeInfoKHR* pTlasBuildRange = &tlasBuildRange;
	cmd->buildAccelerationStructuresKHR(tlasBuildGeometryInfo, pTlasBuildRange);

	// TLAS 写完 → 之后提交的光追 / 计算 Pass 读（屏障的第二同步域覆盖提交顺序上之后的所有命令）
	cmd->pipelineBarrier(
		vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
		vk::PipelineStageFlagBits::eAllCommands,
		vk::MemoryBarrier(vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR));

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	if (_buffers->_tlasAccelerationStructure != VK_NULL_HANDLE)
		device->GetHandle().destroyAccelerationStructureKHR(_buffers->_tlasAccelerationStructure);
	_buffers->_tlasAccelerationStructure = tlasHandle;

	// 构建成功才记下这次的输入
	_lastTlasInstanceBytes.assign(reinterpret_cast<const uint8_t*>(tlasInstances.data()), reinterpret_cast<const uint8_t*>(tlasInstances.data()) + instanceBytes);
	_lastInstanceInfoBytes.assign(reinterpret_cast<const uint8_t*>(tlasInstanceInfos.data()), reinterpret_cast<const uint8_t*>(tlasInstanceInfos.data()) + instanceInfoBytes);

	return true;
}

