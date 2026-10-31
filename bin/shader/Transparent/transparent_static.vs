#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 FragNormal;
layout (location = 2) out vec2 FragTextureCoords;
layout (location = 3) out mat3 TBN;
layout (location = 7) flat out uint materialIndex;

#include "shader/dataDef/camerauboDef.comp"

struct RenderData
{
	mat4 model;
	mat4 prevModel;
	uint materialIndex;
};

layout(set = 0, binding = 2) readonly buffer RenderDatas
{
	RenderData renderdata[];
};

void main()
{
	mat4 model = renderdata[gl_BaseInstance].model;
	materialIndex = renderdata[gl_BaseInstance].materialIndex;

	vec4 worldPos = model * vec4(aPos, 1.0f);
	FragPos = worldPos.xyz;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 T = normalize(normalMatrix * aTangent);
	vec3 B = normalize(normalMatrix * aBitangent);
	vec3 N = normalize(normalMatrix * aNormal);

	TBN = mat3(T, B, N);
	FragNormal = N;
	FragTextureCoords = aTexCoords;
	gl_Position = camera.projView * worldPos;
}
