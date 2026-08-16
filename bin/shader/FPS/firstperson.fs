#version 430 core

#include "shader/dataDef/materialuboDef.comp"

in vec2 FragTextureCoords;
in vec3 FragNormal;
in vec3 FragPos;
in mat3 TBN;

layout (location = 0) out vec4 FragColor;
 
void main()
{
	vec3 normal = calculateNormalFromUBO(FragNormal, TBN, FragTextureCoords);
    vec3 albedo = calculateAlbedoFromUBO(FragTextureCoords);
	FragColor = vec4(albedo, material.opacity);
}
