#version 460 core

#include "shader/dataDef/camerauboDef.comp"

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

out vec3 FragPos;
out vec3 FragNormal;	
out vec2 FragTextureCoords;
out mat3 TBN;
out vec2 MotionVector;
flat out int materialIndex;

struct RenderData
{
	mat4 model;
	mat4 prevModel;
	int materialIndex;
};

layout(std430, binding = 2) buffer RenderDatas
{
	RenderData renderdata[];
};

// uniform mat4 model;
// uniform mat4 prevModel;

void main()
{
	FragTextureCoords = aTexCoords;

	mat4 model = renderdata[gl_BaseInstance].model;
	mat4 prevModel = renderdata[gl_BaseInstance].prevModel;
	materialIndex = renderdata[gl_BaseInstance].materialIndex;

    vec4 worldPos = model * vec4(aPos, 1.0f);
	FragPos = worldPos.xyz;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 T = normalize(normalMatrix * aTangent  	);
	vec3 B = normalize(normalMatrix * aBitangent	);
	vec3 N = normalize(normalMatrix * aNormal		);

	TBN = mat3(T, B, N);
	FragNormal = N;

	vec4 clipPos = camera.projView * worldPos;
	gl_Position = clipPos;

	vec4 prevWorldPos = prevModel * vec4(aPos, 1.0f);
	vec4 prevClipPos = prevcamera.projView * prevWorldPos;

	// 转 NDC 算 UV 差值
    vec2 uv_curr = clipPos.xy / clipPos.w * 0.5 + 0.5;
    vec2 uv_prev = prevClipPos.xy / prevClipPos.w * 0.5 + 0.5;
    MotionVector = uv_curr - uv_prev;
}
