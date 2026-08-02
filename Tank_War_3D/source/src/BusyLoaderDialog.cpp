#include "BusyLoaderDialog.h"
#include <string>
#define _USE_MATH_DEFINES
#include <math.h>

using namespace D2D1;

BusyLoaderDialog::BusyLoaderDialog() :hWnd(nullptr), hParentWnd(nullptr),
hInst(nullptr), globalTime(0.0f),
speed(0.5f), isRunning(false),
m_result(DialogResult::FAILED)
{
	dots.resize(DOT_COUNT);
	for (int i = 0; i < DOT_COUNT; i++) {
		dots[i].progress = 0.0f;
		dots[i].speed = 0.0f;
		dots[i].angle = -M_PI / 2;
		dots[i].active = true;
	}
}

BusyLoaderDialog::~BusyLoaderDialog() {
	Cleanup();
	Close();
}

void BusyLoaderDialog::Cleanup() {
	if (hWnd) {
		if (isRunning) {
			KillTimer(hWnd, ANIMATION_TIMER_ID);
			KillTimer(hWnd, LOGIC_TIMER_ID);
			isRunning = false;
		}
	}
}

bool BusyLoaderDialog::Create(HWND parent, HINSTANCE hInstance) {
	hParentWnd = parent;
	hInst = hInstance;

	// 注册窗口类
	const wchar_t CLASS_NAME[] = L"BusyLoaderDialogClass";

	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.style = CS_HREDRAW | CS_VREDRAW;

	RegisterClass(&wc);

	// 计算窗口居中位置
	RECT parentRect;
	GetWindowRect(parent, &parentRect);
	int x = parentRect.left + (parentRect.right - parentRect.left - WIDTH) / 2;
	int y = parentRect.top + (parentRect.bottom - parentRect.top - HEIGHT) / 2;

	// 创建窗口
	hWnd = CreateWindowEx(
		WS_EX_TOOLWINDOW | WS_EX_LAYERED,
		CLASS_NAME,
		L"加载中...",
		WS_POPUP,
		x, y, WIDTH, HEIGHT,
		parent,
		nullptr,
		hInstance,
		this
	);

	if (!hWnd) return false;

	HRGN hRgn = CreateRoundRectRgn(0, 0, WIDTH, HEIGHT, 30, 30);
	SetWindowRgn(hWnd, hRgn, TRUE);
	DeleteObject(hRgn);

	// 设置窗口透明度
	SetLayeredWindowAttributes(hWnd, 0, WINDOW_ALPHA, LWA_ALPHA);

	return true;
}

void BusyLoaderDialog::Show(
	Task<bool>&& asyncTask,
	std::function<void(bool success)> complete_callback,
	const std::string& message)
{
	if (!hWnd) return;

	isRunning = true;
	globalTime = 0.0f;

	// 重置所有点的状态
	for (int i = 0; i < DOT_COUNT; i++) {
		dots[i].progress = 0.0f;
		dots[i].speed = 0.0f;
		dots[i].angle = -M_PI / 2;
		dots[i].active = true;
	}

	m_message = Tool::UTF8ToWString(message);
	m_result = DialogResult::FAILED;
	m_coroTask = std::move(asyncTask);
	m_onCompleteCallback = complete_callback;

	SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
	showAmination = false;
	CoroTask::Run([&]()->Task<void>
		{
			co_await CoSleep(std::chrono::milliseconds(200));
			// 设置定时器，用于动画更新
			showAmination = true;
			SetTimer(hWnd, ANIMATION_TIMER_ID, ANIMATION_SPEED, nullptr);
			SetTimer(hWnd, LOGIC_TIMER_ID, LOGIC_SPEED, nullptr);
			if (m_coroTask && !m_coroTask.is_done())
				SetLayeredWindowAttributes(hWnd, 0, WINDOW_ALPHA, LWA_ALPHA);
		});

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);

	// 禁用父窗口
	EnableWindow(hParentWnd, FALSE);

	// 进入模态消息循环
	MSG msg;
	while (isRunning && GetMessage(&msg, nullptr, 0, 0)) {
		if (!IsDialogMessage(hWnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

DialogResult BusyLoaderDialog::WaitResult()
{
	if (m_result != DialogResult::MANUEL_CANCEL)
	{
		bool result = m_coroTask ? m_coroTask.sync_wait() : false;
		if (m_result != DialogResult::MANUEL_CANCEL)
			m_result = result ? DialogResult::SUCCESSED : DialogResult::FAILED;
	}
	if (m_onCompleteCallback && m_result != DialogResult::MANUEL_CANCEL)
		std::invoke(m_onCompleteCallback, m_result == DialogResult::SUCCESSED ? true : false);
	return m_result;
}

DialogResult BusyLoaderDialog::WaitResultAndClose()
{
	auto result = WaitResult();
	Close();
	return result;
}

void BusyLoaderDialog::Close() {
	if (hWnd && isRunning) {
		isRunning = false;
		KillTimer(hWnd, ANIMATION_TIMER_ID);
		KillTimer(hWnd, LOGIC_TIMER_ID);

		SafeRelease(m_Text_Format);
		SafeRelease(m_Dot_Brush);
		SafeRelease(m_Text_Brush);
		SafeRelease(m_renderTarget);

		DestroyWindow(hWnd);
		hWnd = nullptr;

		EnableWindow(hParentWnd, TRUE);   // 重新启用父窗口
		SetForegroundWindow(hParentWnd);  // 将焦点返回父窗口
	}
}

float BusyLoaderDialog::CalculateEasing(float t) {
	// 使用缓动函数实现加减速效果
	// 这里使用 easeInOutQuad 函数：先加速后减速
	if (t < 0.5f) {
		return 2.0f * t * t;
	}
	else {
		return 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
	}
}

void BusyLoaderDialog::OnTimer() {
	globalTime += 0.02f;  // 时间增量

	// 更新每个点的进度
	for (int i = 0; i < DOT_COUNT; i++) {
		// 每个点有固定的延迟
		float delay = i * 0.1f;  // 点之间的延迟

		// 计算当前点的归一化时间 (0 到 1)
		float t = globalTime - delay;

		// 当时间在 0 到 1 之间时，点处于运动状态
		if (t >= 0.0f && t <= 1.0f) {
			dots[i].active = true;

			// 使用缓动函数计算进度
			float easedT = CalculateEasing(t);

			// 进度从 0 到 1，对应角度从 0 到 2PI
			dots[i].progress = easedT;
			dots[i].angle = -M_PI / 2 + easedT * 2.0f * M_PI;
		}
		// 时间超过 1，点完成一圈，重置到起点重新开始
		else if (t > 1.0f) {
			dots[i].progress = 0.0f;
			dots[i].angle = -M_PI / 2;
			dots[i].active = true;
		}
		// 时间还没到延迟，点还没开始运动
		else {
			dots[i].active = false;
		}
	}

	// 重置全局时间，防止溢出
	if (globalTime > (DOT_COUNT * 0.1f + 1.0f)) {
		globalTime = 0.0f;
	}

	// 触发重绘
	InvalidateRect(hWnd, nullptr, TRUE);
}

void BusyLoaderDialog::DrawAnimation() {

	if (!m_renderTarget)
	{
		RECT rect;
		if (GetClientRect(hWnd, &rect))
		{
			pD2DFactory->CreateHwndRenderTarget(
				RenderTargetProperties(),
				HwndRenderTargetProperties(hWnd, SizeU(rect.right - rect.left, rect.bottom - rect.top)),
				&m_renderTarget
			);
		}
	}

	if (m_renderTarget)
	{
		m_renderTarget->BeginDraw();
		m_renderTarget->Clear(ColorF(50.f / 255.f, 50.f / 255.f, 50.f / 255.f, 1.f));

		if (!m_Text_Brush)
			m_renderTarget->CreateSolidColorBrush(ColorF(1, 1, 1, 1), &m_Text_Brush);

		if (!m_Text_Format)
		{
			pIDWriteFactory->CreateTextFormat(
				L"SimSun",                   // Font family name
				NULL,                          // Font collection(NULL sets it to the system font collection)
				DWRITE_FONT_WEIGHT_REGULAR,    // Weight
				DWRITE_FONT_STYLE_NORMAL,      // Style
				DWRITE_FONT_STRETCH_NORMAL,    // Stretch
				20.0f,                         // Size    
				L"zh-cn",                      // Local
				&m_Text_Format                // Pointer to recieve the created object
			);
			m_Text_Format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			m_Text_Format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}

		if (m_renderTarget || m_Text_Format)
		{
			D2D1_RECT_F targetRect = D2D1::RectF(
				0,
				HEIGHT - 60,
				WIDTH,
				HEIGHT - 20
			);

			m_renderTarget->DrawText(
				m_message.c_str(),
				wcslen(m_message.c_str()),
				m_Text_Format,
				targetRect,
				m_Text_Brush
			);
		}

		if (!m_Dot_Brush)
			m_renderTarget->CreateSolidColorBrush(ColorF(1, 1, 1, 1), &m_Dot_Brush);

		if (m_Dot_Brush)
		{
			// 绘制运动的小点 - 使用亮色
			for (int i = 0; i < DOT_COUNT; i++) {
				if (!dots[i].active) continue;

				// 计算位置
				int x = CENTER_X + static_cast<int>(CIRCLE_RADIUS * cos(dots[i].angle));
				int y = CENTER_Y + static_cast<int>(CIRCLE_RADIUS * sin(dots[i].angle));

				int currentRadius = static_cast<int>(DOT_RADIUS);
				if (currentRadius < 3)
					currentRadius = 3;  // 最小半径增加到3

				m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), currentRadius, currentRadius), m_Dot_Brush);
			}
		}

		m_renderTarget->EndDraw();
	}
}

LRESULT CALLBACK BusyLoaderDialog::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	BusyLoaderDialog* pDialog = nullptr;

	if (uMsg == WM_CREATE) {
		CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
		pDialog = (BusyLoaderDialog*)pCreate->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pDialog);
	}
	else {
		pDialog = (BusyLoaderDialog*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	}

	switch (uMsg) {
	case WM_TIMER:
		if (pDialog)
		{

			if (wParam == ANIMATION_TIMER_ID) {
				pDialog->OnTimer();
			}
			else if (wParam == LOGIC_TIMER_ID) {
				if (!pDialog->m_coroTask || pDialog->m_coroTask.is_done())
					pDialog->Close();
			}
		}
		return 0;

	case WM_ERASEBKGND:
	{
		return true;
		break;
	}

	case WM_PAINT: {

		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		if (pDialog->showAmination)
			pDialog->DrawAnimation();

		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_CLOSE:
		if (pDialog) {
			pDialog->Cleanup();
			pDialog->Close();
		}
		return 0;

	case WM_DESTROY:
		return 0;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}