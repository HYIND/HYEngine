#pragma once

#include "ECSCore/World.h"
#include "CommonSystems.h"
#include "EditorComponent.h"
#include "CommonData/LightData.h"
#include "SceneCodec/SceneCodec.h"
#include "EngineRuntime.h"
#include "ViewportCamera.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ProjectManager;

void SetNameTag(Entity entity, const std::string& name);

enum class GeometryType { Cube = 0, Sphere };
enum class EditorWorldState { Editing, Playing, Paused };

// 编辑器里的一个场景会话：当前 World、编辑/播放状态、播放快照、场景内容搭建与拾取。
// 运行时、工程、渲染 target 属于应用级的 EditorApp，这里只持有对运行时的引用。
class SceneEditor
{
public:
    explicit SceneEditor(EngineRuntime& runtime);

    static std::shared_ptr<SceneCodec> CreateDefaultSceneCodec();
    static void RegisterSceneComponentCodecs();

    void InitDefaultWorld();
    // 打开工程里的场景资源（GUID 或工程内路径），或从已有的 SceneCodec 恢复（播放快照回滚）。
    // 建 World、注册系统、解析 GUID、反序列化都由引擎做，这里只挂编辑器相机。
    bool LoadSceneAsset(const std::string& sceneAsset);
    bool LoadFromSceneCodec(const std::shared_ptr<SceneCodec>& scene);

    void RunWorld();
    void PauseWorld();
    void ContinueWorld();
    void StopWorld();
    bool PlayWorld();
    void StopPlayback();
    void UpdateEditorFrame(ProjectManager* projectManager);
    EditorWorldState GetEditorState() const;

    World* GetWorld();

    Entity PickObject(const glm::vec3& origin, const glm::vec3& direction);
    RaycastHit RayCast(const glm::vec3& origin, const glm::vec3& direction);

    void SetInputActive(bool enable);

    // 视口相机操作：相机数学在 ViewportCamera 里，这里只把当前场景和视口高度递过去。
    void RotateCamera(float deltaX, float deltaY);
    void PanCamera(float deltaX, float deltaY);
    ViewportCamera& Camera() { return m_camera; }
    // 平移按像素换算世界单位需要视口高度，由应用层注入（渲染 target 高度）。
    void SetViewportHeightProvider(std::function<float()> provider) { m_viewportHeight = std::move(provider); }

    Entity CreateModelEntity(std::shared_ptr<Model> model);
    bool DuplicateEntity(Entity oriEntity, Entity& newEntity);

    void AddLight(LightType type, const glm::vec3& position, const std::string& name = "");
    void AddGeometry(GeometryType type, const glm::vec3& position, const std::string& name = "");
    void AddEmptyEntity(const glm::vec3& position, const std::string& name = "");

    void ReloadScriptAssets(const std::vector<std::shared_ptr<ScriptSourceData>>& changed);

private:
    bool OpenEditorScene(const std::function<bool(World&)>& populateScene);
    // 两个打开入口的公共部分：停旧场景、挂编辑器相机、交给引擎落成新场景。
    bool OpenWithHooks(const std::function<bool(const EngineRuntime::SceneOpenHooks&, std::string*)>& opener);

private:
    EngineRuntime& m_runtime;
    ViewportCamera m_camera;
    std::function<float()> m_viewportHeight;

    World* _world = nullptr; // borrowed from EngineRuntime; invalid after CloseScene().
    EditorWorldState _editorState = EditorWorldState::Editing;
    std::shared_ptr<SceneCodec> _playSnapshot;
    std::unordered_map<std::string, bool> _prePauseSystemStates;
};
