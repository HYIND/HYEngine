#pragma once

#include "ECSCore/Entity.h"
#include "ECSCore/System.h"
#include "RenderEngine/RenderFrameData.h"
#include "RenderEngine/Renderer.h"
#include "SpinLock.h"
#include "Helper/SharedObjectPool.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "Script/IScriptModule.h"

class RenderableBaseModule : public IScriptModule
{
public:
    inline static std::vector<std::string> FieldName = { "model", "light", "sprite" ,"laserbeam", "particle", "skybox" };

public:
    explicit RenderableBaseModule(World* world);
    virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) override;

    bool SetVisiable(const Entity& entity, const std::string& field, bool value);
    bool SetColor(const Entity& entity, const glm::vec3& color);

private:
    World* _world = nullptr;
};

struct Line2
{
    glm::vec2 pos1;
    glm::vec2 pos2;
};

class RenderSystem :public System
{
public:
    RenderSystem();
    virtual void onAttach(World& world) override;

    void SetTriBuffer3D(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render3DFrameData>>> triBuffer);
    void SetTriBuffer2D(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render2DFrameData>>> triBuffer);
    void SetTriBufferUI(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderUIFrameData>>> triBuffer);

public:
    virtual void postUpdate(float deltaTime) override;
    void pushDebugLine(const Line2& line);

private:
    void processSprite(std::shared_ptr<Render::Render2DFrameData> framebuffer);
    void processGIFAnimation(std::shared_ptr<Render::Render2DFrameData> framebuffer);
    void processDebugLines(std::shared_ptr<Render::Render2DFrameData> framebuffer);

    // 从 UIViewRuntime 收集这一帧要画的 UI 文档，按 layer 排序。
    void processUI(std::shared_ptr<Render::RenderUIFrameData> framebuffer);

private:
    void processModel(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void processLight(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void processParticle(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void processLaserBeam(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);

    //private:
    //	void processFirstPersonWeaponVisual(std::shared_ptr<Render::Render3DFrameData>& framebuffer, Entity& entity);

private:
    void processSkybox(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);

private:
	void SyncGLCamera(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);

private:
    std::vector<Line2> _DebugLines;
    std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render3DFrameData>>> _triBuffer3D;
    std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render2DFrameData>>> _triBuffer2D;
    std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderUIFrameData>>> _triBufferUI;
    ThreadPool _threadPool;
    SpinLock _Scene3D_SpinLock;
    SpinLock _D2D_SpinLock;
};

HY_DECLARE_ECS_TYPE(RenderSystem, "hy.engine.system.Render");
