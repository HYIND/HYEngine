#version 460 core

#include "shader/dataDef/materialDataDef.comp"

in vec3 FragPos;
in vec3 FragNormal;
in vec2 FragTextureCoords;
in mat3 TBN;
in vec2 MotionVector;
flat in int materialIndex;

layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoOpacity;
layout (location = 3) out vec4 gMetallicRoughness;
layout (location = 4) out vec2 gMotionVector;
layout (location = 5) out vec3 gEmission;

layout(std430, binding = 3) buffer MaterialDatas {
    MaterialData materialdata[];
};

vec3 calculateNormal(MaterialData material, vec3 vertexNormal, mat3 TBN, vec2 uv)
{
	return material.texture_normal_count > 0 ?
		normalize(TBN * normalize(texture(material.texture_normal, uv).rgb * 2.0 - 1.0))
		: normalize(vertexNormal);
}

float calculateOpacity(MaterialData material, vec2 uv)
{
	if (material.alphamode == 0)
		return material.opacity;

	float opacity = material.opacity;
	if (material.texture_opacity_count > 0)
		opacity *= texture(material.texture_opacity, uv).r;
	else 
		opacity *= texture(material.texture_albedo, uv).a;

	if (material.alphamode == 1)	//Mask
		opacity = opacity > material.maskthreshold ? 1.0f : 0.f;

	return opacity;
}

void getPBRProperties(MaterialData material, vec2 uv, inout vec3 albedo, inout float metallic, inout float roughness, inout float ambientOcclusion)
{
    albedo = material.texture_albedo_count > 0 ? 
            texture(material.texture_albedo, uv).rgb * material.albedo
            :material.albedo;

    ambientOcclusion = material.texture_ao_count > 0 ? 
                    texture(material.texture_ao, uv).r 
                    : material.ambientOcclusion;

	if (material.texture_metallicroughness_count > 0)
	{
        vec2 values = texture(material.texture_metallicroughness, uv).gb;
        metallic = values.x;
        roughness = values.y;
	}
    else if (material.texture_metallic_count > 0 && material.texture_roughness_count > 0 
            && uint64_t(material.texture_metallic) == uint64_t(material.texture_roughness))
    {
        vec2 values = texture(material.texture_metallic, uv).gb;
        metallic = values.x;
        roughness = values.y;
    }
	else 
	{
        metallic = material.texture_metallic_count > 0 ?
                texture(material.texture_metallic, uv).b
                : material.metallic;

        roughness = material.texture_roughness_count > 0 ? 
                texture(material.texture_roughness, uv).g
                : material.roughness;
	}
}

vec3 calculateEmission(MaterialData material, vec2 uv)
{
   	return material.texture_emissive_count > 0 ? 
            texture(material.texture_emissive, uv).rgb * material.emissionColor
            :material.emissionColor * material.emissionStrength;
}

void main()
{
	gPosition = FragPos;

	MaterialData material = materialdata[materialIndex];

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

	gNormal = normal;
	gAlbedoOpacity = vec4(albedo, opacity);
	gMetallicRoughness = vec4(metallic, roughness, ambientOcclusion, material.IOR);
	gMotionVector = MotionVector;
	gEmission = emission;
}
