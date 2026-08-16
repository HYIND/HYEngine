#pragma once

#include "ECSCore/Entity.h"
#include "ECSCore/System.h"
#include "RenderEngine/RenderFrameManager.h"
#include "RenderEngine/Renderer.h"

struct Line2
{
	glm::vec2 pos1;
	glm::vec2 pos2;
};

class RenderSystem :public System
{
public:
	void SetTriBuffer(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> triBuffer);
	void SetOpenGLRender(std::shared_ptr<OpenGLRenderer> render);

public:
	virtual void postUpdate(float deltaTime) override;
	void pushDebugLine(const Line2& line);

private:
	void processSprite(std::shared_ptr<Render::RenderFrameData>& framebuffer);
	void processGIFAnimation(std::shared_ptr<Render::RenderFrameData>& framebuffer);
	void processDebugLines(std::shared_ptr<Render::RenderFrameData>& framebuffer);

private:
	void processModel(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera);
	void processLight(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera);
	void processParticle(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera);
	void processLaserBeam(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera);

//private:
//	void processFirstPersonVisual(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera);
//	void processFirstPersonWeaponVisual(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& entity);

private:
	void processSkybox(std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera);

private:
	void SyncGLCamera(std::shared_ptr<OpenGLRenderer>& render, std::shared_ptr<Render::RenderFrameData>& framebuffer, Entity& maincamera);

private:
	std::vector<Line2> _DebugLines;
	std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> _triBuffer;
	std::shared_ptr<OpenGLRenderer> _render;
};