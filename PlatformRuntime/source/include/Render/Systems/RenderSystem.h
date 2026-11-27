#pragma once

#include "ECSCore/Entity.h"
#include "ECSCore/System.h"
#include "RenderEngine/RenderFrameData.h"
#include "RenderEngine/Renderer.h"
#include "SpinLock.h"
#include "Helper/SharedObjectPool.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "Script/IScriptModule.h"

#include <functional>
#include <vector>

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

    void SetTriBufferScene(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderSceneFrameData>>> triBuffer);
    void SetTriBufferUI(std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderUIFrameData>>> triBuffer);

    // 场景当前生效的 2D 相机（primary 且 priority 最大）；一台都没有时返回默认视图、found = false。
    // 渲染收集和编辑器（鼠标坐标换算到 2D 世界）共用这一个选法
    static VKRenderContext::Camera2DData GetActiveCamera2D(World& world, bool* found = nullptr);

    // 编辑器 2D 点选：和渲染用同一套摆放规则（轴心、尺寸、偏移、旋转、缩放、翻转），按矩形判定（透明像素也算命中）。
    // 返回盖住 point（2D 世界坐标）的最上面那个实体：和绘制顺序一致，层级、层内顺序、实体 id 依次比大小；没有返回空实体
    static Entity Pick2D(World& world, const glm::vec2& point);

    // 编辑器选中框：2D 精灵 / 动图当前帧的矩形（与 Pick2D、渲染同一套摆放规则，九宫格取整体外框）
    struct Bounds2D
    {
        glm::vec2 corners[4] = {};      // 2D 世界坐标，按顺序相邻，可直接连成闭合四边形
        glm::vec2 pivot = glm::vec2(0.0f);   // 轴心（Transform2D 位置 + offset）
    };
    // entity 有 Transform2D 且有已解析的 Sprite 或 AnimatedImagePlayer（Sprite 优先）时返回 true。
    // 不看 visible / 透明度：选中了隐藏或全透明的精灵也能看到它在哪。必须在场景线程调用
    static bool Get2DBounds(World& world, Entity entity, Bounds2D& out);

    // 编辑器 2D 框选：可见的精灵 / 动图（筛选同 Pick2D）里，矩形经 toRectSpace 映射后和轴对齐矩形 [rectMin, rectMax] 有重叠的，
    // 按实体 id 升序、去重。toRectSpace 把 2D 世界坐标映射到框选矩形所在的坐标系（比如视口屏幕坐标），必须是仿射变换
    // （2D 相机的视图变换就是），这样矩形映射后仍是平行四边形。必须在场景线程调用
    static std::vector<Entity> Pick2DRect(World& world, const std::function<glm::vec2(const glm::vec2&)>& toRectSpace,
        const glm::vec2& rectMin, const glm::vec2& rectMax);

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
    std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderSceneFrameData>>> _triBufferScene;
    std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderUIFrameData>>> _triBufferUI;
    ThreadPool _threadPool;
    SpinLock _Scene3D_SpinLock;
    SpinLock _D2D_SpinLock;
    SpinLock _debugLineLock;
};

HY_DECLARE_ECS_TYPE(RenderSystem, "hy.engine.system.Render");
