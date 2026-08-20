#pragma once

#include "ECSCore/World.h"
#include "OpenGLRenderEngine/OpenGLRenderer.h"
#include "RenderEngine/RenderFrameManager.h"
#include "CommonSystems.h"


struct EditorPick :public IComponent
{
};

struct NameTag :public IComponent
{
	std::string name;
};
void SetNameTag(Entity entity, const std::string& name);

class WorldManager
{
public:
	static WorldManager* Instance();

private:
	WorldManager();
	~WorldManager();

public:
	void InitOpenGLRender(uint32_t width, uint32_t height);
	void InitWorld();

	void RenderFrame();

	std::shared_ptr<OpenGLRenderer> GetOpenGLRener();
	std::shared_ptr<World> GetWorld();
	std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> GetTriBuffer();

	void RunWorld();
	void PauseWorld();
	void ContinueWorld();
	void StopWorld();

	Entity PickObject(const glm::vec3& origin, const glm::vec3& direction);
	RaycastHit RayCast(const glm::vec3& origin, const glm::vec3& direction);

	void SetInputActive(bool enable);
	void RotateCamera(float deltaX, float deltaY);
	void PanCamera(float deltaX, float deltaY);
	void ZoomCamera(float delta);

	RenderOption GetOption() const;
	void SetOption(RenderOption option);

	void ResizeOpenGL(uint32_t width, uint32_t height);

public:
	Entity CreateModelEntity(std::shared_ptr<Model> model);
	bool DuplicateEntity(Entity oriEntity, Entity& newEntity);

private:
	void WorldLoop();
	void AnalysisRenderFrameData(std::shared_ptr<Render::RenderFrameData>& framedata, RenderState& state);
	void processSceneModel(
		RenderState& state,
		OpenGLRenderObjectData::SceneRenderData& renderData,
		const std::shared_ptr<OpenGLRenderContext::SceneModelRenderData>& data
	);
	void processFirstPersonModel(
		RenderState& state,
		OpenGLRenderObjectData::FirstPersonRenderData& renderData,
		const std::shared_ptr<OpenGLRenderContext::FirstPersonRenderData>& data);

private:
	std::shared_ptr<World> _world;
	std::shared_ptr<std::thread> _worldThread;
	bool _stop = false;

	std::shared_ptr<OpenGLRenderer> _openglRenderer;
	std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> _triBuffer;

	uint32_t pendingWidth = 0, pendingHeight = 0;
	int64_t resizeTimeStamp = 0;
	bool resizePending;
};