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

void RadixSortByMaterial(std::vector<uint32_t>& indices, const std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& meshes)
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
}

HZBPass::~HZBPass() {
}

void HZBPass::EarlyExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
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
			[&](OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem& item)
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

	registry.Store("frustumCullResult", std::move(frustumCullResult));
	registry.Store("frustumObjectIndex", std::move(frustumObjectIndex));
	registry.Store("frustumObjectMeshaabbs", std::move(frustumObjectMeshaabbs));
	registry.Store("frustumObjectTransforms", std::move(frustumObjectTransforms));
	registry.Store("frustumOcclusionCullResult", std::move(frustumOcclusionCullResult));

}

void HZBPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{
	auto depthMap = ctx.GetTemp(0);
	auto HZBMap = ctx.GetOutput(0);

	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");

	DrawDepthMap(registry, depthMap, state);
	DrawHZB(registry, depthMap, HZBMap, state);

	if (state.option.flags.calculateOcclusionCulling)
	{
		GetOcclusionCulling(registry, HZBMap, state);
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

	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");

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
}

void HZBPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	_commands.clear();
}

uint32_t HZBPass::GetMaxLevel()
{
	return _maxLevel;
}

void HZBPass::DrawDepthMap(RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, RenderState& state)
{

	auto& frustumObjectTransforms = registry.Load<std::vector<glm::mat4>>("frustumObjectTransforms");
	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");

	auto transform_ssbo = _depthShader->GetStorageBlock(2);
	transform_ssbo->WriteData(frustumObjectTransforms.data(), frustumObjectTransforms.size() * sizeof(glm::mat4));
	state.indirectCommands.indirectCommandBuffer->WriteData(_commands.data(), _commands.size() * sizeof(IndirectDrawCommand));

	auto cmd = VKCONTEXT->GetCommandBuffer();
	cmd->Begin();
	cmd->setDynamicViewport(depthMap->GetWidth(), depthMap->GetHeight());

	depthMap->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);

	DynamicRenderInfo renderingInfo;
	renderingInfo
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height)
		.AddDepthAttachment(depthMap->GetImageView());
	cmd->beginRendering(renderingInfo);

	_depthShader->SetUniformBlock(state.camera.curUBO, GeneralBindingPoint::Camera_Cur);
	_depthShader->SetUniformBlock(state.camera.prevUBO, GeneralBindingPoint::Camera_Prev);

	_depthShader->Bind(cmd);

	auto manager = IndirectDrawManager::Instance();

	cmd->bindVertexBuffers(manager->GetVertexBlock());
	cmd->bindIndexBuffer(manager->GetIndexBlock());
	cmd->drawIndexedIndirect(state.indirectCommands.indirectCommandBuffer, _commands.size());

	cmd->endRendering();
	cmd->End();
	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
}

void HZBPass::DrawHZB(RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, std::shared_ptr<Texture2D>& HZBMap, RenderState& state)
{
	Texture2D::CopyTexture(depthMap, HZBMap);

	auto width = depthMap->GetWidth();
	auto height = depthMap->GetHeight();

	auto cmd = VKCONTEXT->GetCommandBuffer();

	std::vector<Pipeline::StorageImageEntry> entrys;
	for (uint32_t level = 0; level < _maxLevel; level++)
		entrys.push_back(Pipeline::StorageImageEntry{ .texture = HZBMap, .usage = Texture2D::BindUsage::Sample, .baseLevel = level ,.levelCount = 1 });

	LevelData data;

	_HZBShader->SetStorageImageArray(entrys, 0);
	_HZBShader->Bind(cmd);

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

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
	//cmd->Reset();


	//for (uint32_t level = 0; level < _maxLevel; level++)
	//	DrawTexture(HZBMap, std::format("temp/HZBMap{}.png", level), level);
}

void HZBPass::GetOcclusionCulling(RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& HZBMap, RenderState& state)
{
	auto cmd = VKCONTEXT->GetCommandBuffer();

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

	_occlusionCullShader->SetUniformBlock(state.camera.curUBO, GeneralBindingPoint::Camera_Cur);
	_occlusionCullShader->SetUniformBlock(state.camera.prevUBO, GeneralBindingPoint::Camera_Prev);
	auto aabb_ssbo = _occlusionCullShader->GetStorageBlock(2);
	auto result_ssbo = _occlusionCullShader->GetStorageBlock(3);
	auto occDataBlock = _occlusionCullShader->GetUniformBlock(4);

	if (uint64_t size = frustumObjectMeshaabbs.size() * sizeof(AABB); aabb_ssbo->GetSize() < size)
		aabb_ssbo->SetSize(size * 1.2);
	if (uint64_t size = frustumOcclusionCullResult.size() * sizeof(int); result_ssbo->GetSize() < size)
		result_ssbo->SetSize(size * 1.2);

	aabb_ssbo->WriteData(frustumObjectMeshaabbs.data(), frustumObjectMeshaabbs.size() * sizeof(AABB));
	occDataBlock->WriteData(&data, sizeof(data));

	_occlusionCullShader->SetUniformTexture(HZBMap, 5);

	_occlusionCullShader->Bind(cmd);
	cmd->dispatch((frustumObjectIndex.size() + occ_work_size_x - 1) / occ_work_size_x, 1, 1);

	VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);

	result_ssbo->GetBuffer()->Readback(frustumOcclusionCullResult.data(), frustumOcclusionCullResult.size() * sizeof(int));

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
