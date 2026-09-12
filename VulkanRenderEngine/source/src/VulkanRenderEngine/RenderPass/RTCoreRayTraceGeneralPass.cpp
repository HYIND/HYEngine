#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/RTCoreRayTraceGeneralPass.h"
#include "VulkanRenderEngine/GlobalConfig.h"

struct BlasData {
	uint32_t meshVersion = 0;
	vk::AccelerationStructureKHR handle;
	uint32_t offset;
};

struct alignas(16) InstanceInfo
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 invModel;

	glm::vec4 normalMatRow1;
	glm::vec4 normalMatRow2;
	glm::vec4 normalMatRow3;

	uint32_t materialIndex;
	uint32_t primitiveOffset;
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


RTCoreRayTraceGeneralBuffer::RTCoreRayTraceGeneralBuffer()
	:_blasBufferManager(1024 * 1024 * 200)
{
	_scratchBlock = std::make_shared<StorageBlock>();
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
}

RTCoreRayTraceGeneralPass::~RTCoreRayTraceGeneralPass()
{}

bool RTCoreRayTraceGeneralPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.rayTraceGIOn || state.option.flags.rayTraceReflectOn;
}

void RTCoreRayTraceGeneralPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
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

	auto traiangleBlock = indirectManager->GetVertexBlock();
	auto indexBlock = indirectManager->GetIndexBlock();

	auto& blasBufferManager = _buffers->_blasBufferManager;

	auto& tlasBlock = _buffers->_tlasBlock;
	auto& tlasInstancesBlock = _buffers->_tlasInstancesBlock;
	auto& tlasInstancesInfosBlock = _buffers->_tlasInstancesInfosBlock;

	auto& scratchBlock = _buffers->_scratchBlock;

	auto device = VKCONTEXT->GetDevice();
	auto blasBuffer = blasBufferManager.GetBuffer()->GetBlock()->GetHandle();
	vk::DeviceAddress blasBufferAddress = blasBufferManager.GetBuffer()->GetBlock()->GetDeviceAddress();

	auto& opaqueMesh = state.objects.sceneRenderData.opaqueMesh;

	uint32_t tlasInstanceCount = 0;

	std::vector<vk::AccelerationStructureInstanceKHR> tlasInstances;
	tlasInstances.reserve(opaqueMesh.size());

	std::vector<InstanceInfo> tlasInstanceInfos;
	tlasInstanceInfos.reserve(opaqueMesh.size());

	uint32_t infosIndex = 0;

	for (auto& item : opaqueMesh)
	{
		auto& meshInfo = item.meshinfo;
		if (!meshInfo.mesh || !meshInfo.material) continue;

		{
			AABB aabb = meshInfo.mesh->GetAABB();
			aabb.MakeTransform(item.transform);
			float dis_sqrt = aabb.DistancePointToAABBSqrt(state.camera.position);
			if (dis_sqrt > maxCacheClearDistanceSqrt)
			{
				//SegmentData data;
				//traiangleBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
				//indexBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
				//meshBVHNodeBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
				continue;
			}
			else if (dis_sqrt > maxDistanceSqrt)
				continue;
		}

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

		uint32_t triangleFirst = meta.vertexOffset;
		uint32_t indexFirst = meta.firstIndex;

		info.materialIndex = materialIndex;
		info.primitiveOffset = indexFirst;

		BlasData* blasData = nullptr;

		{
			SegmentData blasSegData;
			bool find = blasBufferManager.FindSegment(meshuuid, blasSegData);
			if (find)
				blasData = (BlasData*)blasSegData.userData;
		}

		if (blasData == nullptr || blasData->meshVersion != meshVersion || blasData->handle == VK_NULL_HANDLE)
		{
			auto& vertices = meshInfo.mesh->GetVertices();
			auto& indices = meshInfo.mesh->GetIndices();

			vk::DeviceAddress vertexBufferAddress = traiangleBlock->GetDeviceAddress();
			vk::DeviceAddress indexBufferAddress = indexBlock->GetDeviceAddress();

			vk::AccelerationStructureGeometryTrianglesDataKHR trianglesData(
				vk::Format::eR32G32B32Sfloat,
				vertexBufferAddress,
				sizeof(Vertex),
				vertices.size() - 1,
				vk::IndexType::eUint32,
				indexBufferAddress
			);

			vk::AccelerationStructureGeometryDataKHR geometryData(trianglesData);

			vk::AccelerationStructureGeometryKHR blasGeometry(
				vk::GeometryTypeKHR::eTriangles,
				geometryData,
				vk::GeometryFlagBitsKHR::eOpaque
			);

			vk::AccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo;
			blasBuildGeometryInfo
				.setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
				.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
				.setGeometries(blasGeometry);

			vk::AccelerationStructureBuildSizesInfoKHR blasBuildSizes =
				device->GetHandle().getAccelerationStructureBuildSizesKHR(
					vk::AccelerationStructureBuildTypeKHR::eDevice,
					blasBuildGeometryInfo,
					indices.size() / 3
				);

			blasData = new BlasData();
			blasData->meshVersion = meshVersion;
			auto blasSegData = blasBufferManager.SetSegment(meshuuid, blasData, nullptr, blasBuildSizes.accelerationStructureSize);
			uint32_t blasFirst = blasSegData.first;

			scratchBlock->SetSize(blasBuildSizes.buildScratchSize);
			VkDeviceAddress scratchAddress = scratchBlock->GetDeviceAddress();

			vk::AccelerationStructureCreateInfoKHR blasCreateInfo;
			blasCreateInfo
				.setBuffer(blasBuffer)
				.setOffset(blasFirst)
				.setSize(blasBuildSizes.accelerationStructureSize)
				.setType(vk::AccelerationStructureTypeKHR::eBottomLevel);

			auto [blasHandleResult, blasHandle] = device->GetHandle().createAccelerationStructureKHR(blasCreateInfo);
			if (blasHandleResult != vk::Result::eSuccess)
				continue;

			// 存回构建信息
			blasBuildGeometryInfo.dstAccelerationStructure = blasHandle;
			blasBuildGeometryInfo.scratchData.deviceAddress = scratchAddress;

			vk::AccelerationStructureBuildRangeInfoKHR buildRange{};
			buildRange.primitiveCount = indices.size() / 3;							// 三角形数量
			buildRange.primitiveOffset = indexFirst * sizeof(unsigned int);			// 索引缓冲区字节偏移
			buildRange.firstVertex = triangleFirst;									// 顶点缓冲区顶点索引偏移
			buildRange.transformOffset = 0;

			const vk::AccelerationStructureBuildRangeInfoKHR* pBuildRange = &buildRange;

			auto cmd = VKCONTEXT->GetCommandBuffer();
			cmd->buildAccelerationStructuresKHR(blasBuildGeometryInfo, pBuildRange);
			VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

			blasData->handle = blasHandle;
			blasData->offset = blasFirst;
		}

		vk::AccelerationStructureKHR blasHandle = blasData->handle;

		vk::AccelerationStructureInstanceKHR tlasInstance;
		tlasInstance
			.setTransform(toTransformMatrixKHR(item.transform))
			.setInstanceCustomIndex(infosIndex++)												// 设置自定义索引 (用于在着色器中查找实例信息)
			.setAccelerationStructureReference(blasBufferAddress + blasData->offset)			// 设置 BLAS 设备地址
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
	scratchBlock->SetSize(tlasBuildSizes.buildScratchSize);
	VkDeviceAddress scratchAddress = scratchBlock->GetDeviceAddress();

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
