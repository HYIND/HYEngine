#version 460 core

// 有材质的粒子（billboard + 材质）：颜色 = 材质 albedo * 粒子 tint，
// alpha = 材质 opacity * 粒子 alpha。只消费材质的 albedo / opacity，
// metallic、roughness、normal、IOR 等字段对粒子无意义（粒子不做光照）。

#include "shader/dataDef/MaterialTextureDef.comp"
#include "shader/effect/particle_common.comp"

layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec3 FragWorldPos;
layout(location = 2) flat in vec4 FragColor;
layout(location = 3) flat in uint FragMask;
layout(location = 4) flat in uint FragMaterialIndex;
layout(location = 5) flat in uint FragFlags;

layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 color = FragColor;

	if (!ApplyParticleMask(FragMask, FragUV, color))
		discard;

	MaterialData material = materials[FragMaterialIndex];
	vec3 albedo = calculateAlbedo(material, FragUV) * color.rgb;
	float alpha = clamp(calculateOpacity(material, FragUV) * color.a, 0.0, 1.0);

	OutColor = vec4(albedo, alpha);
}
