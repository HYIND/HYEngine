#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"


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
};

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
{
	_blasBufferManager.SetAlign(256);
}

void BlasHandleManager::RemoveBlasData(std::shared_ptr<Mesh>& mesh, std::shared_ptr<VKCore::VulkanDevice>& device)
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

bool BlasHandleManager::FindBlasData(std::shared_ptr<Mesh>& mesh, BlasHandleData& data) {
	auto it = _blasHandles.find(mesh.get());
	if (it == _blasHandles.end())
		return false;
	data = it->second;
	return true;
}

bool BlasHandleManager::UpdateBlasData(
	std::vector<std::shared_ptr<Mesh>>& meshs,
	std::shared_ptr<VertexBufferBlock>& vertexBlock,
	std::shared_ptr<IndexBufferBlock>& indexBlock,
	std::shared_ptr<VKCore::VulkanDevice>& device,
	std::shared_ptr<StorageBlock>& scratchBlock
)
{
	vk::DeviceAddress vertexBufferAddress = vertexBlock->GetDeviceAddress();
	vk::DeviceAddress indexBufferAddress = indexBlock->GetDeviceAddress();

	struct BlasContext {
		std::shared_ptr<Mesh> mesh;
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
		BlasContext(std::shared_ptr<Mesh>& m) :mesh(m) {
			blasData.meshWeakPtr = m;
			blasData.meshVersion = m->GetVerticesIndicesVsrsion();
			blasData.meshUUID = m->GetUUID();
		}
	};

	std::unordered_map<std::shared_ptr<Mesh>, std::shared_ptr<BlasContext>> ctxs;

	auto indirectManager = IndirectDrawManager::Instance();

	auto addToCreate = [&](std::vector<std::shared_ptr<Mesh>>& toCreateMeshs) -> void
		{
			for (auto& mesh : toCreateMeshs)
			{
				if (ctxs.find(mesh) != ctxs.end())
					continue;

				auto ctx = std::make_shared<BlasContext>(mesh);

				auto& vertices = mesh->GetVertices();
				auto& indices = mesh->GetIndices();

				IndirectDrawMeta meta;
				if (!indirectManager->GetIndirectDrawMeta(*mesh, meta))
					continue;

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

		std::vector<std::shared_ptr<Mesh>> hasExistedMeshs;
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


RTCoreRayTraceGeneralBuffer::RTCoreRayTraceGeneralBuffer()
{
	_tlasInstancesBlock = std::make_shared<StorageBlock>();
	_tlasBlock = std::make_shared<StorageBlock>();
	_tlasInstancesInfosBlock = std::make_shared<StorageBlock>();
}

vk::AccelerationStructureKHR RTCoreRayTraceGeneralBuffer::GetAccelerationStructure() const { return _tlasAccelerationStructure; }

std::shared_ptr<StorageBlock> RTCoreRayTraceGeneralBuffer::GetInstancesInfosBlock() const { return _tlasInstancesInfosBlock; }

RTCoreRayTraceGeneralPass::RTCoreRayTraceGeneralPass()
{
	_buffers = std::make_shared<RTCoreRayTraceGeneralBuffer>();
	_fence = std::make_shared< VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());
	_scratchBlock = std::make_shared<StorageBlock>();
}

RTCoreRayTraceGeneralPass::~RTCoreRayTraceGeneralPass()
{}

bool RTCoreRayTraceGeneralPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.rayTraceGIOn || state.option.flags.rayTraceReflectOn;
}

void RTCoreRayTraceGeneralPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	//_fence->WaitAndReset();
}

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

	auto& opaqueMesh = state.objects.sceneRenderData.opaqueMesh;

	std::vector<vk::AccelerationStructureInstanceKHR> tlasInstances;
	std::vector<InstanceInfo> tlasInstanceInfos;
	tlasInstances.reserve(opaqueMesh.size());
	tlasInstanceInfos.reserve(opaqueMesh.size());

	uint32_t infosIndex = 0;

	std::vector<std::shared_ptr<Mesh>> needUpdateBlasMeshs;
	auto getItem = [&](VKRenderObjectData::SceneRenderData::OpaqueMeshItem& item) -> bool {
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

	std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem*> matches;
	for (auto& item : opaqueMesh)
	{
		if (getItem(item))
			matches.push_back(&item);
	}

	_buffers->_blasManager.UpdateBlasData(needUpdateBlasMeshs, vertexBlock, indexBlock, device, _scratchBlock);

	auto blasBlock = _buffers->_blasManager._blasBufferManager.GetBuffer()->GetBlock();
	auto blasBlockAddress = blasBlock->GetDeviceAddress();

	for (auto& itemptr : matches)
	{
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

		IndirectDrawMeta meta;
		uint32_t materialIndex;
		if (!indirectManager->GetIndirectDrawMeta(*mesh, meta) || !indirectManager->GetMaterialIndex(*material, materialIndex))
			continue;

		info.materialIndex = materialIndex;
		info.indexOffset = meta.firstIndex;
		info.vertexOffset = meta.vertexOffset;

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

