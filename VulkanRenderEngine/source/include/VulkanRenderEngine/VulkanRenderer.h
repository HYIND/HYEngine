#pragma once
#include "vkstdafx.h"

#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/RenderGraph/RenderGraph.h"

//#include "VulkanRenderEngine/RenderPass/FirstPersonPass.h"
#include "VulkanRenderEngine/RenderPass/BloomPass.h"
#include "VulkanRenderEngine/RenderPass/CombinPass.h"
#include "VulkanRenderEngine/RenderPass/GlobalPostProcessPass.h"

#include "VulkanRenderEngine/SharedTexture.h"

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/VKCore/CoreGeneral.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"

struct alignas(16) comp_camera
{
	alignas(16) glm::mat4 projection;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 projView;
	alignas(16) glm::mat4 invProjection;
	alignas(16) glm::mat4 invView;
	alignas(16) glm::mat4 invProjView;
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

enum class RenderMode { Present = 0, SharedTexture, Offscreen };

class VulkanRenderer
{
public:
	// 提供CreateInstance扩展，用于创建vk实例，实例用于初始化VulkanRenderer
	static std::shared_ptr<VKCore::VulkanInstance> CreateInstance(const std::vector<std::string>& extensionNames);
	static std::shared_ptr<VKCore::VulkanInstance> CreateInstance(uint32_t extensionCount, const char** extensionNames);

	static std::shared_ptr<VulkanRenderer> CreateForWindow(std::shared_ptr<VKCore::VulkanInstance> instance, VkSurfaceKHR surface, uint32_t width, uint32_t height, bool limitFrameRate = false);					// 创建窗口模式
	static std::shared_ptr<VulkanRenderer> CreateForSharedTexture(std::shared_ptr<VKCore::VulkanInstance> instance, std::shared_ptr<SharedTexture> sharedTexture);		// 创建共享纹理模式
	static std::shared_ptr<VulkanRenderer> CreateForOffScreen(std::shared_ptr<VKCore::VulkanInstance> instance, uint32_t width, uint32_t height);					// 创建离屏模式

public:
	VulkanRenderer();
	~VulkanRenderer();

	void Draw(RenderState& state);

	int GetWidth() const;
	int GetHeight() const;
	std::shared_ptr<Texture2D> GetColorBuffer() const;
	RenderOption GetOption() const;

	void SetOption(RenderOption option);
	void Resize(uint32_t width, uint32_t height);
	void EarlyProcess(RenderState& state);

public:
	std::shared_ptr<VKCore::VulkanInstance> GetVulkanInstance() const;
	std::shared_ptr<VKCore::VulkanSurface> GetVulkanSurface() const;
	std::shared_ptr<VKCore::VulkanDevice> GetVulkanDevice() const;
	std::shared_ptr<VKCore::VulkanSwapchain> GetVulkanSwapchain() const;

private:
	void SetupRenderState(RenderState& state);
	void SetupIndirectDrawData(RenderState& state);

	void FinishRendering(RenderState& state);

	//void RenderFirstPersonLayer(RenderState& state);

private:
	void InitForWindow(uint32_t width, uint32_t height);
	void InitForSharedTexture(std::shared_ptr<SharedTexture> sharedTexture);
	void InitForOffSceen(uint32_t width, uint32_t height);
	void Init_Internal();

	void InitRenderTarget();
	void InitRenderGraph();
	void InitSceneRenderGraph();
	void InitFirstPersonRenderGraph();

private:
	void Draw_Internal(RenderState& state);
	void DrawPresent(RenderState& state);
	void DrawSharedTexture(RenderState& state);
	void DrawOffScreen(RenderState& state);

private:
	uint32_t scr_width = 1;
	uint32_t scr_height = 1;

	std::shared_ptr<VKCore::VulkanInstance> _vulkanInstance;
	std::shared_ptr<VKCore::VulkanDevice> _vulkanDevice;
	std::shared_ptr<VKWrapper::VKFence> _fence;

	RenderMode _mode;

	// ForWindow
	std::shared_ptr<VKCore::VulkanSurface> _vulkanSurface;
	std::shared_ptr<VKCore::VulkanSwapchain> _vulkanSwapchain;
	std::vector<std::shared_ptr<Texture2D>> _depthImages;
	std::vector<std::shared_ptr<VKWrapper::VKSemaphore>> _imageAcquiredSemaphores;
	std::vector<std::shared_ptr<VKWrapper::VKSemaphore>> _renderFinishedSemaphores;

	// ForSharedTexture


	//// FirstPersonLayer
	//std::unique_ptr<FirstPersonPass> _firstPersonPass;

	//combin
	std::unique_ptr<CombinPass> _combinPass;

	// GlobalPostProcess
	std::unique_ptr<BloomPass> _globalBloomPass;
	std::unique_ptr<GlobalPostProcessPass> _globalPostProcessPass;

	struct {
		std::shared_ptr<Texture2D> sceneColorBuffer, sceneDepthBuffer;
		std::shared_ptr<Texture2D> firstPersonColorBuffer, firstPersonDepthBuffer;
		std::shared_ptr<Texture2D> combinColorBuffer, combinBrightColorBuffer;
		std::shared_ptr<Texture2D> finalColorBuffer;

		//SharedTexture* sharedTexture = nullptr;
	}_renderTarget;


	struct {
		std::shared_ptr<UniformBlock> curUBO;
		std::shared_ptr<UniformBlock> prevUBO;
		comp_camera data;
	}_cameraCache;


	struct {
		uint32_t frameIndex = 0;
		float prevEV100 = 1.0f;
		int64_t prevRenderMicroTimeStamp = 0;
	}_record;

	bool needFlipFinalY = false;

	std::unique_ptr<RenderGraph::Graph> _sceneRenderGraph;
	std::unique_ptr<RenderGraph::Graph> _firstPersonRenderGraph;

	RenderOption _option;
};