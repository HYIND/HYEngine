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

class SceneEditor
{
public:
	explicit SceneEditor(EngineRuntime& runtime);

	static std::shared_ptr<SceneCodec> CreateDefaultSceneCodec();
	static void RegisterSceneComponentCodecs();

	void InitDefaultWorld();
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
	void ReloadUIAssets();

	bool HasPendingSceneChange() { return m_runtime.HasPendingSceneChange(); }
	bool PumpSceneChanges() {
		bool open = m_runtime.PumpSceneChanges();
		if (open)_world = m_runtime.GetWorld();
		return open;
	}

private:
	bool OpenEditorScene(const std::function<bool(World&)>& populateScene);
	bool OpenWithHooks(const std::function<bool(const EngineRuntime::SceneOpenHooks&, std::string*)>& opener);

private:
	EngineRuntime& m_runtime;
	ViewportCamera m_camera;
	std::function<float()> m_viewportHeight;

	World* _world = nullptr;
	EditorWorldState _editorState = EditorWorldState::Editing;
	std::shared_ptr<SceneCodec> _playSnapshot;
	std::unordered_map<std::string, bool> _prePauseSystemStates;
};
