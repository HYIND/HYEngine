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

	glm::vec3 emissionColor = glm::vec3(1.f);
	float emissionStrength = 0.0f;

	AlphaMode alphamode = AlphaMode::Opaque;	// 透明纹理使用模式
	float maskthreshold = 0.5f;					// Mask模式下的阈值

	bool twosided = false;		//绘制双面
};

// GPU结构体
struct alignas(16) MaterialData
{
	glm::vec3 albedo = glm::vec3(1.0f);
	float metallic = 0.0f;
	float roughness = 0.5f;
	float ambientOcclusion = 1.0f;

	float opacity = 1.0f;
	float IOR = 1.f;

	glm::vec3 emissionColor = glm::vec3(1.f);
	float emissionStrength = 0.0f;

	AlphaMode alphamode = AlphaMode::Opaque;
	float maskthreshold = 0.5f;

	int twosided = 0;

	int	texture_albedo_count = 0;
	int	texture_metallic_count = 0;
	int	texture_roughness_count = 0;
	int	texture_normal_count = 0;
	int	texture_ao_count = 0;
	int	texture_emissive_count = 0;
	int	texture_metallicroughness_count = 0;
	int	texture_height_count = 0;
	int	texture_opacity_count = 0;

	GLuint64 texture_albedo;
	GLuint64 texture_metallic;
	GLuint64 texture_roughness;
	GLuint64 texture_ao;
	GLuint64 texture_normal;
	GLuint64 texture_emissive;
	GLuint64 texture_metallicroughness;
	GLuint64 texture_height;
	GLuint64 texture_opacity;

	MaterialData();
};

class Material
{
public:
	Material();
	explicit Material(const MaterialProperties& props);
	~Material();

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
	void SetEmissionColor(const glm::vec3& color);
	void SetEmissionStrength(float value);

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
	glm::vec3 GetEmissionColor() const;
	float GetEmissionStrength() const;

	uint32_t GetVersion() const;
	const std::string& GetUUID() const;

	const std::map<TextureType, std::shared_ptr<Texture2D>>& GetTextures() const;
	MaterialData GetMaterialCompData();
	void GetMaterialCompData(MaterialData& comp_data);

	// 克隆
	std::shared_ptr<Material> Clone() const;

	void SetNeedUpdateIndirectDraw(bool value);
	bool GetNeedUpdateIndirectDraw() const;

private:
	void SetChange();

private:
	std::string _uuid;
	MaterialProperties _properties;
	std::map<TextureType, std::shared_ptr<Texture2D>> _textures;

	bool needUpdateIndirectDraw;
	std::atomic<uint32_t> _version;
};
