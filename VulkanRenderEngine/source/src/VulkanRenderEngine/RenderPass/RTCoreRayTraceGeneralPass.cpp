#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"
#include <unordered_set>


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
};

static_assert(sizeof(InstanceInfo) == 208, "InstanceInfo 布局要和 ClosestHit.rchit 里的同名结构保持一致");

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

bool BlasHandleManager::UpdateBlasData(
	std::vector<std::shared_ptr<StaticMesh>>& meshs,
	std::shared_ptr<VertexBufferBlock>& vertexBlock,
	std::shared_ptr<IndexBufferBlock>& indexBlock,
	std::shared_ptr<VKCore::VulkanDevice>& device,
	std::shared_ptr<StorageBlock>& scratchBlock
)
{
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
		BlasContext(std::shared_ptr<StaticMesh>& m) :mesh(m) {
			blasData.meshWeakPtr = m;
			blasData.meshVersion = m->GetVerticesIndicesVsrsion();
			blasData.meshUUID = m->GetUUID();
		}
	};

	std::unordered_map<std::shared_ptr<StaticMesh>, std::shared_ptr<BlasContext>> ctxs;

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
				if (ctxs.find(mesh) != ctxs.end())
					continue;

				auto& meta = indirectDrawMetas[i];
				if (meta.vertexOffset < 0)
					continue;

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

				ctx->blasBuildGeometryInfo
					.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
					.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
					.setGeometries(ctx->blasGeometry);

				ctx->blasBuildSizes = device->GetHandle().getAccelerationStructureBuildSizesKHR(
					vk::AccelerationStructureBuildTypeKHR::eDevice,
					ctx->blasBuildGeometryInfo,
					indices.size() / 3
				);

				auto blasSegData = _blasBufferManager.SetSegment(mesh->GetUUID(), (void*)mesh->GetVerticesIndicesVsrsion(), nullptr, ctx->blasBuildSizes.accelerationStructureSize);
				ctx->blasFirst = blasSegData.first;

				ctxs[mesh] = std::move(ctx);
			}
		};

	addToCreate(meshs);


	// vertexAddress 或者 indexAddress 发生变化，需要重建原有的blas
	auto vertexAddress = vertexBlock->GetDeviceAddress();
	auto indexAddress = indexBlock->GetDeviceAddress();
	if (_lastVertexAddress == 0
		|| _lastIndexAddress == 0
		|| _lastVertexAddress != vertexAddress
		|| _lastIndexAddress != indexAddress
		)
	{
		_lastVertexAddress = vertexAddress;
		_lastIndexAddress = indexAddress;

		std::vector<std::shared_ptr<StaticMesh>> hasExistedMeshs;
		for (auto& it : _blasHandles)
		{
			auto& data = it.second;
			if (data.handle != VK_NULL_HANDLE)
				device->GetHandle().destroyAccelerationStructureKHR(data.handle);
			_blasBufferManager.RemoveSegment(data.meshUUID);

			if (auto mesh = data.meshWeakPtr.lock())
				hasExistedMeshs.push_back(mesh);
		}
		_blasHandles.clear();

		addToCreate(hasExistedMeshs);
	}


	auto blasBlock = _blasBufferManager.GetBuffer()->GetBlock();
	auto blasBlockHandle = blasBlock->GetHandle();

	auto cmd = VKCONTEXT->GetCommandBuffer();

	for (auto& [mesh, ctx] : ctxs)
	{
		auto& vertices = ctx->mesh->GetVertices();
		auto& indices = ctx->mesh->GetIndices();

		scratchBlock->SetSize(ctx->blasBuildSizes.buildScratchSize);
		VkDeviceAddress scratchAddress = scratchBlock->GetDeviceAddress();

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
		ctx->blasBuildGeometryInfo.scratchData.deviceAddress = scratchAddress;

		ctx->buildRange.primitiveCount = indices.size() / 3;									// 三角形数量
		ctx->buildRange.primitiveOffset = ctx->indicesOffset * sizeof(unsigned int);			// 索引缓冲区字节偏移
		ctx->buildRange.firstVertex = ctx->vertexOffset;										// 顶点缓冲区顶点索引偏移
		ctx->buildRange.transformOffset = 0;

		cmd->buildAccelerationStructuresKHR(ctx->blasBuildGeometryInfo, &(ctx->buildRange));
		VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

		ctx->blasData.handle = blasHandle;
		ctx->blasData.offset = ctx->blasFirst;

		_blasHandles[ctx->mesh.get()] = std::move(ctx->blasData);
	}

	return true;
}


// 蒙皮网格的 BLAS：第一次 eBuild，之后每帧 eUpdate（refit）。
// 顶点每帧都变，但拓扑不变，所以不用重建句柄，只需要把新的顶点地址喂进去。
bool BlasHandleManager::UpdateSkinnedBlas(
	std::vector<SkinnedBlasRequest>& requests,
	std::vector<vk::DeviceAddress>& outAddresses,
	std::shared_ptr<IndexBufferBlock>& indexBlock,
	std::shared_ptr<VKCore::VulkanDevice>& device,
	std::shared_ptr<StorageBlock>& scratchBlock
)
{
	outAddresses.assign(requests.size(), 0);

	if (requests.empty() || !device || !indexBlock)
		return true;

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
		return true;

	// 所有 BLAS 共用一块 scratch，各自占一段，最后一次提交
	uint64_t totalScratch = 0;
	for (auto& unitPtr : units)
	{
		auto& unit = *unitPtr;
		const uint32_t need = unit.create ? unit.buildSizes.buildScratchSize : unit.data->scratchSize;
		totalScratch = (totalScratch + 255) / 256 * 256;
		unit.scratchOffset = totalScratch;
		totalScratch += need;
	}

	if (totalScratch > 0)
		scratchBlock->SetSize(totalScratch);

	const vk::DeviceAddress scratchAddress = scratchBlock->GetDeviceAddress();
	auto blasBlockHandle = _skinnedBlasBufferManager.GetBuffer()->GetBlock()->GetHandle();

	auto cmd = VKCONTEXT->GetCommandBuffer();

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

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

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
}

RTCoreRayTraceGeneralPass::~RTCoreRayTraceGeneralPass()
{}

bool RTCoreRayTraceGeneralPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.rayTraceGIOn || state.option.flags.rayTraceReflectOn;
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
	static auto writePaddingCount = [](int count, std::shared_ptr<StorageBlock>& ssbo)-> void {
		glm::ivec4 padding = glm::ivec4(count, 0.f, 0.f, 0.f);
		ssbo->WriteData(&padding, sizeof(padding), 0);
		};

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

	std::vector<vk::AccelerationStructureInstanceKHR> tlasInstances;
	std::vector<InstanceInfo> tlasInstanceInfos;
	tlasInstances.reserve(opaqueItems.size());
	tlasInstanceInfos.reserve(opaqueItems.size());

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

	_buffers->_blasManager.UpdateBlasData(needUpdateBlasMeshs, vertexBlock, indexBlock, device, _scratchBlock);

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

		InstanceInfo info;
		info.model = item.transform;
		info.invModel = glm::inverse(item.transform);
		glm::mat4 mat = transpose(info.invModel);
		info.normalMatRow1 = mat[0];
		info.normalMatRow2 = mat[1];
		info.normalMatRow3 = mat[2];

		auto& mesh = meshInfo.mesh;
		auto& material = meshInfo.material;
		auto meshVersion = mesh->GetVerticesIndicesVsrsion();
		auto meshuuid = mesh->GetUUID();

		auto& meta = indirectDrawMetas[index];
		uint32_t& materialIndex = materialIndexs[index];
		if (meta.vertexOffset < 0 || materialIndex == std::numeric_limits<uint32_t>::max())
			continue;

		info.materialIndex = materialIndex;
		info.indexOffset = meta.firstIndex;
		info.vertexOffset = meta.vertexOffset;
		info.skinned = 0;
		info.skinnedVertexOffset = 0;

		BlasHandleManager::BlasHandleData data;
		if (!_buffers->_blasManager.FindBlasData(mesh, data)
			|| data.handle == VK_NULL_HANDLE)
			continue;

		vk::AccelerationStructureInstanceKHR tlasInstance;
		tlasInstance
			.setTransform(toTransformMatrixKHR(item.transform))
			.setInstanceCustomIndex(infosIndex++)											// 设置自定义索引 (用于在着色器中查找实例信息)
			.setAccelerationStructureReference(blasBlockAddress + data.offset)			// 设置 BLAS 设备地址
			.setMask(0xFF)
			.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleCullDisable)
			.setInstanceShaderBindingTableRecordOffset(0);

		tlasInstances.push_back(tlasInstance);
		tlasInstanceInfos.push_back(std::move(info));
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

	auto& skinnedObjects = state.objects.sceneSkinnedRenderData;
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
		_buffers->_blasManager.UpdateSkinnedBlas(skinnedRequests, skinnedBlasAddresses, indexBlock, device, _scratchBlock);
		_buffers->_blasManager.PruneSkinnedBlas(device, 240);

		for (size_t i = 0; i < skinnedRequests.size(); i++)
		{
			if (skinnedBlasAddresses[i] == 0)
				continue;
			if (skinnedMaterialIndexs[i] == std::numeric_limits<uint32_t>::max())
				continue;

			auto& item = *skinnedMatches[i];

			InstanceInfo info;
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

			vk::AccelerationStructureInstanceKHR tlasInstance;
			tlasInstance
				.setTransform(toTransformMatrixKHR(item.transform))
				.setInstanceCustomIndex(infosIndex++)
				.setAccelerationStructureReference(skinnedBlasAddresses[i])	// 蒙皮 BLAS 的句柄地址
				.setMask(0xFF)
				.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleCullDisable)
				.setInstanceShaderBindingTableRecordOffset(0);

			tlasInstances.push_back(tlasInstance);
			tlasInstanceInfos.push_back(std::move(info));
		}
	}

	tlasInstancesInfosBlock->WriteData(tlasInstanceInfos.data(), tlasInstanceInfos.size() * sizeof(InstanceInfo));
	tlasInstancesBlock->WriteData(tlasInstances.data(), tlasInstances.size() * sizeof(vk::AccelerationStructureInstanceKHR));

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
	tlasBlock->SetSize(tlasBuildSizes.accelerationStructureSize);
	_scratchBlock->SetSize(tlasBuildSizes.buildScratchSize);
	VkDeviceAddress scratchAddress = _scratchBlock->GetDeviceAddress();

	vk::AccelerationStructureCreateInfoKHR tlasCreateInfo;
	tlasCreateInfo
		.setBuffer(tlasBlock->GetHandle())
		.setOffset(0)
		.setSize(tlasBuildSizes.accelerationStructureSize)
		.setType(vk::AccelerationStructureTypeKHR::eTopLevel);

	auto [tlasHandleResult, tlasHandle] = device->GetHandle().createAccelerationStructureKHR(tlasCreateInfo);
	if (tlasHandleResult != vk::Result::eSuccess)
		return false;

	// 存回构建信息
	tlasBuildGeometryInfo.dstAccelerationStructure = tlasHandle;
	tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddress;

	// 记录构建命令
	vk::AccelerationStructureBuildRangeInfoKHR tlasBuildRange{};
	tlasBuildRange.primitiveCount = instanceCount;

	const vk::AccelerationStructureBuildRangeInfoKHR* pTlasBuildRange = &tlasBuildRange;
	auto cmd = VKCONTEXT->GetCommandBuffer();
	cmd->buildAccelerationStructuresKHR(tlasBuildGeometryInfo, pTlasBuildRange);
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	if (_buffers->_tlasAccelerationStructure != VK_NULL_HANDLE)
		device->GetHandle().destroyAccelerationStructureKHR(_buffers->_tlasAccelerationStructure);
	_buffers->_tlasAccelerationStructure = tlasHandle;

	return true;
}

