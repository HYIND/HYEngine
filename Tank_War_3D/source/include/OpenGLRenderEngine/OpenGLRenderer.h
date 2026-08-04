#pragma once

#include "OpenGLRenderEngine/Base/Camera.h"
#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Model.h"
#include "OpenGLRenderEngine/Base/Texture2D.h"
#include "OpenGLRenderEngine/Base/Animator.h"
#include "OpenGLRenderEngine/Base/AtlasMap.h"

#include "OpenGLRenderEngine/RenderPass/BloomPass.h"
#include "OpenGLRenderEngine/RenderPass/FirstPersonPass.h"
#include "OpenGLRenderEngine/RenderPass/CombinPass.h"
#include "OpenGLRenderEngine/RenderPass/GlobalPostProcessPass.h"

#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "OpenGLRenderEngine/RenderGraph/RenderGraph.h"

#include "RenderEngine/SharedTexture.h"

struct alignas(16) comp_camera
{
	alignas(16) glm::mat4 projection;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 projView;
	alignas(16) glm::mat4 invView;
	alignas(16) glm::vec3 invTransViewRow1;
	alignas(16) glm::vec3 invTransViewRow2;
	alignas(16) glm::vec3 invTransViewRow3;
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 directionUp;
	alignas(16) glm::vec3 directionRight;
	float nearPlane = 0.1f;
	float farPlane = 1000.f;
	float fov = 80.f;
};

class OpenGLRenderer
{
public:
	OpenGLRenderer();
	~OpenGLRenderer();

	void Init(int width, int height, SharedTexture* sharedTexture = nullptr);
	void Draw(RenderState& state);

	GLuint GetColorBuffer() const;
	int GetWidth() const;
	int GetHeight() const;

private:
	void InitRenderGraph();
	void InitSceneRenderGraph();
	void InitFirstPersonRenderGraph();

	void UpdateRenderState(RenderState& state);
	void FinishRendering(RenderState& state);

	void RenderFirstPersonLayer(RenderState& state);

private:
	int scr_width;
	int scr_height;

	// FirstPersonLayer
	std::unique_ptr<FirstPersonPass> _firstPersonPass;

	//combin
	std::unique_ptr<CombinPass> _combinPass;

	// GlobalPostProcess
	std::unique_ptr<BloomPass> _globalBloomPass;
	std::unique_ptr<GlobalPostProcessPass> _globalPostProcessPass;

	struct {
		GLuint sceneFbo;
		std::shared_ptr<Texture2D> sceneColorBuffer, sceneDepthBuffer;

		GLuint firstPersonFbo;
		std::shared_ptr<Texture2D> firstPersonColorBuffer, firstPersonDepthBuffer;

		GLuint combinFbo;
		std::shared_ptr<Texture2D> combinColorBuffer, combinBrightColorBuffer, combinDepthBuffer;

		GLuint finalFbo, finalColorBuffer;
	}_renderTarget;


	struct {
		GLuint curUBO;
		GLuint prevUBO;
		comp_camera data;
	}_cameraCache;


	struct {
		int frameIndex = 0;
		float prevEV100 = 1.0f;
		int64_t prevRenderMicroTimeStamp = 0;
	}_record;

	bool needFlipFinalFboY = false;

	std::unique_ptr<OpenGLRenderGraph::RenderGraph> _sceneRenderGraph;
	std::unique_ptr<OpenGLRenderGraph::RenderGraph> _firstPersonRenderGraph;
};