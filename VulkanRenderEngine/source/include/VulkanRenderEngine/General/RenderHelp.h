#pragma once

#include "VulkanRenderEngine/Base/AtlasMap.h"
#include "VulkanRenderEngine/Base/Pipeline.h"
#include "VulkanRenderEngine/General/VKRenderContext.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/VKWrapper/VKCommandBuffer.h"


class BaseParticleProperties;

class RenderHelp
{
public:
	static void renderScreenQuad(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd);
	static void renderBillboardQuad(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd);
	static void renderCube(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd);
	static void renderSphere(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd);
	static void renderCylinder(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd);

public:
	static void SetupAnimatorGroupData(std::shared_ptr<VKWrapper::VKCommandBuffer> cmd, const std::vector<VKRenderContext::AnimatorView>& animatorViews);
	static void SetupLightingData(
		std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		Pipeline& pieline,
		const std::vector<std::shared_ptr<DirLightInfo>>& dirLights,
		const std::vector<std::shared_ptr<PointLightInfo>>& pointLights,
		const std::vector<std::shared_ptr<SpotLightInfo>>& spotLights,
		const std::shared_ptr<AtlasMap>& atlasShadowMap
	);
};

std::shared_ptr<Model> GetFloorModel(const glm::vec2& scale = glm::vec2(1.0f), float textureScale = 1.0f);
std::shared_ptr<Model> GetSphereModel(float radius = 0.5f, int sectors = 36, int stacks = 18);
std::shared_ptr<Model> GetCubeModel(const glm::vec3& scale = glm::vec3(1.0f), float textureScale = 1.0f);
std::shared_ptr<Model> GetCylinderModel(float radius = 0.5f, float height = 1.f, int segments = 36);

bool GetCubeViewPorts(std::array<DynamicViewport, 6>& viewports, const std::shared_ptr<PointLightInfo>& info, const AtlasMap& atlasShadowMap);