#pragma once

#include "stdafx.h"
#include <Windows.h>
#include <mutex>

class RenderContext
{
	class BindGuard
	{
	public:
		BindGuard(RenderContext& context);
		~BindGuard();

		BindGuard(const BindGuard&) = delete;
		BindGuard& operator=(const BindGuard&) = delete;
		BindGuard(BindGuard&&) = delete;
		BindGuard& operator=(BindGuard&&) = delete;

	private:
		RenderContext& _context;
		bool _isownbind;
	};

public:
	RenderContext();
	~RenderContext();

	HDC GetHDC();
	HGLRC GetHGLRC();

	void Bind();
	void UnBind();
	bool IsBind();
	void Release();

	RenderContext::BindGuard GetBindGuard();

	void SetMain(bool value);
private:
	void NeedHDC();
	void NeedHGLRC();

public:
	HDC hdc;
	HGLRC hglrc;
	bool isMain;
	bool isBind;
};

class RenderContextManager
{
public:
	static RenderContextManager* Instance();
	std::shared_ptr<RenderContext>& GetThreadRenderContext();

	HINSTANCE GetHinstance();
	HWND GetHwnd();
	RECT GetRECT();
	HGLRC GetHGLRC();

	void SetHinstance(HINSTANCE hInst);
	void SetHwnd(HWND hwnd);
	void SetRECT(RECT rect);
	void SetHGLRC(HGLRC hglrc);

	void WithMainOpenGLShared(std::function<void(void)>&& func);	//请求共享OPENGL主渲染上下文时使用
	void WithTempReleaseMainOpenGLBind(std::function<void(void)>&& func);

private:
	RenderContextManager();
	~RenderContextManager();

private:
	HINSTANCE main_hInst;
	HWND main_hwnd;
	RECT main_rect;
	HGLRC main_hglrc;

	std::mutex main_openglcontext_shared_lock;
};

#define RENDERCONTEXMANAGER RenderContextManager::Instance()
#define THREADCONTEXT RenderContextManager::Instance()->GetThreadRenderContext()