#include "OpenGLRenderEngine/RenderPass/LightShadowDepthPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include <glm\gtc\matrix_transform.hpp>

//#define STB_IMAGE_WRITE_IMPLEMENTATION
//#include "stb/stb_image_write.h"
//void SaveDepthTexture(GLuint textureID, const char* filename)
//{
//	glBindTexture(GL_TEXTURE_2D, textureID);
//
//	// 获取纹理尺寸
//	GLint width, height;
//	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
//	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
//
//	// 分配内存存储深度值（使用 float，因为深度通常是浮点数）
//	std::vector<float> depthData(width * height);
//
//	// 确保只读深度附件
//	//glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depthData.data());
//	//auto error = glGetError();
//	//if (error != GL_NO_ERROR) {
//	//	return;
//	//}
//
//	// 读取深度数据 - 使用 GL_DEPTH_COMPONENT 和 GL_FLOAT
//	{
//		glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depthData.data());
//		//auto error = glGetError();
//		//if (error != GL_NO_ERROR) {
//		//	return;
//		//}
//	}
//
//	// 转换为字节数据用于保存
//	std::vector<unsigned char> byteData(width * height);
//	for (int i = 0; i < width * height; i++) {
//		// 钳制到 [0,1]（深度值应该已经在这个范围）
//		float val = depthData[i];
//		val = val * 255.f;
//		byteData[i] = (unsigned char)(val);
//	}
//
//	// 保存为 PNG（1通道灰度图）
//	stbi_write_png(filename, width, height, 1, byteData.data(), width);
//}

static bool GetCubeViewPorts(GLfloat viewports[6][4], const std::shared_ptr<PointLightInfo>& info, const AtlasMap& atlasShadowMap)
{
	if (!info || !info->atlas)
		return false;

	for (int i = 0; i < 6; i++)
	{
		AtlasMap::AtlasRect rect;
		if (!atlasShadowMap.GetSpace(info->atlas->ids[i], rect))
			return false;

		viewports[i][0] = rect.x;
		viewports[i][1] = rect.y;
		viewports[i][2] = rect.width;
		viewports[i][3] = rect.height;
	}
	return true;
}

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

		glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, n, f);
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
	const std::string& dirLightVertexShaderPath, const std::string& dirLightGeometryShaderPath,
	const std::string& dirLightFragmentShaderPath, const std::string& pointLightVertexShaderPath, const std::string& pointLightGeometryShaderPath, const std::string& pointLightFragmentShaderPath)
	:
	_shouldUpdateTexture(false),
	_Fbo(0),
	_lastUpadteTime(0)
{
	_ssbo_LightProps = std::make_shared<SSBO>(batch_max * sizeof(LightProp));
	_ssbo_ShadowMatrices = std::make_shared<SSBO>(batch_max * sizeof(glm::mat4));
	//_ssbo_StaticMesh_Transforms = std::make_shared<SSBO>();

	useAMDViewportExt = GLEW_AMD_vertex_shader_viewport_index;
	if (useAMDViewportExt)
	{
		_dirLightShadowDepthStaticMeshShader.CompileFromFile("shader/lighting/AMDViewport_Dirlightshadow_StaticMesh.vs", "shader/lighting/AMDViewport_Dirlightshadow.fs");
		_dirLightShadowDepthSkinnedShader.CompileFromFile("shader/lighting/AMDViewport_Dirlightshadow_Skinned.vs", "shader/lighting/AMDViewport_Dirlightshadow.fs");
		_pointLightShadowDepthStaticMeshShader.CompileFromFile("shader/lighting/AMDViewport_Pointlightshadow_StaticMesh.vs", "shader/lighting/AMDViewport_Pointlightshadow.fs");
		_pointLightShadowDepthSkinnedShader.CompileFromFile("shader/lighting/AMDViewport_Pointlightshadow_Skinned.vs", "shader/lighting/AMDViewport_Pointlightshadow.fs");

		_dirLightShadowDepthStaticMeshShader.bindSSBO("ShadowMatrices", _ssbo_ShadowMatrices);
		_dirLightShadowDepthSkinnedShader.bindSSBO("ShadowMatrices", _ssbo_ShadowMatrices);
		_pointLightShadowDepthStaticMeshShader.bindSSBO("ShadowMatrices", _ssbo_ShadowMatrices);
		_pointLightShadowDepthSkinnedShader.bindSSBO("ShadowMatrices", _ssbo_ShadowMatrices);

		_pointLightShadowDepthStaticMeshShader.bindSSBO("LightProps", _ssbo_LightProps);
		_pointLightShadowDepthSkinnedShader.bindSSBO("LightProps", _ssbo_LightProps);
	}
	else
	{
		//_dirLightShadowDepthShader.CompileFromFile(dirLightVertexShaderPath, dirLightGeometryShaderPath, dirLightFragmentShaderPath);
		//_pointLightShadowDepthShader.CompileFromFile(pointLightVertexShaderPath, pointLightGeometryShaderPath, pointLightFragmentShaderPath);
	}
	_atlas = std::make_shared<AtlasMap>(2000, 2000, 32768);

}

LightShadowDepthPass::~LightShadowDepthPass()
{
	if (_Fbo != 0)
		glDeleteFramebuffers(1, &_Fbo);
}

bool LightShadowDepthPass::ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return _shouldUpdateTexture;
}

void LightShadowDepthPass::Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto shadowAtlas = ctx.GetPersitent(0);
	if (!shadowAtlas)
		return;

	if (state.lights.dirLightInfos.empty() && state.lights.spotLightInfos.empty() && state.lights.pointLightInfos.empty())
		return;

	glm::u32vec2 size = glm::max(_atlas->GetSize(), glm::u32vec2(16, 16));
	shadowAtlas->Resize(size.x, size.y);

	_dirLightShadowDepthStaticMeshShader.bindSSBO("Transforms", state.indirectCommands.ssbo_StaticMesh_Transforms);
	_pointLightShadowDepthStaticMeshShader.bindSSBO("Transforms", state.indirectCommands.ssbo_StaticMesh_Transforms);

	if (_Fbo == 0)
	{
		glGenFramebuffers(1, &_Fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, _Fbo);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, _Fbo);
	shadowAtlas->Bind(0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowAtlas->GetID(), 0);
	glClear(GL_DEPTH_BUFFER_BIT);

	GLboolean isCullFaceEnabled = glIsEnabled(GL_CULL_FACE);

	glDisable(GL_CULL_FACE);

	processDirAndSpotLight(state);
	processPointLight(state);

	if (isCullFaceEnabled == GL_TRUE)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);

	auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
	auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;
	std::for_each(oneSideCommands.begin(), oneSideCommands.end(),
		[&](IndirectDrawCommand& command)->void {
			if (command.instanceCount > 0)
				command.instanceCount = 1;
		}
	);
	std::for_each(twoSideCommands.begin(), twoSideCommands.end(),
		[&](IndirectDrawCommand& command)->void {
			if (command.instanceCount > 0)
				command.instanceCount = 1;
		}
	);
}

void LightShadowDepthPass::FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	CalculateShadowAtlas(state);

	//if (_shouldUpdateTexture)
	//{

	//	{
	//		auto& skinnedItems = state.objects.sceneRenderData.opaqueSkinnedModel;
	//	}
	//}
}

void LightShadowDepthPass::FrameEnd(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
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

void LightShadowDepthPass::processDirAndSpotLight(RenderState& state)
{
	auto& dirLightsInfo = state.lights.dirLightInfos;
	auto& spotLightsInfo = state.lights.spotLightInfos;

	std::vector<glm::mat4> shadowMatrices;
	shadowMatrices.reserve(batch_max);

	int count = 0;

	auto render = [&]()->void {
		_ssbo_ShadowMatrices->WriteData(shadowMatrices.data(), shadowMatrices.size() * sizeof(glm::mat4));
		if (useAMDViewportExt)
			RenderSceneLightShadowPassSceneInstance(
				state,
				_dirLightShadowDepthStaticMeshShader,
				_dirLightShadowDepthSkinnedShader,
				count,
				state.objects.sceneRenderData.opaqueMesh,
				state.objects.sceneRenderData.opaqueSkinnedModel);
		else
			RenderSceneLightShadowPassScene(
				state,
				_dirLightShadowDepthStaticMeshShader,
				_dirLightShadowDepthSkinnedShader,
				count,
				state.objects.sceneRenderData.opaqueMesh,
				state.objects.sceneRenderData.opaqueSkinnedModel);
		count = 0;
		shadowMatrices.clear();
		};

	for (auto& info : dirLightsInfo)
	{
		if (!info || !info->light || info->cascades.empty() || !info->light->getCastShadow())
			continue;

		float aspect = state.camera.projection[1][1] / state.camera.projection[0][0];
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

			GLfloat viewport[4];
			viewport[0] = rect.x;
			viewport[1] = rect.y;
			viewport[2] = rect.width;
			viewport[3] = rect.height;
			glViewportArrayv(count, 1, viewport);

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

		GLfloat viewport[4];
		viewport[0] = rect.x;
		viewport[1] = rect.y;
		viewport[2] = rect.width;
		viewport[3] = rect.height;
		glViewportArrayv(count, 1, viewport);

		count++;
		if (count >= batch_max)
			render();
	}

	if (count > 0)
		render();
}

void LightShadowDepthPass::processPointLight(RenderState& state)
{
	auto& pointLightsInfo = state.lights.pointLightInfos;

	std::vector<glm::mat4> shadowMatrices;
	shadowMatrices.reserve(batch_max);

	std::vector<LightProp> lightProps;
	lightProps.reserve(batch_max);

	int count = 0;

	auto render = [&]()->void {
		_ssbo_ShadowMatrices->WriteData(shadowMatrices.data(), shadowMatrices.size() * sizeof(glm::mat4));
		_ssbo_LightProps->WriteData(lightProps.data(), lightProps.size() * sizeof(LightProp));
		if (useAMDViewportExt)
			RenderSceneLightShadowPassSceneInstance(
				state,
				_pointLightShadowDepthStaticMeshShader,
				_pointLightShadowDepthSkinnedShader,
				count,
				state.objects.sceneRenderData.opaqueMesh,
				state.objects.sceneRenderData.opaqueSkinnedModel);
		else
			RenderSceneLightShadowPassScene(
				state,
				_pointLightShadowDepthStaticMeshShader,
				_pointLightShadowDepthSkinnedShader,
				count,
				state.objects.sceneRenderData.opaqueMesh,
				state.objects.sceneRenderData.opaqueSkinnedModel);
		count = 0;
		shadowMatrices.clear();
		lightProps.clear();
		};

	for (auto& info : pointLightsInfo)
	{
		if (!info || !info->light || !info->light->getCastShadow())
			continue;

		GLfloat viewports[6][4];
		if (!GetCubeViewPorts(viewports, info, *_atlas))
			continue;

		auto light = info->light;

		if (!state.camera.frustum.IsSphereOnFrustum(light->getPosition(), light->getRadius()))
			continue;

		float shadowMapWidth = light->getShadowMapWidth(),
			shadowMapHeight = light->getShadowMapHeight();

		GLfloat aspect = (GLfloat)shadowMapWidth / (GLfloat)shadowMapHeight;
		GLfloat near_plane = 0.1f;
		GLfloat far_plane = light->getRadius();
		glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), aspect, near_plane, far_plane);
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

			glViewportArrayv(count, 1, viewports[face]);

			count++;
			if (count >= batch_max)
				render();
		}
	}

	if (count > 0)
		render();
}

void LightShadowDepthPass::RenderSceneLightShadowPassScene(
	RenderState& state,
	Shader& shader_StaticMesh,
	Shader& shader_Skinned,
	GLsizei count,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& opaqueSinnedModels
)
{

	if (!opaqueMeshes.empty())
	{
		auto& shader = shader_StaticMesh;

		shader.Use();
		shader.setInt("count", count);

		glm::mat4 cur_Model = glm::mat4(1.0f);
		shader.setMat4("model", cur_Model);

		for (size_t i = 0; i < opaqueMeshes.size(); i++)
		{
			auto& mesh = opaqueMeshes[i];

			if (cur_Model != mesh.transform)
			{
				shader.setMat4("model", mesh.transform);
				cur_Model = mesh.transform;
			}
			mesh.meshinfo.DrawGeometry(shader);
		}
	}

	if (!opaqueSinnedModels.empty())
	{
		auto& shader = shader_Skinned;

		shader.Use();
		shader.setInt("count", count);
		RenderHelp::SetupAnimatorGroupData(shader, {});

		glm::mat4 cur_Model = glm::mat4(1.0f);
		shader.setMat4("model", cur_Model);

		for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
		{
			auto& model = opaqueSinnedModels[i];

			if (cur_Model != model.transform)
			{
				shader.setMat4("model", model.transform);
				cur_Model = model.transform;
			}
			RenderHelp::SetupAnimatorGroupData(shader, *model.animators);

			for (auto& meshinfo : model.models)
			{
				meshinfo.DrawGeometry(shader);
			}
		}
	}
}

void LightShadowDepthPass::RenderSceneLightShadowPassSceneInstance(
	RenderState& state,
	Shader& shader_StaticMesh,
	Shader& shader_Skinned,
	GLsizei count,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& opaqueSinnedModels
)
{
	if (!opaqueMeshes.empty())
	{
		auto& shader = shader_StaticMesh;

		shader.Use();

		if (state.indirectCommands.indirectVAO == 0 || state.indirectCommands.indirectCommandBuffer == 0)
			return;

		glBindVertexArray(state.indirectCommands.indirectVAO);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, state.indirectCommands.indirectCommandBuffer);

		auto& oneSideCommands = state.indirectCommands.staticMesh_OneSideCommand;
		auto& twoSideCommands = state.indirectCommands.staticMesh_TwoSideCommand;

		if (!oneSideCommands.empty())
		{
			std::for_each(oneSideCommands.begin(), oneSideCommands.end(),
				[&](IndirectDrawCommand& command)->void {
					if (command.instanceCount > 0)
						command.instanceCount = count;
				}
			);

			glBufferData(GL_DRAW_INDIRECT_BUFFER, oneSideCommands.size() * sizeof(IndirectDrawCommand), oneSideCommands.data(), GL_DYNAMIC_DRAW);
			glMultiDrawElementsIndirect(
				GL_TRIANGLES,            // 图元类型
				GL_UNSIGNED_INT,         // 索引类型
				(void*)0,                // 起始偏移
				oneSideCommands.size(),  // 命令数量
				0);                      // 步长（0=连续存储）
		}

		if (!twoSideCommands.empty())
		{
			std::for_each(twoSideCommands.begin(), twoSideCommands.end(),
				[&](IndirectDrawCommand& command)->void {
					if (command.instanceCount > 0)
						command.instanceCount = count;
				}
			);

			glBufferData(GL_DRAW_INDIRECT_BUFFER, twoSideCommands.size() * sizeof(IndirectDrawCommand), twoSideCommands.data(), GL_DYNAMIC_DRAW);
			glMultiDrawElementsIndirect(
				GL_TRIANGLES,            // 图元类型
				GL_UNSIGNED_INT,         // 索引类型
				(void*)0,                // 起始偏移
				twoSideCommands.size(),  // 命令数量
				0);                      // 步长（0=连续存储）
		}
	}

	if (!opaqueSinnedModels.empty())
	{
		auto& shader = shader_Skinned;

		shader.Use();
		RenderHelp::SetupAnimatorGroupData(shader, {});

		glm::mat4 cur_Model = glm::mat4(1.0f);
		shader.setMat4("model", cur_Model);

		for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
		{
			auto& model = opaqueSinnedModels[i];

			if (cur_Model != model.transform)
			{
				shader.setMat4("model", model.transform);
				cur_Model = model.transform;
			}
			RenderHelp::SetupAnimatorGroupData(shader, *model.animators);

			for (auto& meshinfo : model.models)
			{
				meshinfo.DrawGeometryInstanced(shader, count);
			}
		}
	}
}