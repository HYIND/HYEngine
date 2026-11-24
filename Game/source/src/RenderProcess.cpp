#include "RenderProcess.h"
#include "GeneralManager/RenderManager.h"
#include <chrono>
#include <thread>
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
	{
		std::string error;
		if (!bridge->Initialize(*renderer, &error))
			std::cerr << std::format("[ Present ] 出图初始化失败：{}\n", error);
	}

	InitManager.Init(new Task<void>(CoroTask::Run([]()->void {})));

	FPSPrinter fpsprinter;

	while (!shouldStopRenderThread || !(*shouldStopRenderThread))
	{
		auto start = Tool::GetTimestampMircoseconds();

		if (renderer && bridge)
			bridge->PresentFrame();

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
