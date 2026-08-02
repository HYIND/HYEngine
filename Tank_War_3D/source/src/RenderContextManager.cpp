#include "Manager/RenderContextManager.h"
#include <format>
#include <thread>
#include "FlexThreadPool.h"
#include "CriticalSectionLock.h"

thread_local std::shared_ptr<RenderContext> thread_context = nullptr;
thread_local std::shared_ptr<LockGuard<std::mutex>> thread_maincontext_lock = nullptr;

FlexThreadPool delete_context_pool(4);

RenderContext::BindGuard::BindGuard(RenderContext& context)
	:_context(context)
{
	if (!_context.IsBind())
	{
		_context.Bind();
		_isownbind = true;
	}
	else
		_isownbind = false;
}

RenderContext::BindGuard::~BindGuard()
{
	if (_isownbind)
		_context.UnBind();
}

RenderContext::RenderContext() : hdc(NULL), hglrc(NULL), isMain(false), isBind(false)
{
}

RenderContext::~RenderContext()
{
	if (hglrc || hdc)
	{
		// 线程的opengl上下文释放时机似乎先于thread_local变量析构，本线程直接wglDeleteContext会非法访问
		// 转移到专用线程进行析构
		delete_context_pool.submit([hglrc = this->hglrc, hdc = this->hdc]()->void {
			if (hglrc) wglDeleteContext(hglrc);
			if (hdc) ReleaseDC(RENDERCONTEXMANAGER->GetHwnd(), hdc);
			});
	}
}

HDC RenderContext::GetHDC()
{
	NeedHDC();
	return hdc;
}

HGLRC RenderContext::GetHGLRC()
{
	NeedHGLRC();
	return hglrc;
}

void RenderContext::Bind()
{
	NeedHGLRC();
	wglMakeCurrent(hdc, hglrc);
	isBind = true;
}

void RenderContext::UnBind()
{
	wglMakeCurrent(NULL, NULL);
	isBind = false;
}

bool RenderContext::IsBind()
{
	return isBind;
}

void RenderContext::Release()
{
	if (hglrc)
	{
		UnBind();
		wglDeleteContext(hglrc);
		hglrc = NULL;
	}
	if (hdc)
	{
		ReleaseDC(RENDERCONTEXMANAGER->GetHwnd(), hdc);
		hdc = NULL;
	}
}

RenderContext::BindGuard RenderContext::GetBindGuard()
{
	return BindGuard(*this);
}

void RenderContext::SetMain(bool value)
{
	isMain = value;
}

void RenderContext::NeedHDC()
{
	if (!hdc)
	{
		HDC temphdc = GetDC(RENDERCONTEXMANAGER->GetHwnd());

		if (isMain)
		{
			PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),    // size of this pfd  
		1,                                // version number  
		PFD_DRAW_TO_WINDOW |              // support window  
		PFD_SUPPORT_OPENGL |              // support OpenGL  
		PFD_DOUBLEBUFFER,                 // double buffered  
		PFD_TYPE_RGBA,                    // RGBA type  
		24,                               // 24-bit color depth  
		0, 0, 0, 0, 0, 0,                 // color bits ignored  
		0,                                // no alpha buffer  
		0,                                // shift bit ignored  
		0,                                // no accumulation buffer  
		0, 0, 0, 0,                       // accum bits ignored  
		32,                               // 32-bit z-buffer      
		0,                                // no stencil buffer  
		0,                                // no auxiliary buffer  
		PFD_MAIN_PLANE,                   // main layer  
		0,                                // reserved  
		0, 0, 0                           // layer masks ignored  
			};

			int iPixelFormat = ChoosePixelFormat(temphdc, &pfd);
			SetPixelFormat(temphdc, iPixelFormat, &pfd);
		}

		hdc = temphdc;
	}
}

void RenderContext::NeedHGLRC()
{
	NeedHDC();

	if (!hglrc)
	{
		hglrc = wglCreateContext(hdc);

		if (!isMain)
		{
			RENDERCONTEXMANAGER->WithMainOpenGLShared([&]()->void {
				if (!wglShareLists(RenderContextManager::Instance()->GetHGLRC(), hglrc)) //与主渲染上下文关联
				{
					DWORD error = GetLastError();
					switch (error) {
					case ERROR_BUSY: // 170
						std::cerr << "wglShareLists error, One or both contexts are currently bound!" << std::endl;
						break;
					case ERROR_INVALID_PIXEL_FORMAT: // 2000
						std::cerr << "wglShareLists error, Pixel formats don't match!" << std::endl;
						break;
					default:
						std::cerr << "wglShareLists error, Unknown error: " << error << std::endl;
					}
				}
				}
			);
		}
	}
}

RenderContextManager* RenderContextManager::Instance()
{
	static RenderContextManager* m_instance = new RenderContextManager();
	return m_instance;
}

std::shared_ptr<RenderContext>& RenderContextManager::GetThreadRenderContext()
{
	if (!thread_context)
		thread_context = std::make_shared<RenderContext>();
	return thread_context;
}

HINSTANCE RenderContextManager::GetHinstance()
{
	return main_hInst;
}

HWND RenderContextManager::GetHwnd()
{
	return main_hwnd;
}

RECT RenderContextManager::GetRECT()
{
	return main_rect;
}

HGLRC RenderContextManager::GetHGLRC()
{
	return main_hglrc;
}

void RenderContextManager::SetHinstance(HINSTANCE hInst)
{
	main_hInst = hInst;
}

void RenderContextManager::SetHwnd(HWND hwnd)
{
	main_hwnd = hwnd;
}

void RenderContextManager::SetRECT(RECT rect)
{
	main_rect = rect;
}

void RenderContextManager::SetHGLRC(HGLRC hglrc)
{
	main_hglrc = hglrc;
}

void RenderContextManager::WithMainOpenGLShared(std::function<void(void)>&& func)
{
	thread_maincontext_lock = std::make_shared<LockGuard<std::mutex>>(main_openglcontext_shared_lock);
	//std::lock_guard<std::mutex> guard(main_openglcontext_shared_lock);
	func();
	thread_maincontext_lock.reset();
}

void RenderContextManager::WithTempReleaseMainOpenGLBind(std::function<void(void)>&& func)
{
	if (thread_maincontext_lock && thread_maincontext_lock->isownlock())
	{
		thread_maincontext_lock->unlock();
		func();
		thread_maincontext_lock->lock();
	}
	else
	{
		func();
	}
}

RenderContextManager::RenderContextManager()
{
	delete_context_pool.start();
}
RenderContextManager::~RenderContextManager()
{
}

