#pragma once
#include <glm/glm.hpp>

// 可调节材质属性，由Render系统进行同步
struct VariableMaterialData
{
	enum class AlphaMode
	{
		Opaque = 0,		// 完全忽略透明纹理
		Mask,			// 遮罩模式，根据阈值决定是否显示（0或1）
		Blend
	};

	// PBR属性
	glm::vec3 albedo = glm::vec3(1.0f);
	float metallic = 0.05f;
	float roughness = 0.6f;

	// 透明属性
	float opacity = 1.0f;
	AlphaMode alphamode = AlphaMode::Opaque;	// 透明纹理使用模式

	bool twosided = false;		//绘制双面
};

struct VariableMaterialChangeFlag
{
	bool albedoChange = false;
	bool metallicChange = false;
	bool roughnessChange = false;
	bool opacityChange = false;
	bool alphamodeChange = false;
	bool twosidedChange = false;

	bool AnyChange() {
		return albedoChange || metallicChange || roughnessChange || opacityChange || alphamodeChange || twosidedChange;
	}
	void Reset() {
		albedoChange = false;
		metallicChange = false;
		roughnessChange = false;
		opacityChange = false;
		alphamodeChange = false;
		twosidedChange = false;
	}
};