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

struct alignas(16) camera_compData
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

enum class RenderMode { Present = 0, Offscreen };

class VulkanRenderer
{
public:
	// 提供CreateInstance扩展，用于创建vk实例，实例用于初始化VulkanRenderer
	static std::shared_ptr<VKCore::VulkanInstance> CreateInstance(const std::vector<std::string>& extensionNames);
	static std::shared_ptr<VKCore::VulkanInstance> CreateInstance(uint32_t extensionCount, const char** extensionNames);

	static std::shared_ptr<VulkanRenderer> CreateForWindow(std::shared_ptr<VKCore::VulkanInstance> instance, VkSurfaceKHR surface, uint32_t width, uint32_t height, bool limitFrameRate = false);					// 创建窗口模式
	static std::shared_ptr<VulkanRenderer> CreateForOffScreen(std::shared_ptr<VKCore::VulkanInstance> instance, uint32_t width, uint32_t height);					// 创建离屏模式

	// 用于分段初始化
	// 仅创建设备、不初始化交换链和渲染目标
	static std::shared_ptr<VulkanRenderer> CreateOnlyDevice(std::shared_ptr<VKCore::VulkanInstance> instance, VkSurfaceKHR surface);
	static std::shared_ptr<VulkanRenderer> CreateOnlyDevice(std::shared_ptr<VKCore::VulkanInstance> instance);

	// 分段初始化
	// 初始化交换链和渲染目标
	static bool CreateForWindow_Target(std::shared_ptr<VulkanRenderer>& renderer, uint32_t width, uint32_t height, bool limitFrameRate = false);
	static bool CreateForOffScreen_Target(std::shared_ptr<VulkanRenderer>& renderer, uint32_t width, uint32_t height);

public:
	VulkanRenderer();
	~VulkanRenderer();

	uint32_t GetWidth() const;
	uint32_t GetHeight() const;
	RenderOption GetOption() const;

	void SetOption(RenderOption option);
	void Resize(uint32_t width, uint32_t height);


	bool PushFrameState(std::shared_ptr<RenderState>& state);

	void WaitImage(std::function<void(std::shared_ptr<Texture2D>)> callback);
	bool FetchImage(std::function<void(std::shared_ptr<Texture2D>)> callback);

	void Run();
	void Stop();
	void ExecuteLoop();

public:
	std::shared_ptr<VKCore::VulkanInstance> GetVulkanInstance() const;
	std::shared_ptr<VKCore::VulkanSurface> GetVulkanSurface() const;
	std::shared_ptr<VKCore::VulkanDevice> GetVulkanDevice() const;
	std::shared_ptr<VKCore::VulkanSwapchain> GetVulkanSwapchain() const;

private:
	void InitForWindow(uint32_t width, uint32_t height);
	void InitForOffSceen(uint32_t width, uint32_t height);
	void Init_Internal();

	void InitRenderTarget();
	void InitRenderGraph();
	void InitSceneRenderGraph();
	void InitFirstPersonRenderGraph();

private:
	struct RenderTargetData {
		std::shared_ptr<Texture2D> sceneColorBuffer, sceneDepthBuffer;
		std::shared_ptr<Texture2D> firstPersonColorBuffer, firstPersonDepthBuffer;
		std::shared_ptr<Texture2D> combinColorBuffer, combinBrightColorBuffer;
		std::shared_ptr<Texture2D> finalColorBuffer;
	};

	struct FrameData {
		std::shared_ptr<RenderGraph::Graph::FilghtSync> sync;
		std::shared_ptr<RenderTargetData> renderTarget;
		std::shared_ptr<RenderState> state;
		std::shared_ptr<ThreadPool::SubmitHandle<void>> handle;
	};

	void DrawOffScreen(std::shared_ptr<FrameData> data);

	void PresentImage(std::shared_ptr<FrameData>& data);

private:
	void SetupRenderState(std::shared_ptr<RenderState>& state);
	void SetupIndirectDrawData(std::shared_ptr<RenderState>& state);

	void FinishRendering(std::shared_ptr<RenderState>& state);

	//void RenderFirstPersonLayer(RenderState& state);

private:
	uint32_t scr_width;
	uint32_t scr_height;

	std::shared_ptr<VKCore::VulkanInstance> _vulkanInstance;
	std::shared_ptr<VKCore::VulkanDevice> _vulkanDevice;

	RenderMode _mode;

	// ForWindow
	std::shared_ptr<VKCore::VulkanSurface> _vulkanSurface;
	std::shared_ptr<VKCore::VulkanSwapchain> _vulkanSwapchain;
	std::vector<std::shared_ptr<VKWrapper::VKSemaphore>> _imageAcquiredSemaphores;
	std::vector<std::shared_ptr<VKWrapper::VKSemaphore>> _renderFinishedSemaphores;


	//// FirstPersonLayer
	//std::unique_ptr<FirstPersonPass> _firstPersonPass;

	CriticalSectionLock _globalMutex;
	std::unique_ptr<CombinPass> _combinPass;
	std::unique_ptr<BloomPass> _globalBloomPass;
	std::unique_ptr<GlobalPostProcessPass> _globalPostProcessPass;

	std::vector<std::shared_ptr<RenderTargetData>> _renderTargets;

	camera_compData _cameraCache;

	struct {
		uint32_t frameIndex = 0;
		float prevEV100 = 1.0f;
		int64_t prevRenderMicroTimeStamp = 0;
	}_record;

	bool needFlipFinalY = false;

	std::unique_ptr<RenderGraph::Graph> _sceneRenderGraph;

	RenderOption _option;


	uint32_t _maxFramesInFlight = 1;  // 最大在途帧数

	std::queue<std::shared_ptr<RenderState>> _candidateFrameStates;
	SpinLock _candidateFrameStatesMutex;

	std::queue<std::shared_ptr<FrameData>> _runningFrames;

	SpinLock _doneFramesMutex;
	std::queue<std::shared_ptr<FrameData>> _doneFrames;

	ThreadPool _frameTaskPool;
	bool _stop = true;
	std::shared_ptr<std::thread> _loopThread;
};