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
    inline static std::vector<std::string> FieldName = { "model", "light", "sprite", "animatedimage", "laserbeam", "particle", "skybox" };

public:
    explicit RenderableBaseModule(World* world);
    virtual void Register(sol::state_view lua, sol::usertype<Entity>& entityType) override;

    bool SetVisiable(const Entity& entity, const std::string& field, bool value);
    bool SetColor(const Entity& entity, const glm::vec3& color);

private:
    World* _world = nullptr;
};

// 2D 调试线（世界坐标，y 朝下）；宽度按屏幕像素
struct Line2
{
    glm::vec2 pos1;
    glm::vec2 pos2;
    glm::vec4 color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    float width = 1.0f;
};

class RenderSystem :public System
{
public:
    RenderSystem();
    virtual void onAttach(World& world) override;

    void SetTriBuffer3D(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render3DFrameData>>> triBuffer);
    void SetTriBuffer2D(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::Render2DFrameData>>> triBuffer);
    void SetTriBufferUI(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderUIFrameData>>> triBuffer);

    // 场景当前生效的 2D 相机（primary 且 priority 最大）；一台都没有时返回默认视图、found = false。
    // 渲染收集和编辑器（鼠标坐标换算到 2D 世界）共用这一个选法
    static VKRenderContext::Camera2DData GetActiveCamera2D(World& world, bool* found = nullptr);

    // 编辑器 2D 点选：和渲染用同一套摆放规则（轴心、尺寸、偏移、旋转、缩放、翻转），按矩形判定（透明像素也算命中）。
    // 返回盖住 point（2D 世界坐标）的最上面那个实体：和绘制顺序一致，层级、层内顺序、实体 id 依次比大小；没有返回空实体
    static Entity Pick2D(World& world, const glm::vec2& point);

public:
    virtual void postUpdate(float deltaTime) override;
    void pushDebugLine(const Line2& line);

private:
    void ProcessSprite(std::shared_ptr<Render::Render2DFrameData> framebuffer);
    void ProcessAnimatedImage(std::shared_ptr<Render::Render2DFrameData> framebuffer);
    void ProcessCamera2D(std::shared_ptr<Render::Render2DFrameData> framebuffer);
    void ProcessDebugLines(std::shared_ptr<Render::Render2DFrameData> framebuffer);
    void ProcessUI(std::shared_ptr<Render::RenderUIFrameData> framebuffer);

private:
    void ProcessModel(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void ProcessStaticModel(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void ProcessSkinnedModel(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void ProcessLight(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void ProcessParticle(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void ProcessLaserBeam(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);

private:
    void ProcessSkybox(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);
    void ProcessDDGIVolume(std::shared_ptr<Render::Render3DFrameData> framebuffer, Entity maincamera);

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
    SpinLock _debugLineLock;
};

HY_DECLARE_ECS_TYPE(RenderSystem, "hy.engine.system.Render");
