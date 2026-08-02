#pragma once

#include <glm/glm.hpp>
#include <string>
#include <map>
#include <memory>
#include "Texture2D.h"
#include "OpenGLRenderEngine/Base/Shader.h"

enum class TextureType
{
	Albedo = 0,
	Metallic,
	Roughness,
	Normal,
	AO,
	Emissive,
	MetallicRoughness,
	Height,
	Opacity
};

// 使用透明纹理的方式
enum class AlphaMode
{
	Opaque = 0,		// 完全忽略透明纹理
	Mask,			// 遮罩模式，根据阈值决定是否显示（0或1）
	Blend
};

struct MaterialProperties
{
	// PBR属性
	glm::vec3 albedo = glm::vec3(1.0f);
	float metallic = 0.02f;					// 金属度 (0-1)
	float roughness = 0.6f;					// 粗糙度 (0-1)
	float ambientOcclusion = 1.0f;			// 环境光遮蔽

	// 透明属性
	float opacity = 1.0f;

	// 折射属性
	float IOR = 1.f;			// 折射率，决定折射角度，详情查IOR折射率表

	AlphaMode alphamode = AlphaMode::Opaque;	// 透明纹理使用模式
	float maskthreshold = 0.5f;					// Mask模式下的阈值

	bool twosided = false;		//绘制双面

	// 是否使用纹理
	bool useAlbedoTexture = true;
	bool useMetallicTexture = true;
	bool useRoughnessTexture = true;
	bool useNormalTexture = true;
	bool useAOTexture = true;
	bool useEmissiveTexture = true;
	bool useMetallicRoughnessTexture = true;
	bool useHeightTexture = true;
	bool useOpacityTexture = true;
};

class Material 
{
public:
	Material() = default;
	explicit Material(const MaterialProperties& props);

	// 添加纹理
	void SetTexture(TextureType type, std::shared_ptr<Texture2D> texture);

	// 设置属性
	void SetProperty(const MaterialProperties& props);
	void SetAlbedo(const glm::vec3& albedo);
	void SetMetallic(float metallic);
	void SetRoughness(float roughness);
	void SetAmbientOcclusion(float ambientOcclusion);
	void SetOpacity(float opacity);
	void SetIOR(float IOR);
	void SetAlpahMode(AlphaMode mode);
	void SetMaskThreshold(float value);
	void SetTwoSided(bool value);

	// 获取属性
	MaterialProperties GetProperties() const;
	glm::vec3 GetAlbedo() const;
	float GetMetallic() const;
	float GetRoughness() const;
	float GetAmbientOcclusion() const;
	float GetOpacity() const;
	float GetIOR() const;
	AlphaMode GetAlphaMode() const;
	float GetMaskThreshold() const;
	bool GetTwoSided() const;

	const std::map<TextureType, std::shared_ptr<Texture2D>>& GetTextures() const;

	// 克隆
	std::shared_ptr<Material> Clone() const;

private:
	MaterialProperties properties;
	std::map<TextureType, std::shared_ptr<Texture2D>> textures;
};
