#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/HZBPass.h"
//#include "VulkanRenderEngine/General/RenderHelper.h"
//#include "VulkanRenderEngine/General/GPUTimer.h"


struct LevelData {
	uint32_t inputLevel;
	uint32_t outputLevel;
};

void RadixSortByMaterial(std::vector<uint32_t>& indices, const std::vector<VKRenderObjectData::SceneStaticRenderData::MeshItem>& meshes)
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

		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

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
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X);
		config.AddDefineMacro("work_size_y", GlobalConfig::Global_WorkSize_Y);
		config.computePath = HZBComputerShaderPath;

		config
			.AddStorageVariableImageArray(0, 32)
			.AddPushConstant(sizeof(LevelData));

		if (config.Validate())
			_HZBShader->Create(config);
	}

	{
		_occlusionCullShader = std::make_shared<ComputePipeline>();

		ComputePipelineConfig config;
		config.AddDefineMacro("work_size_x", GlobalConfig::Global_WorkSize_X * GlobalConfig::Global_WorkSize_Y);
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

HZBPass::~HZBPass() {}

void HZBPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto depthMap = ctx.GetTemp(0);
	auto HZBMap = ctx.GetOutput(0);
	auto HZBMinMap = ctx.GetOutput(1);

	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");

	auto cmd = cmdCtx.GetCmd();

	DrawDepthMap(cmd, registry, depthMap, state);
	DrawHZB(cmd, registry, depthMap, HZBMap, HZBMinMap, state);

	if (state.option.flags.calculateOcclusionCulling)
	{
		GetOcclusionCulling(cmd, registry, HZBMap, state);
	}
	else
	{
		auto& items = state.objects.sceneStaticRenderData.opaqueItems;
		auto& renderIndex = state.objects.sceneStaticRenderData.opaqueItems_cullRenderIndex;
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

		auto& transparentIndex = state.objects.sceneStaticRenderData.transparentItems_cullRenderIndex;
		auto& transparentFrustumIndex = registry.Load<std::vector<uint32_t>>("transparentFrustumObjectIndex");
		transparentIndex.assign(transparentFrustumIndex.begin(), transparentFrustumIndex.end());
	}
}

void HZBPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	Frustum& frustum = state.camera.frustum;
	auto& opaqueMeshes = state.objects.sceneStaticRenderData.opaqueItems;

	std::vector<bool> frustumCullResult;
	std::vector<uint32_t> frustumObjectIndex;
	std::vector<AABB> frustumObjectMeshaabbs;
	std::vector<glm::mat4> frustumObjectTransforms;
	std::vector<int> frustumOcclusionCullResult;


	if (!opaqueMeshes.empty())
	{
		frustumCullResult.resize(opaqueMeshes.size(), false);
		frustumObjectMeshaabbs.resize(opaqueMeshes.size());

		std::for_each(std::execution::par_unseq, opaqueMeshes.begin(), opaqueMeshes.end(),
			[&](VKRenderObjectData::SceneStaticRenderData::MeshItem& item)
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

	auto& transparentMeshes = state.objects.sceneStaticRenderData.transparentItems;

	std::vector<uint32_t> transparentFrustumObjectIndex;
	std::vector<AABB> transparentObjectMeshaabbs;

	if (!transparentMeshes.empty())
	{
		std::vector<bool> transparentCullResult(transparentMeshes.size(), false);
		transparentObjectMeshaabbs.resize(transparentMeshes.size());

		std::for_each(std::execution::par_unseq, transparentMeshes.begin(), transparentMeshes.end(),
			[&](VKRenderObjectData::SceneStaticRenderData::MeshItem& item)
			{
				uint32_t meshIndex = &item - transparentMeshes.data();

				AABB aabbworld = item.meshinfo.mesh->GetAABB();
				aabbworld.MakeTransform(item.transform);

				transparentObjectMeshaabbs[meshIndex] = aabbworld;
				transparentCullResult[meshIndex] = !frustum.IsAABBOnFrustum(aabbworld);
			});

		size_t transparentWritePos = 0;
		for (size_t i = 0; i < transparentMeshes.size(); i++)
		{
			if (transparentCullResult[i]) continue;

			if (transparentWritePos != i)
				transparentObjectMeshaabbs[transparentWritePos] = transparentObjectMeshaabbs[i];

			transparentFrustumObjectIndex.push_back(static_cast<uint32_t>(i));
			transparentWritePos++;
		}

		transparentObjectMeshaabbs.resize(transparentWritePos);
	}

	auto commandPtrs = registry.Get<std::vector<IndirectDrawCommand>>("commands");
	auto& commands = *commandPtrs;

	auto indirectManager = IndirectDrawManager::Instance();
	commands.resize(frustumObjectIndex.size());

	indirectManager->WithSharedLock(
		{ IndirectDrawManager::SharedLockType::Mesh },
		[&]()
		{
			std::for_each(std::execution::par_unseq, frustumObjectIndex.begin(), frustumObjectIndex.end(),
				[&](uint32_t& meshIndex)-> void
				{
					size_t inedx = &meshIndex - frustumObjectIndex.data();
					IndirectDrawCommand& command = commands[inedx];

					auto& item = opaqueMeshes[meshIndex];

					IndirectDrawMeta meta;
					if (!indirectManager->GetIndirectDrawMeta_LockFree(*(item.meshinfo.mesh), meta))
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
		});

	registry.Store("frustumCullResult", std::move(frustumCullResult));
	registry.Store("frustumObjectIndex", std::move(frustumObjectIndex));
	registry.Store("frustumObjectMeshaabbs", std::move(frustumObjectMeshaabbs));
	registry.Store("frustumObjectTransforms", std::move(frustumObjectTransforms));
	registry.Store("frustumOcclusionCullResult", std::move(frustumOcclusionCullResult));
	registry.Store("transparentFrustumObjectIndex", std::move(transparentFrustumObjectIndex));
	registry.Store("transparentObjectMeshaabbs", std::move(transparentObjectMeshaabbs));
}

void HZBPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{}

uint32_t HZBPass::GetMaxLevel() const
{
	return _maxLevel;
}

void HZBPass::DrawDepthMap(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, RenderState& state)
{
	auto& frustumObjectTransforms = registry.Load<std::vector<glm::mat4>>("frustumObjectTransforms");
	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");

	GraphicsBindingRecord depthShaderBinding;

	auto commandPtrs = registry.Get<std::vector<IndirectDrawCommand>>("commands");
	auto& commands = *commandPtrs;

	auto transform_ssbo = depthShaderBinding.GetStorageBlock(2);
	transform_ssbo->WriteDataAsync(cmd, frustumObjectTransforms.data(), frustumObjectTransforms.size() * sizeof(glm::mat4));

	auto indirectCommandBuffer = registry.GetIndirectBlock("indirectCommandBuffer");
	indirectCommandBuffer->WriteDataAsync(cmd, commands.data(), commands.size() * sizeof(IndirectDrawCommand));

	transform_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	indirectCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);

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
			.setBuffer(indirectCommandBuffer->GetBuffer()->GetHandle())
			.setOffset(0)
			.setSize(vk::WholeSize)
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead);
		cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eDrawIndirect, bufferBarrier);
	}


	cmd->setDynamicViewport(depthMap->GetWidth(), depthMap->GetHeight());

	depthMap->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	DynamicRenderInfo renderingInfo;
	renderingInfo
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height)
		.AddDepthAttachment(depthMap->GetImageView(vk::ImageAspectFlagBits::eDepth));
	cmd->beginRendering(renderingInfo);

	depthShaderBinding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);

	_depthShader->Bind(cmd, depthShaderBinding);

	auto manager = IndirectDrawManager::Instance();

	cmd->bindVertexBuffers(manager->GetVertexBlock());
	cmd->bindIndexBuffer(manager->GetIndexBlock());
	cmd->drawIndexedIndirect(indirectCommandBuffer, commands.size());

	cmd->endRendering();
}

void HZBPass::DrawHZB(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& depthMap, std::shared_ptr<Texture2D>& HZBMap, std::shared_ptr<Texture2D>& HZBMinMap, RenderState& state)
{
	Texture2D::CopyTextureAsync(cmd, depthMap, HZBMap);
	Texture2D::CopyTextureAsync(cmd, depthMap, HZBMinMap);

	auto width = depthMap->GetWidth();
	auto height = depthMap->GetHeight();

	std::vector<BindingRecord::StorageImageEntry> entrys;
	entrys.reserve(_maxLevel * 2);
	for (uint32_t level = 0; level < _maxLevel; level++)
	{
		entrys.push_back(BindingRecord::StorageImageEntry{ .texture = HZBMap, .aspect = vk::ImageAspectFlagBits::eDepth, .baseLevel = level ,.levelCount = 1 });
		entrys.push_back(BindingRecord::StorageImageEntry{ .texture = HZBMinMap, .aspect = vk::ImageAspectFlagBits::eDepth, .baseLevel = level ,.levelCount = 1 });
	}

	LevelData data;

	ComputeBindingRecord HZBShaderBinding;

	HZBShaderBinding.SetStorageImageArray(entrys, 0);
	_HZBShader->Bind(cmd, HZBShaderBinding);

	for (uint32_t level = 1; level < _maxLevel; level++) {
		uint32_t prevW = std::max(1u, width >> (level - 1));
		uint32_t prevH = std::max(1u, height >> (level - 1));
		uint32_t currW = std::max(1u, width >> level);
		uint32_t currH = std::max(1u, height >> level);

		data.inputLevel = level - 1;
		data.outputLevel = level;

		_HZBShader->SetPushConstants(cmd, &data, sizeof(data));
		cmd->dispatch((currW + GlobalConfig::Global_WorkSize_X - 1) / GlobalConfig::Global_WorkSize_X, (currH + GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_Y, 1);

		if (level < _maxLevel - 1)
		{
			HZBMap->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
			HZBMinMap->Barrier(cmd, nullptr, ImageLayout::BindStage::Compute, ImageLayout::BindUsage::Read);
		}
	}

	//for (uint32_t level = 0; level < _maxLevel; level++)
	//	DrawTexture(HZBMap, std::format("temp/HZBMap{}.png", level), level);
}

void HZBPass::GetOcclusionCulling(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, RenderGraph::FrameDataRegistry& registry, std::shared_ptr<Texture2D>& HZBMap, RenderState& state)
{

	auto& frustumObjectIndex = registry.Load<std::vector<uint32_t>>("frustumObjectIndex");
	auto& frustumObjectMeshaabbs = registry.Load<std::vector<AABB>>("frustumObjectMeshaabbs");
	auto& frustumOcclusionCullResult = registry.Load<std::vector<int>>("frustumOcclusionCullResult");
	auto& transparentFrustumIndex = registry.Load<std::vector<uint32_t>>("transparentFrustumObjectIndex");
	auto& transparentObjectMeshaabbs = registry.Load<std::vector<AABB>>("transparentObjectMeshaabbs");

	const size_t opaqueCount = frustumObjectMeshaabbs.size();
	const size_t transparentCount = transparentObjectMeshaabbs.size();

	std::vector<AABB> allAabbs;
	allAabbs.reserve(opaqueCount + transparentCount);
	allAabbs.insert(allAabbs.end(), frustumObjectMeshaabbs.begin(), frustumObjectMeshaabbs.end());
	allAabbs.insert(allAabbs.end(), transparentObjectMeshaabbs.begin(), transparentObjectMeshaabbs.end());

	std::vector<int> cullResult(allAabbs.size(), 0);
	frustumOcclusionCullResult.assign(allAabbs.size(), 0);

	if (allAabbs.empty())
	{
		state.objects.sceneStaticRenderData.transparentItems_cullRenderIndex.clear();
		return;
	}

	struct alignas(16) OccData {
		glm::mat4 viewProj;
		glm::ivec2 depthMapSize;
		int count;
		int maxLevel;
	};
	OccData data{
		.viewProj = state.camera.projection * state.camera.view,
		.depthMapSize = HZBMap->GetSize(),
		.count = int(allAabbs.size()),
		.maxLevel = int(HZBMap->GetMaxLevel())
	};

	ComputeBindingRecord occlusionCullShaderBinding;
	occlusionCullShaderBinding.SetUniformBlock(state.camera.curUBO, GeneralBindingPoint::Camera_Cur);
	occlusionCullShaderBinding.SetUniformBlock(state.camera.prevUBO, GeneralBindingPoint::Camera_Prev);
	auto aabb_ssbo = occlusionCullShaderBinding.GetStorageBlock(2);
	auto result_ssbo = occlusionCullShaderBinding.GetStorageBlock(3);
	auto occDataBlock = occlusionCullShaderBinding.GetUniformBlock(4);

	if (uint64_t size = allAabbs.size() * sizeof(AABB); aabb_ssbo->GetSize() < size)
		aabb_ssbo->SetSizeAsync(cmd, size * 1.2);
	if (uint64_t size = cullResult.size() * sizeof(int); result_ssbo->GetSize() < size)
		result_ssbo->SetSizeAsync(cmd, size * 1.2);

	aabb_ssbo->WriteDataAsync(cmd, allAabbs.data(), allAabbs.size() * sizeof(AABB));
	occDataBlock->WriteDataAsync(cmd, &data, sizeof(data));

	aabb_ssbo->Barrier(cmd, BufferUsage::TransferWrite);
	occDataBlock->Barrier(cmd, BufferUsage::TransferWrite);

	occlusionCullShaderBinding.SetUniformTexture(HZBMap, vk::ImageAspectFlagBits::eDepth, 5);

	_occlusionCullShader->Bind(cmd, occlusionCullShaderBinding);
	cmd->dispatch((allAabbs.size() + GlobalConfig::Global_WorkSize_X * GlobalConfig::Global_WorkSize_Y - 1) / GlobalConfig::Global_WorkSize_X * GlobalConfig::Global_WorkSize_Y, 1, 1);

	// 内含隐式Submit
	result_ssbo->GetBuffer()->Readback(cmd, cullResult.data(), cullResult.size() * sizeof(int));
	std::copy(cullResult.begin(), cullResult.end(), frustumOcclusionCullResult.begin());

	auto& items = state.objects.sceneStaticRenderData.opaqueItems;
	auto& renderIndex = state.objects.sceneStaticRenderData.opaqueItems_cullRenderIndex;
	auto& oneSideIndex = renderIndex.oneSideIndex;
	auto& twoSideIndex = renderIndex.twoSideIndex;

	for (size_t i = 0; i < opaqueCount; i++)
	{
		if (cullResult[i] > 0) continue;

		auto meshIndex = frustumObjectIndex[i];
		if (items[meshIndex].meshinfo.material->GetTwoSided())
			twoSideIndex.push_back(meshIndex);
		else
			oneSideIndex.push_back(meshIndex);
	}

	auto& transparentIndex = state.objects.sceneStaticRenderData.transparentItems_cullRenderIndex;
	transparentIndex.clear();
	transparentIndex.reserve(transparentCount);

	for (size_t i = 0; i < transparentCount; i++)
	{
		if (cullResult[opaqueCount + i] > 0) continue;
		transparentIndex.push_back(transparentFrustumIndex[i]);
	}
	//std::cout << std::format("frustumObjectSize = {}, renderObjectSize = {}\n", frustumObjectIndex.size(), oneSideIndex.size() + twoSideIndex.size());
}
