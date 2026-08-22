#version 450 core

#include "shader/dataDef/camerauboDef.comp"
#include "shader/Helper/ligtingHelper.comp"

in vec2 FragTexCoords;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoOpacity;
uniform sampler2D gMetallicRoughness;
uniform sampler2D ssao;
uniform sampler2D atlasShadowMap;
uniform sampler2D gEmission;

void main()
{
	vec3 normal = texture(gNormal, FragTexCoords).rgb;
	if(length(normal) <= 0.001)
		discard;

	vec3 fragPos = texture(gPosition, FragTexCoords).rgb;
    vec3 albedo = texture(gAlbedoOpacity, FragTexCoords).rgb;
	vec4 metallicRoughness = texture(gMetallicRoughness, FragTexCoords);
	float metallic = metallicRoughness.r;
	float roughness = metallicRoughness.g;
	float ambientOcclusion = clamp(min(texture(ssao, FragTexCoords).r, metallicRoughness.b), 0.f, 1.f);
	vec3 emission = texture(gEmission, FragTexCoords).rgb;

	vec3 lightColor = CalcLighting(camera.view, camera.position, camera.farPlane, atlasShadowMap, fragPos, normal,
									albedo, metallic, roughness, ambientOcclusion);

	FragColor = vec4(lightColor + emission, 1.0f);
	// FragColor = vec4(1,0,0, 1.0f);

	float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = vec4(FragColor.rgb, 1.0);
	else
		BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}
