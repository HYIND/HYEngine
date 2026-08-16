#include "OpenGLRenderEngine/General/FBOHelper.h"
#include <iostream>

static void CheckFboStatus()
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		std::cout << "Framebuffer not complete!" << std::endl;
		switch (status) {
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
			std::cout << "Framebuffer incomplete: Attachment is incomplete." << std::endl;
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
			std::cout << "Framebuffer incomplete: No attachments." << std::endl;
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
			std::cout << "Framebuffer incomplete: Draw buffer issue." << std::endl;
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
			std::cout << "Framebuffer incomplete: Read buffer issue." << std::endl;
			break;
		case GL_FRAMEBUFFER_UNSUPPORTED:
			std::cout << "Framebuffer incomplete: Unsupported format combination." << std::endl;
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
			std::cout << "Framebuffer incomplete: Multisample mismatch." << std::endl;
			break;
		default:
			std::cout << "Framebuffer incomplete: Unknown error." << std::endl;
			break;
		}
	}
}

static void BindFbo(GLuint fbo, GLuint colorBuffer)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);

	GLenum attachments[] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, attachments);

	CheckFboStatus();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
static void BindFbo(GLuint fbo, GLuint colorBuffer, GLuint depthBuffer)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);

	GLenum attachments[] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, attachments);

	glBindTexture(GL_TEXTURE_2D, depthBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthBuffer, 0);

	CheckFboStatus();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
static void BindFbo(GLuint fbo, GLuint colorBuffer, GLuint brightColorBuffer, GLuint depthBuffer)
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer, 0);

	glBindTexture(GL_TEXTURE_2D, brightColorBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, brightColorBuffer, 0);

	GLenum attachments[] = { GL_COLOR_ATTACHMENT0 ,GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, attachments);

	glBindTexture(GL_TEXTURE_2D, depthBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthBuffer, 0);

	CheckFboStatus();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FBOHelper::InitFbo(GLuint& fbo,
	std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& depthBuffer,
	uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT)
{
	if (fbo == 0)
		glGenFramebuffers(1, &fbo);
	if (!colorBuffer)
		colorBuffer = std::make_shared<Texture2D>(SCR_WIDTH, SCR_HEIGHT, GL_RGBA16F);
	else
		colorBuffer->Resize(SCR_WIDTH, SCR_HEIGHT);
	if (!depthBuffer)
		depthBuffer = std::make_shared<Texture2D>(SCR_WIDTH, SCR_HEIGHT, GL_DEPTH24_STENCIL8);
	else
		depthBuffer->Resize(SCR_WIDTH, SCR_HEIGHT);

	colorBuffer->SetFiltering(GL_NEAREST).SetWrapping(GL_CLAMP_TO_EDGE);
	depthBuffer->SetFiltering(GL_NEAREST).SetWrapping(GL_CLAMP_TO_EDGE);

	BindFbo(fbo, colorBuffer->GetID(), depthBuffer->GetID());
}

void FBOHelper::InitFbo(GLuint& fbo,
	std::shared_ptr<Texture2D>& colorBuffer, std::shared_ptr<Texture2D>& brightColorBuffer, std::shared_ptr<Texture2D>& depthBuffer,
	uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT)
{
	if (fbo == 0)
		glGenFramebuffers(1, &fbo);
	if (!colorBuffer)
		colorBuffer = std::make_shared<Texture2D>(SCR_WIDTH, SCR_HEIGHT, GL_RGBA16F);
	else
		colorBuffer->Resize(SCR_WIDTH, SCR_HEIGHT);
	if (!brightColorBuffer)
		brightColorBuffer = std::make_shared<Texture2D>(SCR_WIDTH, SCR_HEIGHT, GL_RGBA16F);
	else
		brightColorBuffer->Resize(SCR_WIDTH, SCR_HEIGHT);
	if (!depthBuffer)
		depthBuffer = std::make_shared<Texture2D>(SCR_WIDTH, SCR_HEIGHT, GL_DEPTH24_STENCIL8);
	else
		depthBuffer->Resize(SCR_WIDTH, SCR_HEIGHT);

	colorBuffer->SetFiltering(GL_NEAREST).SetWrapping(GL_CLAMP_TO_EDGE);
	brightColorBuffer->SetFiltering(GL_NEAREST).SetWrapping(GL_CLAMP_TO_EDGE);
	depthBuffer->SetFiltering(GL_NEAREST).SetWrapping(GL_CLAMP_TO_EDGE);

	BindFbo(fbo, colorBuffer->GetID(), brightColorBuffer->GetID(), depthBuffer->GetID());
}

void FBOHelper::InitFbo(GLuint& fbo, GLuint& colorBuffer, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT)
{

	if (fbo == 0)
		glGenFramebuffers(1, &fbo);
	if (colorBuffer == 0)
		glGenTextures(1, &colorBuffer);

	glBindTexture(GL_TEXTURE_2D, colorBuffer);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SCR_WIDTH, SCR_HEIGHT,
		0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	BindFbo(fbo, colorBuffer);
}

void FBOHelper::InitFbo(GLuint& fbo, SharedTexture* sharedTexture, uint32_t SCR_WIDTH, uint32_t SCR_HEIGHT)
{
	wglDXLockObjectsNV(sharedTexture->InteropDevice, 1, &sharedTexture->InteropObject);

	if (fbo == 0)
		glGenFramebuffers(1, &fbo);

	glBindTexture(GL_TEXTURE_2D, sharedTexture->glTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	BindFbo(fbo, sharedTexture->glTex);

	wglDXUnlockObjectsNV(sharedTexture->InteropDevice, 1, &sharedTexture->InteropObject);
}
