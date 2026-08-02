#include "RenderProcess.h"
#include <thread>
#include "Scene.h"
#include "RenderEngine/D2DTools.h"
#include "Manager/RenderContextManager.h"

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
			this_thread::sleep_for(5ms);

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
		std::cout << std::format("Fps = {} ms, avg frame cost {} ms\n", Fps, avgCost);

		fpsAccumulator = 0;
		timeMicroAccumulator = 0;
	}

private:
	uint32_t fpsAccumulator = 0;
	uint32_t timeMicroAccumulator = 0;
};

void Render_Thread(
	std::shared_ptr<Render::Renderer> render,
	std::shared_ptr<DynamicFpsController> fpscontroller,
	std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> buffermanager)
{
	render_thread_id = std::this_thread::get_id();

	for (int i = 0; i < 3; i++)
		buffermanager->setInitialValue(i, std::make_shared<Render::RenderFrameData>());
	render->SetBuffers(buffermanager);

	auto rect = RENDERCONTEXMANAGER->GetRECT();
	float width = rect.right - rect.left, height = rect.bottom - rect.top;

	THREADCONTEXT->SetMain(true);
	RENDERCONTEXMANAGER->SetHGLRC(THREADCONTEXT->GetHGLRC());

	{
		auto guard = THREADCONTEXT->GetBindGuard();
		GLenum err = glewInit();
		if (err != GLEW_OK) {
			std::cerr << "glewInit fail!\n";
		}
		render->InitOpenGLRender(width, height);
	}
	InitManager.Init(new Task<void>(CoroTask::Run([]()->void {})));

	CoroTask::Run([]()->void {
		//InitManager.waitDone();
		auto guard = THREADCONTEXT->GetBindGuard();
		ResFactory->InitOpenGLResource();
		}
	);

	FPSPrinter fpsprinter;
	fpscontroller->reset();

	while (true)
	{
		auto start = Tool::GetTimestampMircoseconds();

		STATUS m_status = Get_CurScene();

		bool isrunningworld = (m_status == STATUS::LocalGame_Status || m_status == STATUS::OnlineGame_Status);

		{
			//渲染
			pRenderTarget->BeginDraw();
			pRenderTarget->Clear(ColorF(1, 1, 1, 1));
			pRenderTarget->EndDraw();

			if (isrunningworld && render)
				render->renderFrame(fpscontroller->getTimeDiffMS());

			auto scene = _Scene::CurScene;
			if (scene)
				scene->DrawScene(fpscontroller->getTimeDiffMS());

			HRESULT hr = g_pSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);  // 1 = 垂直同步，0 = 无标志
			if (FAILED(hr)) {}
		}
		int targetfps = isrunningworld ? fpscontroller->getGameTargetFps() : 30;
		fpscontroller->setCurTargetFps(targetfps);

		fpscontroller->run();		// 帧率动态控制

		fpsprinter.PushFrame(Tool::GetTimestampMircoseconds() - start);
		if (fpsprinter.IsTimeAccumulatorTo(500000))
			fpsprinter.ClearAndPrint();
	}

	RENDERCONTEXMANAGER->SetHGLRC(NULL);
}

bool IsRenderThread()
{
	return std::this_thread::get_id() == render_thread_id;
}

void WaitForInitOpenGL()
{
	InitManager.waitDone();
}

bool IsInitOpenGL()
{
	return InitManager.isInitDone();
}
