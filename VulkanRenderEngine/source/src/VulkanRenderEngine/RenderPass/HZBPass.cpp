#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/HZBPass.h"
//#include "VulkanRenderEngine/General/RenderHelp.h"
//#include "VulkanRenderEngine/General/GPUTimer.h"

constexpr uint32_t work_size_x = 16;
constexpr uint32_t work_size_y = 16;

constexpr uint32_t occ_work_size_x = 256;

struct LevelData {
	uint32_t inputLevel;
	uint32_t outputLevel;
};

void RadixSortByMaterial(std::vector<uint32_t>& indices, const std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem>& meshes)
{
	size_t n = indices.size();
	if (n <= 1) return;

	const int BITS = 16;
	const int RADIX = 1 << BITS;
	const int MASK = RADIX - 1;

	std::vector<uint32_t> temp(n);
	std::vector<uintptr_t> keys(n);
	std::vector<uint32_t> count(RADIX);

	// 提取材质指针值
	for (size_t i = 0; i < n; i++) {
		keys[i] = reinterpret_cast<uintptr_t>(meshes[indices[i]].meshinfo.material.get());
	}

	// 64位指针，4轮16位基数排序
	for (int shift = 0; shift < 64; shift += BITS) {
		std::fill(count.begin(), count.end(), 0);

		for (size_t i = 0; i < n; i++) {
			count[(keys[i] >> shift) & MASK]++;
		}

		uint32_t sum = 0;
		for (int i = 0; i < RADIX; i++) {
			uint32_t t = count[i];
			count[i] = sum;
			sum += t;
		}

		for (size_t i = 0; i < n; i++) {
			uint32_t key = (keys[i] >> shift) & MASK;
			temp[count[key]++] = indices[i];
		}

		indices.swap(temp);
	}
}

HZBPass::HZBPass(
	const std::string& depthVertexShaderPath,
	const std::string& depthfragmentShaderPath,
	const std::string& HZBComputerShaderPath,
	const std::string& OcclusionCullingComputerShaderPath
)
{

	{
		_depthShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = depthVertexShaderPath;
		config.fragmentPath = depthfragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddCameraUnifromDataBinding()
			.AddStorageBuffer(2);

		if (config.Validate())
			_depthShader->Create(config);
	}

	{
		_HZBShader = std::make_shared<ComputePipeline>();

		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", work_size_x);
		config.AddDefineMacro("work_size_y", work_size_y);
		config.computePath = HZBComputerShaderPath;

		config
			.AddStorageVariableImageArray(0, 16)
			.AddPushConstant(sizeof(LevelData));

		if (config.Validate())
			_HZBShader->Create(config);
	}

	{
		_occlusionCullShader = std::make_shared<ComputePipeline>();

		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", occ_work_size_x);
		config.AddDefineMacro("work_size_y", 1);
		config.computePath = OcclusionCullingComputerShaderPath;

		config
			.AddCameraUnifromDataBinding()
			.AddStorageBuffer(2)
			.AddStorageBuffer(3)
			.AddUnifromBuffer(4)
			.AddUnifromTexture(5);

		if (config.Validate())
			_occlusionCullShader->Create(config);
	}

	_indirectCommandBuffer = std::make_shared<IndirectBufferBlock>();
}

HZBPass::~HZBPass() {}

void HZBPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto depthMap = ctx.GetTemp(0);
	auto HZBMap = ctx.GetOutput(0);

	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");

	auto cmd = VKCONTEXT->GetCommandBuffer();

	DrawDepthMap(cmd, registry, depthMap, state);
	DrawHZB(cmd, registry, depthMap, HZBMap, state);

	if (state.option.flags.calculateOcclusionCulling)
	{
		GetOcclusionCulling(cmd, registry, HZBMap, state);
	}
	else
	{
		auto& items = state.objects.sceneRenderData.opaqueMesh;
		auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_cullRenderIndex;
		auto& oneSideIndex = renderIndex.oneSideIndex;
		auto& twoSideIndex = renderIndex.twoSideIndex;

		for (size_t i = 0; i < frustumObjectIndex.size(); i++)
		{
			auto meshIndex = frustumObjectIndex[i];
			if (items[meshIndex].meshinfo.material->GetTwoSided())
				twoSideIndex.push_back(meshIndex);
			else
				oneSideIndex.push_back(meshIndex);
		}
	}
}

void HZBPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	Frustum& frustum = state.camera.frustum;
	auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;

	std::vector<bool> frustumCullResult;
	std::vector<uint32_t> frustumObjectIndex;
	std::vector<AABB> frustumObjectMeshaabbs;
	std::vector<glm::mat4> frustumObjectTransforms;
	std::vector<int> frustumOcclusionCullResult;


	if (!opaqueMeshes.empty())
	{
		frustumCullResult.resize(opaqueMeshes.size(), false);
		frustumObjectMeshaabbs.resize(opaqueMeshes.size());

		std::for_each(std::execution::par, opaqueMeshes.begin(), opaqueMeshes.end(),
			[&](VKRenderObjectData::SceneRenderData::OpaqueMeshItem& item)
			{
				uint32_t meshIndex = &item - opaqueMeshes.data();

				AABB aabbworld = item.meshinfo.mesh->GetAABB();
				aabbworld.MakeTransform(item.transform);

				frustumObjectMeshaabbs[meshIndex] = aabbworld;
				frustumCullResult[meshIndex] = !frustum.IsAABBOnFrustum(aabbworld);
			});
	}

	frustumObjectIndex.reserve(opaqueMeshes.size());
	frustumObjectTransforms.reserve(opaqueMeshes.size());
	size_t writePos = 0;
	for (int i = 0; i < opaqueMeshes.size(); i++)
	{
		if (frustumCullResult[i]) continue;

		if (writePos != i)
			frustumObjectMeshaabbs[writePos] = frustumObjectMeshaabbs[i];

		frustumObjectIndex.push_back(i);
		frustumObjectTransforms.push_back(opaqueMeshes[i].transform);
		writePos++;
	}

	frustumOcclusionCullResult.resize(writePos, 0);
	frustumObjectMeshaabbs.resize(writePos);

	auto indirectManager = IndirectDrawManager::Instance();
	_commands.resize(frustumObjectIndex.size());
	std::for_each(std::execution::par, frustumObjectIndex.begin(), frustumObjectIndex.end(),
		[&](uint32_t& meshIndex)-> void
		{
			size_t inedx = &meshIndex - frustumObjectIndex.data();
			IndirectDrawCommand& command = _commands[inedx];

			auto& item = opaqueMeshes[meshIndex];

			IndirectDrawMeta meta;
			if (!indirectManager->GetIndirectDrawMeta(*(item.meshinfo.mesh), meta))
			{
				command.instanceCount = 0;
				return;
			}

			command.indexCount = meta.indexCount;
			command.firstIndex = meta.firstIndex;
			command.vertexOffset = meta.vertexOffset;
			command.instanceCount = 1;
			command.firstInstance = inedx;
		});

	registry.Store("frustumCullResult", std::move(frustumCullResult));
	registry.Store("frustumObjectIndex", std::move(frustumObjectIndex));
	registry.Store("frustumObjectMeshaabbs", std::move(frustumObjectMeshaabbs));
	registry.Store("frustumObjectTransforms", std::move(frustumObjectTransforms));
	registry.Store("frustumOcclusionCullResult", std::move(frustumOcclusionCullResult));
}

void HZBPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	_commands.clear();
}

uint32_t HZBPass::GetMaxLevel()
{
	return _maxLevel;
}

void HZBPass::DrawDepthMap(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, RenderState& state)
{
	auto& frustumObjectTransforms = registry.Load<std::vector<glm::mat4>>("frustumObjectTransforms");
	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");

	auto transform_ssbo = _depthShaderBinding.GetStorageBlock(2);
	transform_ssbo->WriteDataAsync(cmd, frustumObjectTransforms.data(), frustumObjectTransforms.size() * sizeof(glm::mat4));
	_indirectCommandBuffer->WriteDataAsync(cmd, _commands.data(), _commands.size() * sizeof(IndirectDrawCommand));

	transform_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	_indirectCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);

	{
		vk::BufferMemoryBarrier bufferBarrier{};
		bufferBarrier
			.setBuffer(transform_ssbo->GetBuffer()->GetHandle())
			.setOffset(0)
			.setSize(vk::WholeSize)
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
		cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eVertexShader, bufferBarrier);
	}

	{
		vk::BufferMemoryBarrier bufferBarrier{};
		bufferBarrier
			.setBuffer(_indirectCommandBuffer->GetBuffer()->GetHandle())
			.setOffset(0)
			.setSize(vk::WholeSize)
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead);
		cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eDrawIndirect, bufferBarrier);
	}


	cmd->setDynamicViewport(depthMap->GetWidth(), depthMap->GetHeight());

	depthMap->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);

	DynamicRenderInfo renderingInfo;
	renderingInfo
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height)
		.AddDepthAttachment(depthMap->GetImageView());
	cmd->beginRendering(renderingInfo);

	_depthShaderBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);

	_depthShader->Bind(cmd, _depthShaderBinding);

	auto manager = IndirectDrawManager::Instance();

	cmd->bindVertexBuffers(manager->GetVertexBlock());
	cmd->bindIndexBuffer(manager->GetIndexBlock());
	cmd->drawIndexedIndirect(_indirectCommandBuffer, _commands.size());

	cmd->endRendering();
}

void HZBPass::DrawHZB(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, std::shared_ptr<Texture2D>& HZBMap, RenderState& state)
{
	Texture2D::CopyTextureAsync(cmd, depthMap, HZBMap);

	auto width = depthMap->GetWidth();
	auto height = depthMap->GetHeight();

	std::vector<BindingRecord::StorageImageEntry> entrys;
	for (uint32_t level = 0; level < _maxLevel; level++)
		entrys.push_back(BindingRecord::StorageImageEntry{ .texture = HZBMap, .usage = Texture2D::BindUsage::Sample, .baseLevel = level ,.levelCount = 1 });

	LevelData data;

	_HZBShaderBinding.SetStorageImageArray(entrys, 0);
	_HZBShader->Bind(cmd, _HZBShaderBinding);

	for (uint32_t level = 1; level < _maxLevel; level++) {
		uint32_t prevW = std::max(1u, width >> (level - 1));
		uint32_t prevH = std::max(1u, height >> (level - 1));
		uint32_t currW = std::max(1u, width >> level);
		uint32_t currH = std::max(1u, height >> level);

		data.inputLevel = level - 1;
		data.outputLevel = level;

		_HZBShader->SetPushConstants(cmd, &data, sizeof(data));
		cmd->dispatch((currW + work_size_x - 1) / work_size_x, (currH + work_size_y - 1) / work_size_y, 1);

		if (level < _maxLevel - 1)
			HZBMap->Barrier(cmd, nullptr, Texture2D::BindStage::Compute, Texture2D::BindUsage::Sample);
	}

	//for (uint32_t level = 0; level < _maxLevel; level++)
	//	DrawTexture(HZBMap, std::format("temp/HZBMap{}.png", level), level);
}

void HZBPass::GetOcclusionCulling(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& HZBMap, RenderState& state)
{

	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");
	auto& frustumObjectMeshaabbs = registry.Load<std::vector<AABB>>("frustumObjectMeshaabbs");
	auto& frustumOcclusionCullResult = registry.Load<std::vector<int>>("frustumOcclusionCullResult");

	struct alignas(16) OccData {
		glm::mat4 viewProj;
		glm::ivec2 depthMapSize;
		int count;
		int maxLevel;
	};
	OccData data{
		.viewProj = state.camera.projection * state.camera.view,
		.depthMapSize = HZBMap->GetSize(),
		.count = int(frustumObjectIndex.size()),
		.maxLevel = int(HZBMap->GetMaxLevel())
	};

	_occlusionCullShaderBinding.SetUniformBlock(state.camera.curUBO, GeneralBindingPoint::Camera_Cur);
	_occlusionCullShaderBinding.SetUniformBlock(state.camera.prevUBO, GeneralBindingPoint::Camera_Prev);
	auto aabb_ssbo = _occlusionCullShaderBinding.GetStorageBlock(2);
	auto result_ssbo = _occlusionCullShaderBinding.GetStorageBlock(3);
	auto occDataBlock = _occlusionCullShaderBinding.GetUniformBlock(4);

	if (uint64_t size = frustumObjectMeshaabbs.size() * sizeof(AABB); aabb_ssbo->GetSize() < size)
		aabb_ssbo->SetSizeAsync(cmd, size * 1.2);
	if (uint64_t size = frustumOcclusionCullResult.size() * sizeof(int); result_ssbo->GetSize() < size)
		result_ssbo->SetSizeAsync(cmd, size * 1.2);

	aabb_ssbo->WriteDataAsync(cmd, frustumObjectMeshaabbs.data(), frustumObjectMeshaabbs.size() * sizeof(AABB));
	occDataBlock->WriteDataAsync(cmd, &data, sizeof(data));

	aabb_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	occDataBlock->Barrier(cmd, BufferUsage::TransferWrite);

	_occlusionCullShaderBinding.SetUniformTexture(HZBMap, 5);

	_occlusionCullShader->Bind(cmd, _occlusionCullShaderBinding);
	cmd->dispatch((frustumObjectIndex.size() + occ_work_size_x - 1) / occ_work_size_x, 1, 1);

	// 内含隐式Submit
	result_ssbo->GetBuffer()->Readback(cmd, frustumOcclusionCullResult.data(), frustumOcclusionCullResult.size() * sizeof(int));
	//VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	auto& items = state.objects.sceneRenderData.opaqueMesh;
	auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_cullRenderIndex;
	auto& oneSideIndex = renderIndex.oneSideIndex;
	auto& twoSideIndex = renderIndex.twoSideIndex;

	for (size_t i = 0; i < frustumObjectIndex.size(); i++)
	{
		if (frustumOcclusionCullResult[i] > 0) continue;

		auto meshIndex = frustumObjectIndex[i];
		if (items[meshIndex].meshinfo.material->GetTwoSided())
			twoSideIndex.push_back(meshIndex);
		else
			oneSideIndex.push_back(meshIndex);
	}
	//std::cout << std::format("frustumObjectSize = {}, renderObjectSize = {}\n", frustumObjectIndex.size(), oneSideIndex.size() + twoSideIndex.size());
}
