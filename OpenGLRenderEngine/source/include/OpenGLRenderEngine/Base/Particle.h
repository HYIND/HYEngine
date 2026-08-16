#pragma once

#include "OpenGLRenderEngine/Base/Texture2D.h"
#include "OpenGLRenderEngine/Base/Model.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "Effect.h"

enum class ParticleShape { BillboardDisc = 0, BillboardSoftDisc = 1, BillboardQuad = 2, Sphere = 3 };	//Billboard模式：始终朝向玩家；WorldTrans模式，正常应用旋转
enum class ParticleType { Color = 0, Color_Texture = 1, Model = 2 };

struct BaseParticleProperties :public BaseEffectProperties
{
	ParticleType particleType;

	glm::vec3 baseColor;
	float opacity;

	glm::mat4 transform = glm::mat4(1.0f);
	glm::vec3 position = glm::vec3(0.f);
	glm::quat rotation = glm::identity<glm::quat>();
	glm::vec3 scale = glm::vec3(1.f);

public:
	BaseParticleProperties() { effectType = EffectType::Particle; }
	BaseParticleProperties(const glm::vec3& color, float opacity) :baseColor(color), opacity(std::clamp(opacity, 0.f, 1.f)) { effectType = EffectType::Particle; }
	BaseParticleProperties(const BaseParticleProperties& other)
		:particleType(other.particleType)
		, baseColor(other.baseColor)
		, opacity(other.opacity)
		, transform(other.transform)
		, position(other.position)
		, rotation(other.rotation)
		, scale(other.scale) {
		effectType = EffectType::Particle;
	}
	virtual std::shared_ptr<BaseEffectProperties> Clone() = 0;
};

struct ColorParticleProperties :public BaseParticleProperties
{
	ParticleShape shape = ParticleShape::BillboardDisc;

	ColorParticleProperties(const glm::vec3& color = glm::vec3(1.0f), float opacity = 1.0f, ParticleShape shape = ParticleShape::BillboardDisc)
		:BaseParticleProperties(color, opacity), shape(shape)
	{
		particleType = ParticleType::Color;
	}
	ColorParticleProperties(const ColorParticleProperties& other)
		: BaseParticleProperties(other), shape(other.shape) {
	}
	virtual std::shared_ptr<BaseEffectProperties> Clone() {
		auto other = std::make_shared<ColorParticleProperties>(*this);
		return other;
	}
};
struct TextureParticleProperties :public BaseParticleProperties
{
	ParticleShape shape = ParticleShape::BillboardQuad;
	std::shared_ptr<Texture2D> texture;

	TextureParticleProperties(std::shared_ptr<Texture2D> texture, const glm::vec3& color = glm::vec3(1.0f), float opacity = 1.0f, ParticleShape shape = ParticleShape::BillboardQuad)
		:BaseParticleProperties(color, opacity), texture(texture)
	{
		particleType = ParticleType::Color_Texture;
	}
	TextureParticleProperties(const TextureParticleProperties& other)
		: BaseParticleProperties(other), shape(other.shape), texture(other.texture) {
	}
	virtual std::shared_ptr<BaseEffectProperties> Clone() {
		auto other = std::make_shared<TextureParticleProperties>(*this);
		return other;
	}
};
struct ModelParticleProperties :public BaseParticleProperties
{
	std::shared_ptr<Model> model;

	ModelParticleProperties(std::shared_ptr<Model> model, const glm::vec3& color = glm::vec3(1.0f), float opacity = 1.0f)
		:BaseParticleProperties(color, opacity), model(model)
	{
		particleType = ParticleType::Model;
	}
	ModelParticleProperties(const ModelParticleProperties& other)
		: BaseParticleProperties(other), model(other.model) {
	}
	virtual std::shared_ptr<BaseEffectProperties> Clone() {
		auto other = std::make_shared<ModelParticleProperties>(*this);
		return other;
	}
};
