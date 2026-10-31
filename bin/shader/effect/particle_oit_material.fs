#version 460 core

// 有材质粒子的 AlphaBlend 版本 —— Weighted Blended OIT 的第一段（累积）。
// 颜色/不透明度来自材质 albedo * 粒子 tint、材质 opacity * 粒子 alpha。

#include "shader/dataDef/camerauboDef.comp"
#include "shader/dataDef/MaterialTextureDef.comp"
#include "shader/effect/particle_common.comp"

layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec3 FragWorldPos;
layout(location = 2) flat in vec4 FragColor;
layout(location = 3) flat in uint FragMask;
layout(location = 4) flat in uint FragMaterialIndex;
layout(location = 5) flat in uint FragFlags;

layout(location = 0) out vec4 OutAccum;
layout(location = 1) out vec4 OutReveal;

// McGuire & Bavoil 2013 的权重：近处/不透明的片元权重大，远处的快速衰减。
float OitWeight(float z, float alpha)
{
	float a = min(1.0, alpha * 10.0) + 0.01;
	return clamp(a * a * a * 1e8 * pow(1.0 - z * 0.9, 3.0), 1e-2, 3e3);
}

void main()
{
	vec4 color = FragColor;

	if (!ApplyParticleMask(FragMask, FragUV, color))
		discard;

	MaterialData material = materials[FragMaterialIndex];
	vec3 albedo = calculateAlbedo(material, FragUV) * color.rgb;
	float alpha = clamp(calculateOpacity(material, FragUV) * color.a, 0.0, 1.0);
	if (alpha <= 0.0)
		discard;

	float depth = clamp(length(FragWorldPos - camera.position) / max(camera.farPlane, 1e-4), 0.0, 0.999);
	float weight = OitWeight(depth, alpha);

	OutAccum = vec4(albedo * alpha * weight, alpha * weight);
	OutReveal = vec4(alpha);
}
