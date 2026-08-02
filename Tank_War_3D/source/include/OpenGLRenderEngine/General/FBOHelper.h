#pragma once

#include "GL/glew.h"
#include "RenderEngine/SharedTexture.h"
#include "../Base/Texture2D.h"
#include <memory>

class FBOHelper
{
public:
	static void InitFbo(GLuint& fbo, std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& depthBuffer, float SCR_WIDTH, float SCR_HEIGHT);
	static void InitFbo(GLuint& fbo, std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& brightColorBuffer, std::shared_ptr<Texture2D>& depthBuffer, float SCR_WIDTH, float SCR_HEIGHT);

	static void InitFbo(GLuint& fbo, GLuint& colorBuffer, float SCR_WIDTH, float SCR_HEIGHT);
	static void InitFbo(GLuint& fbo, SharedTexture* sharedTexture, float SCR_WIDTH, float SCR_HEIGHT);
};
