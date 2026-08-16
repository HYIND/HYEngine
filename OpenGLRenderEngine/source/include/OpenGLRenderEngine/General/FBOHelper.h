#pragma once

#include "GL/glew.h"
#include "OpenGLRenderEngine/SharedTexture.h"
#include "../Base/Texture2D.h"
#include <memory>

class FBOHelper
{
public:
	static void InitFbo(GLuint& fbo, std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& depthBuffer, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
	static void InitFbo(GLuint& fbo, std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& brightColorBuffer, std::shared_ptr<Texture2D>& depthBuffer, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);

	static void InitFbo(GLuint& fbo, GLuint& colorBuffer, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
	static void InitFbo(GLuint& fbo, SharedTexture* sharedTexture, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
};
