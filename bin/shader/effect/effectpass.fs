#version 430 core

#include "shader/dataDef/materialuboDef.comp"
#include "shader/dataDef/camerauboDef.comp"

out vec4 FragColor;

in vec3 FragPos;
in vec3 LocalFragPos;
in vec3 FragNormal;
in vec2 FragTextureCoords;
in mat3 TBN;


struct Color_Texture_Data
{
	vec3 basecolor;
	float opacity;

	sampler2D texture;
	bool textureEnable;
};

struct Laser_Beam_Data
{
	vec3 color;
	float white_width;
	float color_width;
};

uniform Color_Texture_Data color_texture;
uniform Laser_Beam_Data laser_beam;

uniform int effectType;
uniform int particleType;
uniform int particleShape;

uniform vec3 particleSize;
uniform vec3 particlePos;

uniform mat4 model;

vec4 calculateColor();
vec4 calculateParticle();
vec4 calculateLaserBeam();

void main()
{
	vec4 Color = calculateColor();
	FragColor = Color;
}

vec4 calculateColor()
{
	if(effectType == 0)
		return calculateParticle();
	else if (effectType == 1)
		return calculateLaserBeam();

	return vec4(0.f);
}

vec4 calculateParticle()
{
	if(particleType == 0 || particleType == 1)
	{
		vec4 diffuse = vec4(0);

		if(particleType == 0)
		{
			diffuse = vec4(color_texture.basecolor, color_texture.opacity);
		}
		else if(particleType == 1)
		{
			if(color_texture.textureEnable)
				diffuse = texture(color_texture.texture, FragTextureCoords).rgba * vec4(color_texture.basecolor, color_texture.opacity);
			else 
				diffuse = vec4(color_texture.basecolor, color_texture.opacity);
		}

		if(particleShape == 0)
		{
			vec4 origin = model * vec4(0,0,0,1);
			vec3 localPos = FragPos - particlePos;
			vec3 normalizedPos = localPos / particleSize;
			float normalizedDist = length(normalizedPos);
			if(normalizedDist >= 1.f || normalizedDist <= 0.f)
				discard;
			return diffuse;
		}
		else if(particleShape == 1)
		{
			vec4 origin = model * vec4(0,0,0,1);
			vec3 localPos = FragPos - particlePos;
			vec3 normalizedPos = localPos / particleSize;
			float normalizedDist = length(normalizedPos);
			if(normalizedDist >= 1.f || normalizedDist <= 0.f)
				discard;
			return vec4(diffuse.rgb, diffuse.a * (1.0f - normalizedDist));
		}
		else if(particleShape == 2)
		{
			return diffuse;
		}
	}
	else if (particleType == 2)
	{
		if(material.texture_albedo_count > 0)
		{
			vec3 albedo = texture(material.texture_albedo, FragTextureCoords).rgb;
			return vec4(albedo, material.opacity) * vec4(color_texture.basecolor, color_texture.opacity);
		}
		return vec4(material.albedo, material.opacity) * vec4(color_texture.basecolor, color_texture.opacity);
	}

	return vec4(color_texture.basecolor, color_texture.opacity); 
}

vec4 calculateLaserBeam()
{
	if(material.texture_albedo_count > 0)
	{
		vec3 albedo = texture(material.texture_albedo, FragTextureCoords).rgb;
		return vec4(albedo, material.opacity) * vec4(laser_beam.color, 1.0);
	}
	return vec4(laser_beam.color, 1.0);
}