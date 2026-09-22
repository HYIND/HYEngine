#pragma once

#include "ECSCore/World.h"
#include "VulkanRenderEngine/VulkanRenderer.h"
#include "RenderEngine/RenderFrameManager.h"
#include "CommonSystems.h"
#include "Helper/DynamicFpsController.h"

struct EditorPick :public IComponent
{};

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
	void SetRender(std::shared_ptr<VulkanRenderer> renderer);
	void InitWorld();


	std::shared_ptr<VulkanRenderer> GetVulkanRener();
	std::shared_ptr<World> GetWorld();
	std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> GetTriBuffer();

	void RunWorld();
	void PauseWorld();
	void ContinueWorld();
	void StopWorld();

	void RunPushFrame();
	void StopPushFrame();

	void WaitImage(const std::function<void(std::shared_ptr<Texture2D>)>& callback);

	Entity PickObject(const glm::vec3& origin, const glm::vec3& direction);
	RaycastHit RayCast(const glm::vec3& origin, const glm::vec3& direction);

	void SetInputActive(bool enable);
	void RotateCamera(float deltaX, float deltaY);
	void PanCamera(float deltaX, float deltaY);
	void ZoomCamera(float delta);

	RenderOption GetOption() const;
	void SetOption(RenderOption option);

	bool ResizeVulkan(uint32_t width, uint32_t height, std::function<void()> prevClear);

public:
	Entity CreateModelEntity(std::shared_ptr<Model> model);
	bool DuplicateEntity(Entity oriEntity, Entity& newEntity);

public:
	DynamicFpsController controller;
	DynamicFpsEstimate estimate;

private:
	void WorldLoop();
	void PushFrameLoop();

private:
	std::shared_ptr<World> _world;
	std::shared_ptr<std::thread> _worldThread;
	bool _worldStop = true;

	std::shared_ptr<std::thread> _framePushThread;
	bool _framePushStop = true;

	std::shared_ptr<VulkanRenderer> _renderer;
	std::shared_ptr<TripleBuffer<std::shared_ptr<Render::RenderFrameData>>> _triBuffer;

	uint32_t pendingWidth = 0, pendingHeight = 0;
	int64_t resizeTimeStamp = 0;
	bool resizePending = false;
};