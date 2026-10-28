#pragma once

#include "VulkanRenderEngine/Base/AtlasMap.h"
#include "VulkanRenderEngine/Base/Pipeline.h"
#include "VulkanRenderEngine/General/VKRenderContext.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/VKWrapper/VKCommandBuffer.h"


class BaseParticleProperties;

class RenderHelper
{
public:
	static std::shared_ptr<StaticModel> GetFloorModel(const glm::vec2& scale = glm::vec2(1.0f), float textureScale = 1.0f, const AssetGUID& guid = "");
	static std::shared_ptr<StaticModel> GetSphereModel(float radius = 0.5f, int sectors = 36, int stacks = 18, const AssetGUID& guid = "");
	static std::shared_ptr<StaticModel> GetCubeModel(const glm::vec3& scale = glm::vec3(1.0f), float textureScale = 1.0f, const AssetGUID& guid = "");
	static std::shared_ptr<StaticModel> GetCylinderModel(float radius = 0.5f, float height = 1.f, int segments = 36, const AssetGUID& guid = "");
};

