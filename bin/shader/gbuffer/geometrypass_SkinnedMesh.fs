#version 430 core

in vec3 FragPos;
in vec3 FragNormal;
in vec2 FragTextureCoords;
in mat3 TBN;
in vec2 MotionVector;

layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoOpacity;
layout (location = 3) out vec4 gMetallicRoughness;
layout (location = 4) out vec2 gMotionVector;

#include "shader/dataDef/materialuboDef.comp"

void main()
{
	gPosition = FragPos;

	vec3 normal = calculateNormalFromUBO(FragNormal, TBN, FragTextureCoords);

	vec3 albedo;
	float metallic;
	float roughness;
	float ambientOcclusion;

	getPBRPropertiesFromUBO(FragTextureCoords, albedo, metallic, roughness, ambientOcclusion);
	
	float opacity = calculateOpacityFromUBO(FragTextureCoords);

	if (opacity < 0.01)
		discard;

	gNormal = normal;
	gAlbedoOpacity = vec4(albedo, opacity);
	gMetallicRoughness = vec4(metallic, roughness, ambientOcclusion, material.IOR);
	gMotionVector = MotionVector;
}
