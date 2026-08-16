#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

out vec3 FragPos;
out vec3 LocalFragPos;
out vec3 FragNormal;	
out vec2 FragTextureCoords;
out mat3 TBN;

#include "shader/dataDef/camerauboDef.comp"

uniform mat4 model;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);

	LocalFragPos = aPos;
	FragPos = worldPos.xyz;
	FragNormal = normalize(mat3(transpose(inverse(model))) * normalize(aNormal));
	FragTextureCoords = aTexCoords;

	vec3 T = normalize(vec3(model * vec4(aTangent,   0.0)));
	vec3 B = normalize(vec3(model * vec4(aBitangent, 0.0)));
	vec3 N = normalize(vec3(model * vec4(aNormal,    0.0)));
	TBN = mat3(T, B, N);

	gl_Position = camera.projection * camera.view * worldPos;
} 
