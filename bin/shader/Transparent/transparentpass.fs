#version 430 core

#include "shader/dataDef/materialuboDef.comp"
#include "shader/dataDef/camerauboDef.comp"
#include "shader/Helper/lightingHelper.comp"

out vec4 FragColor;

in vec3 FragPos;
in vec3 FragNormal;
in vec2 FragTextureCoords;
in mat3 TBN;

uniform sampler2D atlasShadowMap;

void main()
{

	vec3 normal = calculateNormalFromUBO(FragNormal, TBN, FragTextureCoords);
	float opacity = material.opacity;

	PBRProperties prop;
	getPBRPropertiesFromUBO(FragTextureCoords, prop.albedo, prop.metallic, prop.roughness, prop.ambientOcclusion);

	if(opacity < 0.005)
		discard;

	vec3 lightingColor = CalcLighting(camera.view, camera.position, camera.farPlane, atlasShadowMap, FragPos, normal, prop);

	vec3 scatteringColor = prop.albedo;
	float lightingFact = 0.9;
	float scatteringFact = 0.1;

	FragColor = vec4(lightingColor * lightingFact + scatteringColor  * scatteringFact, opacity);
}
