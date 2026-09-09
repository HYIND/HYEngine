#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/VKWrapper/WrapperGeneral.h"
#include "../Base/Texture2D.h"
//#include "VulkanRenderEngine/SharedTexture.h"


class FBOHelper
{
public:
	static void InitFbo(std::shared_ptr<VKWrapper::VKFrameBuffer>& fbo, std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& depthBuffer, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
	static void InitFbo(std::shared_ptr<VKWrapper::VKFrameBuffer>& fbo, std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& brightColorBuffer, std::shared_ptr<Texture2D>& depthBuffer, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);

	static void InitFbo(std::shared_ptr<VKWrapper::VKFrameBuffer>& fbo, std::shared_ptr<Texture2D>& colorBuffer, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
	//static void InitFbo(GLuint& fbo, SharedTexture* sharedTexture, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT);
};
