#version 460 core

// 半透明材质的 Weighted Blended OIT 累积段（McGuire & Bavoil 2013）。
//
// attachment 0 (accum)  : Σ color*α*w , Σ α*w        混合 One / One
// attachment 1 (reveal) : Π (1-α)                     混合 Zero / (1 - src)

#include "shader/dataDef/MaterialTextureDef.comp"
#include "shader/dataDef/camerauboDef.comp"
#include "shader/Helper/lightingHelper.comp"

layout (location = 0) in vec3 FragPos;
layout (location = 1) in vec3 FragNormal;
layout (location = 2) in vec2 FragTextureCoords;
layout (location = 3) in mat3 TBN;
layout (location = 7) flat in uint materialIndex;

layout (set = 0, binding = 8) uniform sampler2D atlasShadowMap;

layout (location = 0) out vec4 OutAccum;
layout (location = 1) out vec4 OutReveal;

// 与粒子 OIT 保持一致的权重：近处/不透明的片元权重大，远处快速衰减。
float OitWeight(float z, float alpha)
{
	float a = min(1.0, alpha * 10.0) + 0.01;
	return clamp(a * a * a * 1e8 * pow(1.0 - z * 0.9, 3.0), 1e-2, 3e3);
}

void main()
{
	MaterialData material = materials[materialIndex];

	vec3 normal = calculateNormal(material, FragNormal, TBN, FragTextureCoords);
	if (dot(normal, normal) < 1e-6)
		discard;

	float opacity = clamp(calculateOpacity(material, FragTextureCoords), 0.0, 1.0);
	if (opacity < 0.005)
		discard;

	PBRProperties prop;
	getPBRProperties(material, FragTextureCoords, prop.albedo, prop.metallic, prop.roughness, prop.ambientOcclusion);

	vec3 lightingColor = CalcLighting(camera.view, camera.position, camera.farPlane, atlasShadowMap, FragPos, normal, prop);

	vec3 scatteringColor = prop.albedo;
	const float lightingFact = 0.9;
	const float scatteringFact = 0.1;
	vec3 color = lightingColor * lightingFact + scatteringColor * scatteringFact;

	// 归一化到相机远平面（与粒子一致），只用于权重衰减，不代表真实深度
	float depth = clamp(length(FragPos - camera.position) / max(camera.farPlane, 1e-4), 0.0, 0.999);
	float weight = OitWeight(depth, opacity);

	OutAccum = vec4(color * opacity * weight, opacity * weight);
	OutReveal = vec4(opacity);
}
