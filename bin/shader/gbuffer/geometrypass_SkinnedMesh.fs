#version 430 core

#include "shader/dataDef/MaterialTextureDef.comp"

layout (location = 0) in vec3 FragPos;
layout (location = 1) in vec3 FragNormal;
layout (location = 2) in vec2 FragTextureCoords;
layout (location = 3) in mat3 TBN;
layout (location = 6) in vec2 MotionVector;
layout (location = 7) flat in uint materialIndex;

layout (location = 0) out vec3 gPosition;
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

	gPosition = FragPos;
	gNormal = normal;
	gAlbedoOpacity = vec4(albedo, opacity);
	gMetallicRoughness = vec4(metallic, roughness, ambientOcclusion, material.IOR);
	gMotionVector = MotionVector;
	gEmission = emission;
}
