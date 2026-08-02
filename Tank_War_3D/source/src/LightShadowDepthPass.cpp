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

LightShadowDepthPass::LightShadowDepthPass(
	const std::string& dirLightVertexShaderPath, const std::string& dirLightGeometryShaderPath,
	const std::string& dirLightFragmentShaderPath, const std::string& pointLightVertexShaderPath, const std::string& pointLightGeometryShaderPath, const std::string& pointLightFragmentShaderPath)
	:
	_shouldUpdateTexture(false),
	_Fbo(0),
	_lastUpadteTime(0)
{
	useAMDViewportExt = GLEW_AMD_vertex_shader_viewport_index;
	if (useAMDViewportExt)
	{
		_dirLightShadowDepthShader.CompileFromFile("shader/lighting/AMDViewport_Dirlightshadow.vs", "shader/lighting/AMDViewport_Dirlightshadow.fs");
		_pointLightShadowDepthShader.CompileFromFile("shader/lighting/AMDViewport_Pointlightshadow.vs", "shader/lighting/AMDViewport_Pointlightshadow.fs");
	}
	else
	{
		_dirLightShadowDepthShader.CompileFromFile(dirLightVertexShaderPath, dirLightGeometryShaderPath, dirLightFragmentShaderPath);
		_pointLightShadowDepthShader.CompileFromFile(pointLightVertexShaderPath, pointLightGeometryShaderPath, pointLightFragmentShaderPath);
	}
	_atlas = std::make_shared<AtlasMap>(1024, 1024, 32768);
}

LightShadowDepthPass::~LightShadowDepthPass()
{
	if (_Fbo != 0)
		glDeleteFramebuffers(1, &_Fbo);
}

bool LightShadowDepthPass::ShouldExecute(RenderState& state) const
{
	return _shouldUpdateTexture;
}

void LightShadowDepthPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto shadowAtlas = ctx.GetPersitent(0);
	if (!shadowAtlas)
		return;

	glm::u32vec2 size = glm::max(_atlas->GetSize(), glm::u32vec2(16, 16));
	shadowAtlas->Resize(size.x, size.y);

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

	glDisable(GL_CULL_FACE);

	processDirAndSpotLight(state);
	processPointLight(state);

	glEnable(GL_CULL_FACE);
}

void LightShadowDepthPass::FrameBegin(RenderState& state)
{
	CalculateShadowAtlas(state);
}

void LightShadowDepthPass::FrameEnd(RenderState& state)
{
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
	auto& renderItems = state.objects.sceneItems;

	_dirLightShadowDepthShader.Use();
	int count = 0;
	for (auto& info : dirLightsInfo)
	{
		if (!info || !info->light || info->cascades.empty())
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

			std::string name = std::format("shadowMatrices[{}]", count);
			_dirLightShadowDepthShader.setMat4(name, cascade.lightSpaceMatrix);

			GLfloat viewport[4];
			viewport[0] = rect.x;
			viewport[1] = rect.y;
			viewport[2] = rect.width;
			viewport[3] = rect.height;
			glViewportArrayv(count, 1, viewport);

			count++;
			if (count >= batch_max)
			{
				_dirLightShadowDepthShader.setInt("count", count);
				if (useAMDViewportExt)
					RenderHelp::renderLightShadowPassSceneInstance(state, _dirLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
				else
					RenderHelp::renderLightShadowPassScene(state, _dirLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
				count = 0;
			}
		}
	}

	for (auto& info : spotLightsInfo)
	{
		if (!info || !info->light || !info->atlas)
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

		std::string name = std::format("shadowMatrices[{}]", count);
		_dirLightShadowDepthShader.setMat4(name, mat);

		GLfloat viewport[4];
		viewport[0] = rect.x;
		viewport[1] = rect.y;
		viewport[2] = rect.width;
		viewport[3] = rect.height;
		glViewportArrayv(count, 1, viewport);

		count++;
		if (count >= batch_max)
		{
			_dirLightShadowDepthShader.setInt("count", count);
			if (useAMDViewportExt)
				RenderHelp::renderLightShadowPassSceneInstance(state, _dirLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
			else
				RenderHelp::renderLightShadowPassScene(state, _dirLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
			count = 0;
		}
	}

	if (count > 0)
	{
		_dirLightShadowDepthShader.setInt("count", count);
		if (useAMDViewportExt)
			RenderHelp::renderLightShadowPassSceneInstance(state, _dirLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
		else
			RenderHelp::renderLightShadowPassScene(state, _dirLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
	}
}

void LightShadowDepthPass::processPointLight(RenderState& state)
{
	auto& pointLightsInfo = state.lights.pointLightInfos;
	auto& renderItems = state.objects.sceneItems;

	_pointLightShadowDepthShader.Use();
	int count = 0;
	std::vector<glm::mat4> viewProjs;
	viewProjs.reserve(16);
	for (auto& info : pointLightsInfo)
	{
		if (!info || !info->light)
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

			std::string name = std::format("shadowMatrices[{}]", count);
			_pointLightShadowDepthShader.setMat4(name, shadowTransforms[face]);
			_pointLightShadowDepthShader.setVec3(std::format("lightPos[{}]", count), lightPos);
			_pointLightShadowDepthShader.setFloat(std::format("farPlane[{}]", count), far_plane);
			viewProjs.push_back(shadowTransforms[face]);

			glViewportArrayv(count, 1, viewports[face]);

			count++;
			if (count >= batch_max)
			{
				_pointLightShadowDepthShader.setInt("count", count);
				if (useAMDViewportExt)
					RenderHelp::renderLightShadowPassSceneInstance(state, _pointLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
				else
					RenderHelp::renderLightShadowPassScene(state, _pointLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
				count = 0;
				viewProjs.clear();
			}
		}
	}

	if (count > 0)
	{
		_pointLightShadowDepthShader.setInt("count", count);
		if (useAMDViewportExt)
			RenderHelp::renderLightShadowPassSceneInstance(state, _pointLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
		else
			RenderHelp::renderLightShadowPassScene(state, _pointLightShadowDepthShader, count, renderItems, state.objectsGroupMapper.sceneItemsGroupMapper);
	}
}