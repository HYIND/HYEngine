#version 460 core

#include "shader/dataDef/MaterialTextureDef.comp"
#include "shader/dataDef/camerauboDef.comp"
#include "shader/Helper/lightingHelper.comp"

layout (location = 0) in vec3 FragPos;
layout (location = 1) in vec3 FragNormal;
layout (location = 2) in vec2 FragTextureCoords;
layout (location = 3) in mat3 TBN;
layout (location = 7) flat in uint materialIndex;

layout (set = 0, binding = 8) uniform sampler2D atlasShadowMap;

layout (location = 0) out vec4 FragColor;

void main()
{
	MaterialData material = materials[materialIndex];

	vec3 normal = calculateNormal(material, FragNormal, TBN, FragTextureCoords);
	if (!gl_FrontFacing)		// 双面渲染，背面要翻法线，否则光照是反的
		normal = -normal;
	if (dot(normal, normal) < 1e-6)
		discard;

	float opacity = calculateOpacity(material, FragTextureCoords);
	if (opacity < 0.005)
		discard;

	PBRProperties prop;
	getPBRProperties(material, FragTextureCoords, prop.albedo, prop.metallic, prop.roughness, prop.ambientOcclusion);

	vec3 lightingColor = CalcLighting(camera.view, camera.position, camera.farPlane, atlasShadowMap, FragPos, normal, prop);

	vec3 scatteringColor = prop.albedo;
	const float lightingFact = 0.9;
	const float scatteringFact = 0.1;

	FragColor = vec4(lightingColor * lightingFact + scatteringColor * scatteringFact, opacity);
}
