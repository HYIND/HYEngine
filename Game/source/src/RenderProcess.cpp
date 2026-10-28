#include "RenderProcess.h"
#include "GeneralManager/RenderManager.h"
#include <chrono>
#include <thread>
#include "RenderEngine/D2DTools.h"
#include "GameEngine.h"

std::thread::id render_thread_id;

class InitRenderManager
{
public:
	void Init(Task<void>* task)
	{
		StartInit = true;
		this->task = task;
	}
	bool isInitDone()
	{
		if (InitDone)
			return true;

		if (!StartInit)
			return false;

		if (task->is_done())
		{
			InitDone = true;
			return true;
		}
		return false;
	}
	void waitDone()
	{
		if (InitDone)
			return;
		while (!task)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));

		task->sync_wait();
		InitDone = true;
		return;
	}
private:
	Task<void>* task = nullptr;
	bool StartInit = false;
	bool InitDone = false;
};

InitRenderManager InitManager;

class FPSPrinter
{
public:
	void PushFrame(uint32_t micro_delta)
	{
		fpsAccumulator++;
		timeMicroAccumulator += micro_delta;
	}
	bool IsTimeAccumulatorTo(uint32_t toMicroTime)
	{
		return timeMicroAccumulator >= toMicroTime;
	}
	void ClearAndPrint()
	{
		if (fpsAccumulator == 0 || timeMicroAccumulator == 0)
			return;

		float avgCost = float(timeMicroAccumulator) / float(fpsAccumulator) / 1000.f;
		float Fps = 1000.f / avgCost;
		std::cout << std::format("Fps = {} , avg frame cost {} ms\n", Fps, avgCost);

		fpsAccumulator = 0;
		timeMicroAccumulator = 0;
	}

private:
	uint32_t fpsAccumulator = 0;
	uint32_t timeMicroAccumulator = 0;
};

void RenderLoop(bool* shouldStopRenderThread)
{
	render_thread_id = std::this_thread::get_id();

	auto bridge = RenderManager::Instance()->getVulkanBridge();
	auto* renderer = GameEngine::Instance().Runtime().GetRenderer();
	if (bridge && renderer)
		bridge->Initialize(*renderer);

	InitManager.Init(new Task<void>(CoroTask::Run([]()->void {})));

	FPSPrinter fpsprinter;

	auto lastFrameTime = Tool::GetTimestampMircoseconds();

	while (!shouldStopRenderThread || !(*shouldStopRenderThread))
	{
		auto start = Tool::GetTimestampMircoseconds();
		const float deltaMS = static_cast<float>(start - lastFrameTime) / 1000.f;
		lastFrameTime = start;

		{
			auto* renderer2D = GameEngine::Instance().Runtime().GetRenderer2D();
			if (renderer2D)
				renderer2D->Clear(1.f, 1.f, 1.f, 1.f);

			if (renderer)
			{
				if (bridge)
					bridge->PresentFrame();

				if (renderer2D)
					renderer2D->RenderFrame();
			}

			if (renderer2D)
				renderer2D->Present();
			else if (g_pSwapChain)
				g_pSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
		}

		fpsprinter.PushFrame(Tool::GetTimestampMircoseconds() - start);
		if (fpsprinter.IsTimeAccumulatorTo(500000))
			fpsprinter.ClearAndPrint();
	}

}

bool IsRenderThread()
{
	return std::this_thread::get_id() == render_thread_id;
}

void WaitForInitScene3D()
{
	InitManager.waitDone();
}

bool IsInitScene3D()
{
	return InitManager.isInitDone();
}
