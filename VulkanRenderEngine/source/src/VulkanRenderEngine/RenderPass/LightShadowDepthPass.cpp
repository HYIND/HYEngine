#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "VulkanRenderEngine/General/RenderHelp.h"

struct CascadeSplit
{
	float nearPlane;
	float farPlane;
	glm::mat4 projection;
};

static std::vector<CascadeSplit> CalculateCascadeSplit(
	float fov,
	float aspect,
	float nearPlane,
	float farPlane,
	int cascadeCount,
	float lambda = 0.75f)  // lambda控制对数/均匀混合，0.5常用
{
	if (cascadeCount <= 0)
		return {};

	std::vector<CascadeSplit> splits;
	splits.reserve(cascadeCount);

	for (int i = 0; i < cascadeCount; ++i) {
		float t0 = static_cast<float>(i) / cascadeCount;
		float t1 = static_cast<float>(i + 1) / cascadeCount;

		float logN = nearPlane * std::pow(farPlane / nearPlane, t0);
		float logF = nearPlane * std::pow(farPlane / nearPlane, t1);

		float uniN = nearPlane + (farPlane - nearPlane) * t0;
		float uniF = nearPlane + (farPlane - nearPlane) * t1;

		// 混合
		float n = lambda * logN + (1.0f - lambda) * uniN;
		float f = lambda * logF + (1.0f - lambda) * uniF;

		glm::mat4 proj = vkPerspective(glm::radians(fov), aspect, n, f);
		splits.push_back({ n, f,proj });
	}

	return splits;
}

constexpr int batch_max = 16;

struct alignas(16) LightProp
{
	glm::vec3 lightPos;
	float farPlane;
};

LightShadowDepthPass::LightShadowDepthPass(
	const std::string& dirLightShadowStaticMeshVertexShaderPath,
	const std::string& dirLightShadowSkinnedMeshVertexShaderPath,
	const std::string& dirLightShadowFragmentShaderPath,
	const std::string& pointLightShadowStaticMeshVertexShaderPath,
	const std::string& pointLightShadowSkinnedMeshVertexShaderPath,
	const std::string& pointLightShadowFragmentShaderPath
)
	:
	_shouldUpdateTexture(false),
	_lastUpadteTime(0)
{
	_ssbo_LightProps = std::make_shared<StorageBlock>(batch_max * sizeof(LightProp));
	_ssbo_ShadowMatrices = std::make_shared<StorageBlock>(batch_max * sizeof(glm::mat4));

	_oneSideCommandBuffer = std::make_shared<IndirectBufferBlock>();
	_twoSideCommandBuffer = std::make_shared<IndirectBufferBlock>();

	{
		_dirLightShadowDepthStaticMeshShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = dirLightShadowStaticMeshVertexShaderPath;
		config.fragmentPath = dirLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_dirLightShadowDepthStaticMeshShader->Create(config);
	}

	{
		_dirLightShadowDepthSkinnedShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = dirLightShadowSkinnedMeshVertexShaderPath;
		config.fragmentPath = dirLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddAnimationDataBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_dirLightShadowDepthSkinnedShader->Create(config);
	}

	{
		_pointLightShadowDepthStaticMeshShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = pointLightShadowStaticMeshVertexShaderPath;
		config.fragmentPath = pointLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5)
			.AddStorageBuffer(6);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_pointLightShadowDepthStaticMeshShader->Create(config);
	}

	{
		_pointLightShadowDepthSkinnedShader = std::make_shared<GraphicsPipeline>();

		GraphicsPipelineConfig config;
		config.vertexPath = pointLightShadowSkinnedMeshVertexShaderPath;
		config.fragmentPath = pointLightShadowFragmentShaderPath;

		config.AddVertexInputAttributeDescription(Vertex::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(Vertex::GetVertexInputBindingDescription());

		config.SetDepthAttachmentFormat(vk::Format::eD32Sfloat);
		config.SetStencilAttachmentFormat(vk::Format::eUndefined);

		config
			.AddBindlessMaterialTextureBinding()
			.AddAnimationDataBinding()
			.AddStorageBuffer(4)
			.AddStorageBuffer(5)
			.AddStorageBuffer(6);

		config.AddDynamicState(vk::DynamicState::eCullMode);

		if (config.Validate())
			_pointLightShadowDepthSkinnedShader->Create(config);
	}

	_dirLightShadowDepthStaticMeshShader->SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());
	_pointLightShadowDepthStaticMeshShader->SetBindlessMaterialTexture(IndirectDrawManager::Instance()->GetMaterialSSBO(), BindlessTextureManager::Instance());

	_dirLightShadowDepthStaticMeshShader->SetStorageBlock(_ssbo_ShadowMatrices, 4);
	_dirLightShadowDepthSkinnedShader->SetStorageBlock(_ssbo_ShadowMatrices, 4);
	_pointLightShadowDepthStaticMeshShader->SetStorageBlock(_ssbo_ShadowMatrices, 4);
	_pointLightShadowDepthSkinnedShader->SetStorageBlock(_ssbo_ShadowMatrices, 4);

	_pointLightShadowDepthStaticMeshShader->SetStorageBlock(_ssbo_LightProps, 6);
	_pointLightShadowDepthSkinnedShader->SetStorageBlock(_ssbo_LightProps, 6);

	_atlas = std::make_shared<AtlasMap>(2000, 2000, 32768);
}

LightShadowDepthPass::~LightShadowDepthPass()
{}

bool LightShadowDepthPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return _shouldUpdateTexture;
}

void LightShadowDepthPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{
	auto shadowAtlas = ctx.GetPersitent(0);
	if (!shadowAtlas)
		return;

	if (state.lights.dirLightInfos.empty() && state.lights.spotLightInfos.empty() && state.lights.pointLightInfos.empty())
		return;

	_staticMesh_OneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
	_staticMesh_TwoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

	glm::u32vec2 size = glm::max(_atlas->GetSize(), glm::u32vec2(16, 16));
	shadowAtlas->Resize(size.x, size.y);

	_dirLightShadowDepthStaticMeshShader->SetStorageBlock(state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices, 5);
	_pointLightShadowDepthStaticMeshShader->SetStorageBlock(state.indirectCommands.ssbo_StaticMesh_TransformAndMaterialIndices, 5);

	{
		auto cmd = VKCONTEXT->GetCommandBuffer();
		shadowAtlas->TransitionLayout(cmd, nullptr, Texture2D::BindStage::Graphics, Texture2D::BindUsage::Output);
		if (cmd->IsRecording())
			VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
	}

	DynamicRenderInfo renderInfo;
	renderInfo
		.SetRenderArea(shadowAtlas->GetWidth(), shadowAtlas->GetHeight())
		.AddDepthAttachment(shadowAtlas->GetImageView());

	auto semaphore = std::make_shared<VKWrapper::VKTimelineSemaphore>(VKCONTEXT->GetDevice().get());
	uint64_t cmdcount = 0;
	processDirAndSpotLight(semaphore, cmdcount, state, renderInfo);
	processPointLight(semaphore, cmdcount, state, renderInfo);

	if (cmdcount > 0)
		semaphore->Wait(cmdcount);
}

void LightShadowDepthPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	CalculateShadowAtlas(state);
}

void LightShadowDepthPass::FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	if (!_shouldUpdateTexture)
		return;

	if (updateFrameDelta > 1)
	{
		_history.dirLightInfos = std::move(state.lights.dirLightInfos);
		_history.pointLightInfos = std::move(state.lights.pointLightInfos);
		_history.spotLightInfos = std::move(state.lights.spotLightInfos);
	}
}

void LightShadowDepthPass::CalculateShadowAtlas(RenderState& state)
{

	state.lights.shadowAtlas = _atlas;

	bool shouldCalculate = updateFrameDelta <= 0
		|| state.renderRecord.frameIndex % updateFrameDelta == 0
		|| (state.renderRecord.currentRenderMicroTimeStamp - _lastUpadteTime >= 25 * 1000.f);

	if (!shouldCalculate)
	{
		state.lights.dirLightInfos = _history.dirLightInfos;
		state.lights.pointLightInfos = _history.pointLightInfos;
		state.lights.spotLightInfos = _history.spotLightInfos;
		_shouldUpdateTexture = false;
		return;
	}

	_atlas->ReleaseSpace();
	_lastUpadteTime = state.renderRecord.currentRenderMicroTimeStamp;

	auto& dirLightInfos = state.lights.dirLightInfos;
	auto& spotLightInfos = state.lights.spotLightInfos;
	auto& pointLightInfos = state.lights.pointLightInfos;

	for (auto& info : dirLightInfos)
	{
		if (!info || !info->light)continue;
		auto& light = info->light;
		for (int i = 0; i < light->getCascadeLevel(); i++)
		{
			uint32_t id;
			if (_atlas->AllocateSpace(light->getShadowMapWidth(), light->getShadowMapHeight(), id))
				info->cascades.push_back({ id ,0 });
			else
				break;
		}
	}

	for (auto& info : spotLightInfos)
	{
		if (!info || !info->light)continue;
		auto& light = info->light;
		uint32_t id;
		if (_atlas->AllocateSpace(light->getShadowMapWidth(), light->getShadowMapHeight(), id))
		{
			info->atlas = std::make_shared<SpotLightInfo::Atlas>();
			info->atlas->id = id;
		}
	}

	for (auto& info : pointLightInfos)
	{
		if (!info || !info->light)continue;
		auto& light = info->light;
		uint32_t id;

		info->atlas = std::make_shared<PointLightInfo::Atlas>();
		for (int i = 0; i < 6; i++)
		{
			if (_atlas->AllocateSpace(light->getShadowMapWidth(), light->getShadowMapHeight(), info->atlas->ids[i]))
				info->atlas->enable[i] = true;
		}
	}

	_shouldUpdateTexture = true;
}

void LightShadowDepthPass::processDirAndSpotLight(std::shared_ptr<VKWrapper::VKTimelineSemaphore>& semaphore, uint64_t& cmdcount, RenderState& state, DynamicRenderInfo& renderInfo)
{
	auto& dirLightsInfo = state.lights.dirLightInfos;
	auto& spotLightsInfo = state.lights.spotLightInfos;

	std::vector<glm::mat4> shadowMatrices;
	shadowMatrices.reserve(batch_max);
	std::vector<DynamicViewport> viewPorts;
	viewPorts.reserve(batch_max);

	uint32_t count = 0;

	auto render = [&]()->void {
		auto cmd = VKCONTEXT->GetCommandBuffer();
		_ssbo_ShadowMatrices->WriteDataAsync(cmd, shadowMatrices.data(), shadowMatrices.size() * sizeof(glm::mat4));
		_ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		RenderSceneLightShadowPassSceneInstance(
			cmd,
			state,
			_dirLightShadowDepthStaticMeshShader,
			_dirLightShadowDepthSkinnedShader,
			count,
			state.objects.sceneRenderData.opaqueMesh,
			state.objects.sceneRenderData.opaqueSkinnedModel,
			renderInfo,
			viewPorts
		);
		_ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::StorageRead, BufferUsage::TransferWrite);
		count = 0;
		shadowMatrices.clear();
		viewPorts.clear();
		if (cmd->IsRecording())
		{
			CmdSyncSeamphore sync;
			sync.waitSemaphores.push_back({ .semaphore = semaphore, .value = cmdcount++ });
			sync.signalSemaphores.push_back({ .semaphore = semaphore, .value = cmdcount });
			VKCONTEXT->SubmitCommandImmediately(cmd, sync);
		}
		};

	for (auto& info : dirLightsInfo)
	{
		if (!info || !info->light || info->cascades.empty() || !info->light->getCastShadow())
			continue;

		float aspect = fabs(state.camera.projection[1][1] / state.camera.projection[0][0]);
		auto cascadeSplit = CalculateCascadeSplit(state.camera.fov, aspect, state.camera.nearPlane, state.camera.farPlane, info->cascades.size());

		for (int i = 0; i < cascadeSplit.size(); i++)
		{
			info->cascades[i].lightSpaceMatrix = info->light->getLightSpaceMatrixWithFrustumCorners(cascadeSplit[i].projection, state.camera.view, &info->cascades[i].center, &info->cascades[i].radius);
			info->cascades[i].cascadePlaneDistance = cascadeSplit[i].farPlane;
		}

		for (auto& cascade : info->cascades)
		{
			AtlasMap::AtlasRect rect;
			if (!_atlas->GetSpace(cascade.id, rect))
				continue;

			int level = info->light->getCascadeLevel();

			shadowMatrices.push_back(cascade.lightSpaceMatrix);
			viewPorts.push_back({ rect.width, rect.height, rect.x ,rect.y });

			count++;
			if (count >= batch_max)
				render();
		}
	}

	for (auto& info : spotLightsInfo)
	{
		if (!info || !info->light || !info->atlas || !info->light->getCastShadow())
			continue;

		auto& light = info->light;

		if (!state.camera.frustum.IsSphereOnFrustum(light->getPosition(), light->getRadius()))
			continue;

		auto mat = light->getLightSpaceMatrix();
		Frustum lightFrustm(mat);
		if (!state.camera.frustum.IsIntersectsFrustum(lightFrustm))
			continue;

		AtlasMap::AtlasRect rect;
		if (!_atlas->GetSpace(info->atlas->id, rect))
			continue;

		shadowMatrices.push_back(mat);
		viewPorts.push_back({ rect.width, rect.height, rect.x ,rect.y });

		count++;
		if (count >= batch_max)
			render();
	}

	if (count > 0)
		render();
}

void LightShadowDepthPass::processPointLight(std::shared_ptr<VKWrapper::VKTimelineSemaphore>& semaphore, uint64_t& cmdcount, RenderState& state, DynamicRenderInfo& renderInfo)
{
	auto& pointLightsInfo = state.lights.pointLightInfos;

	std::vector<glm::mat4> shadowMatrices;
	shadowMatrices.reserve(batch_max);
	std::vector<DynamicViewport> viewPorts;
	viewPorts.reserve(batch_max);
	std::vector<LightProp> lightProps;
	lightProps.reserve(batch_max);

	uint32_t count = 0;

	auto render = [&]()->void {
		auto cmd = VKCONTEXT->GetCommandBuffer();
		_ssbo_ShadowMatrices->WriteDataAsync(cmd, shadowMatrices.data(), shadowMatrices.size() * sizeof(glm::mat4));
		_ssbo_LightProps->WriteDataAsync(cmd, lightProps.data(), lightProps.size() * sizeof(LightProp));
		_ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		_ssbo_LightProps->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);
		RenderSceneLightShadowPassSceneInstance(
			cmd,
			state,
			_pointLightShadowDepthStaticMeshShader,
			_pointLightShadowDepthSkinnedShader,
			count,
			state.objects.sceneRenderData.opaqueMesh,
			state.objects.sceneRenderData.opaqueSkinnedModel,
			renderInfo,
			viewPorts
		);
		_ssbo_ShadowMatrices->Barrier(cmd, BufferUsage::StorageRead, BufferUsage::TransferWrite);
		_ssbo_LightProps->Barrier(cmd, BufferUsage::StorageRead, BufferUsage::TransferWrite);
		if (cmd->IsRecording())
		{
			CmdSyncSeamphore sync;
			sync.waitSemaphores.push_back({ .semaphore = semaphore, .value = cmdcount++ });
			sync.signalSemaphores.push_back({ .semaphore = semaphore, .value = cmdcount });
			VKCONTEXT->SubmitCommandImmediately(cmd, sync);
		}
		count = 0;
		shadowMatrices.clear();
		viewPorts.clear();
		lightProps.clear();
		};

	for (auto& info : pointLightsInfo)
	{
		if (!info || !info->light || !info->light->getCastShadow())
			continue;

		std::array<DynamicViewport, 6> viewports;
		if (!GetCubeViewPorts(viewports, info, *_atlas))
			continue;

		auto light = info->light;

		if (!state.camera.frustum.IsSphereOnFrustum(light->getPosition(), light->getRadius()))
			continue;

		float shadowMapWidth = light->getShadowMapWidth(),
			shadowMapHeight = light->getShadowMapHeight();

		float aspect = (float)shadowMapWidth / (float)shadowMapHeight;
		float near_plane = 0.1f;
		float far_plane = light->getRadius();
		glm::mat4 shadowProj = vkPerspective(glm::radians(90.0f), aspect, near_plane, far_plane);
		glm::vec3 lightPos = light->getPosition();

		std::vector<glm::mat4> shadowTransforms;
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)));
		shadowTransforms.push_back(shadowProj *
			glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0)));

		for (int face = 0; face < 6; face++)
		{
			Frustum faceFrustm(shadowTransforms[face]);
			if (!state.camera.frustum.IsIntersectsFrustum(faceFrustm))
				continue;

			shadowMatrices.push_back(shadowTransforms[face]);
			lightProps.push_back({ lightPos ,far_plane });
			viewPorts.push_back(viewports[face]);

			count++;
			if (count >= batch_max)
				render();
		}
	}

	if (count > 0)
		render();
}

void LightShadowDepthPass::RenderSceneLightShadowPassSceneInstance(
	std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
	RenderState& state,
	std::shared_ptr<GraphicsPipeline>& shader_StaticMesh,
	std::shared_ptr<GraphicsPipeline>& shader_Skinned,
	uint32_t count,
	std::vector<VKRenderObjectData::SceneRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<VKRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& opaqueSinnedModels,
	DynamicRenderInfo& renderInfo,
	std::vector<DynamicViewport>& viewPorts
)
{
	auto manager = IndirectDrawManager::Instance();

	if (!opaqueMeshes.empty())
	{
		auto& oneSideCommands = _staticMesh_OneSideCommands;
		auto& twoSideCommands = _staticMesh_TwoSideCommands;

		if (!oneSideCommands.empty() || !twoSideCommands.empty())
		{
			auto& shader = shader_StaticMesh;

			shader->Bind(cmd);

			cmd->setDynamicViewports(viewPorts);
			cmd->beginRendering(renderInfo);
			if (renderInfo.depthAttachment->loadOp == vk::AttachmentLoadOp::eClear)
				renderInfo.depthAttachment->loadOp = vk::AttachmentLoadOp::eLoad;

			cmd->bindVertexBuffers(manager->GetVertexBlock());
			cmd->bindIndexBuffer(manager->GetIndexBlock());
			cmd->setCullMode(vk::CullModeFlagBits::eNone);


			if (!oneSideCommands.empty())
			{
				std::for_each(std::execution::par_unseq, oneSideCommands.begin(), oneSideCommands.end(),
					[&](IndirectDrawCommand& command)->void {
						if (command.instanceCount > 0)
							command.instanceCount = count;
					}
				);

				_oneSideCommandBuffer->WriteDataAsync(cmd, oneSideCommands.data(), oneSideCommands.size() * sizeof(IndirectDrawCommand));
				_oneSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);

				cmd->drawIndexedIndirect(_oneSideCommandBuffer, oneSideCommands.size());

				_oneSideCommandBuffer->Barrier(cmd, BufferUsage::IndirectRead, BufferUsage::TransferWrite);
			}

			if (!twoSideCommands.empty())
			{
				std::for_each(std::execution::par_unseq, twoSideCommands.begin(), twoSideCommands.end(),
					[&](IndirectDrawCommand& command)->void {
						if (command.instanceCount > 0)
							command.instanceCount = count;
					}
				);

				_twoSideCommandBuffer->WriteDataAsync(cmd, twoSideCommands.data(), twoSideCommands.size() * sizeof(IndirectDrawCommand));
				_twoSideCommandBuffer->Barrier(cmd, BufferUsage::TransferWrite);

				cmd->drawIndexedIndirect(_twoSideCommandBuffer, twoSideCommands.size());

				_twoSideCommandBuffer->Barrier(cmd, BufferUsage::IndirectRead, BufferUsage::TransferWrite);
			}

			cmd->endRendering();
		}
	}

	//if (!opaqueSinnedModels.empty())
	//{
	//  cmd2 = VKCONTEXT->GetCommandBuffer();
	// 	fence2 = std::make_shared<VKWrapper::VKFence>(VKCONTEXT->GetDevice().get());
	//	auto& shader = shader_Skinned;

	//	shader.Use();
	//	RenderHelp::SetupAnimatorGroupData(shader, {});

	//	glm::mat4 cur_Model = glm::mat4(1.0f);
	//	shader.setMat4("model", cur_Model);

	//	for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
	//	{
	//		auto& model = opaqueSinnedModels[i];

	//		if (cur_Model != model.transform)
	//		{
	//			shader.setMat4("model", model.transform);
	//			cur_Model = model.transform;
	//		}
	//		RenderHelp::SetupAnimatorGroupData(shader, *model.animators);

	//		for (auto& meshinfo : model.models)
	//		{
	//			meshinfo.DrawGeometryInstanced(shader, count);
	//		}
	//	}
	//}
}