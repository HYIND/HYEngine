#pragma once

#include "RenderEngine/Renderer.h"
#include "Helper/DynamicFpsController.h"
#include "RenderEngine/RenderFrameManager.h"
#include "Helper/TripleBuffer.h"

#define GLEW_STATIC    
#include "GL\glew.h"

void Render_Thread(
	std::shared_ptr<Render::Renderer> render,
	std::shared_ptr<DynamicFpsController> fpscontroller,
	std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> buffermanager);

bool IsRenderThread();

void WaitForInitOpenGL();
bool IsInitOpenGL();