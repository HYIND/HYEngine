#version 460 core

#include "shader/dataDef/MaterialTextureDef.comp"
#include "shader/dataDef/GBufferPack.comp"

layout (location = 0) in vec3 FragPos;
layout (location = 1) in vec3 FragNormal;
layout (location = 2) in vec2 FragTextureCoords;
layout (location = 3) in mat3 TBN;
layout (location = 6) in vec4 CurClipPos;
layout (location = 8) in vec4 PrevClipPos;
layout (location = 7) flat in uint materialIndex;

layout (location = 0) out vec4 gPosition;		// xyz = 世界坐标，w = 打包的几何法线（GBufferPack.comp）
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoOpacity;
layout (location = 3) out vec4 gMetallicRoughness;
layout (location = 4) out vec2 gMotionVector;
layout (location = 5) out vec3 gEmission;

void main()
{
	MaterialData material = materials[materialIndex];

	vec3 normal = calculateNormal(material, FragNormal, TBN, FragTextureCoords);

	vec3 albedo = vec3(0.8);
	float metallic = 0.1;
	float roughness = 0.6;
	float ambientOcclusion = 1.0;

	getPBRProperties(material, FragTextureCoords, albedo, metallic, roughness, ambientOcclusion);
	
	float opacity = calculateOpacity(material, FragTextureCoords);

	vec3 emission = calculateEmission(material, FragTextureCoords);

	if (opacity < 0.01)
		discard;

	gPosition = vec4(FragPos, PackGeometryNormal(FragNormal));
	gNormal = normal;
	gAlbedoOpacity = vec4(albedo, opacity);
	gMetallicRoughness = vec4(metallic, roughness, ambientOcclusion, material.IOR);
	// 透视除法放在插值之后。上一帧在相机后方（w <= 0）时没有有效的上一帧位置，
	// 给一个超出屏幕的运动矢量，时域累积的边界检查会直接拒掉历史（原来会除出错误的值）
	vec2 uv_curr = CurClipPos.xy / CurClipPos.w * 0.5 + 0.5;
	vec2 motion = vec2(2.0);
	if (PrevClipPos.w > 1e-5)
	{
		vec2 uv_prev = PrevClipPos.xy / PrevClipPos.w * 0.5 + 0.5;
		motion = uv_curr - uv_prev;
	}
	gMotionVector = motion;
	gEmission = emission;
}
