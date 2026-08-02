#pragma once

#include "stdafx.h"
#include <memory>
#include <vector>
#include "Coroutine.h"
#include "RenderEngine/D2DTools.h"

enum class DialogResult
{
	FAILED = -1,
	MANUEL_CANCEL = 0,
	SUCCESSED = 1
};

class BusyLoaderDialog {
private:
	static constexpr int WIDTH = 300;
	static constexpr int HEIGHT = 200;
	static constexpr int DOT_COUNT = 7;            // 点的数量
	static constexpr int CENTER_X = WIDTH / 2;
	static constexpr int CENTER_Y = HEIGHT / 2 - 20;
	static constexpr int CIRCLE_RADIUS = 40;        // 圆周半径
	static constexpr int DOT_RADIUS = 5;             // 点的大小
	static constexpr int ANIMATION_SPEED = 10;       // 动画更新间隔(ms)
	static constexpr int LOGIC_SPEED = 10;			 // 逻辑更新间隔(ms)
	static constexpr UINT_PTR ANIMATION_TIMER_ID = 1001;// 动画定时器ID
	static constexpr UINT_PTR LOGIC_TIMER_ID = 1002;	// 逻辑定时器ID
	static constexpr BYTE WINDOW_ALPHA = 215;			// 窗口透明度

	struct Dot {
		float progress;      // 0.0 到 1.0 的进度
		float speed;         // 当前速度
		float angle;         // 当前角度
		bool active;         // 是否激活
	};

	HWND hWnd;
	HWND hParentWnd;
	HINSTANCE hInst;
	std::vector<Dot> dots;
	float globalTime;        // 全局时间
	float speed;             // 当前基础速度
	bool isRunning;

	std::wstring m_message;

	DialogResult m_result;						    // 任务结果
	Task<bool> m_coroTask;							// 异步任务
	std::function<void(bool)> m_onCompleteCallback; // 完成回调

	ID2D1HwndRenderTarget* m_renderTarget = nullptr;
	ID2D1SolidColorBrush* m_Text_Brush = nullptr;
	ID2D1SolidColorBrush* m_Dot_Brush = nullptr;
	IDWriteTextFormat* m_Text_Format = nullptr;

	bool showAmination = true;

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void DrawAnimation();
	void OnTimer();
	void Cleanup();
	float CalculateEasing(float t);

public:
	BusyLoaderDialog();
	~BusyLoaderDialog();

	bool Create(HWND parent, HINSTANCE hInstance);
	void Show(Task<bool>&& asyncTask,
		std::function<void(bool success)> complete_callback,
		const std::string& message);

	DialogResult WaitResult();
	DialogResult WaitResultAndClose();

	void Close();
};