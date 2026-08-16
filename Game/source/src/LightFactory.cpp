#include "Factory/LightFactory.h"
#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "GamePlayComponents.h"

Entity LightFactory::CreateDirLight(
	World& world,
	const glm::vec3& direction,
	const glm::vec3& color,
	float luxIntensity,
	bool castShadow,
	uint32_t cascadeLevel,
	uint32_t shadowMapWidth,
	uint32_t shadowMapHeight
)
{
	Entity entity = world.createEntityWithTag<TagLight>();
	DirectionalLightData light;
	light.color = color;
	light.luxIntensity = luxIntensity;
	light.cascadeLevel = cascadeLevel;
	light.shadowMapWidth = shadowMapWidth;
	light.shadowMapHeight = shadowMapHeight;
	light.castShadow = castShadow;
	auto& renderlight = entity.addComponent<RenderLight>(light);
	renderlight.renderCube = false;
	auto& trans = entity.addComponent<Transform>();
	trans.rotation = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), glm::normalize(direction));

	return entity;
}

Entity LightFactory::CreateSpotLight(
	World& world,
	const glm::vec3 position,
	const glm::vec3& direction,
	float cdIntensity,
	float cutOffAngle,
	float outercutOffAngle,
	const glm::vec3& color,
	bool castShadow,
	uint32_t shadowMapWidth,
	uint32_t shadowMapHeight
)
{
	Entity entity = world.createEntityWithTag<TagLight>();
	SpotLightData light;
	light.color = color;
	light.cdIntensity = cdIntensity;
	light.cutOffAngle = cutOffAngle;
	light.outercutOffAngle = outercutOffAngle;
	light.shadowMapWidth = shadowMapWidth;
	light.shadowMapHeight = shadowMapHeight;
	light.castShadow = castShadow;
	auto& renderlight = entity.addComponent<RenderLight>(light);
	renderlight.renderCube = false;
	auto& trans = entity.addComponent<Transform>();
	trans.position = position;
	trans.rotation = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), glm::normalize(direction));

	return entity;
}

Entity LightFactory::CreatePointLight(
	World& world,
	const glm::vec3 position,
	float cdIntensity,
	const glm::vec3& color,
	bool castShadow,
	uint32_t shadowMapWidth,
	uint32_t shadowMapHeight
)
{
	Entity entity = world.createEntityWithTag<TagLight>();
	PointLightData light;
	light.color = color;
	light.cdIntensity = cdIntensity;
	light.shadowMapWidth = shadowMapWidth;
	light.shadowMapHeight = shadowMapHeight;
	light.castShadow = castShadow;
	auto& renderlight = entity.addComponent<RenderLight>(light);
	renderlight.renderCube = false;
	auto& trans = entity.addComponent<Transform>();
	trans.position = position;

	return entity;
}
