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
layout (location = 6) out vec2 MotionVector;
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

struct SkinnedMotionData
{
	uint enabled;			// 1 = 上一帧的蒙皮顶点可用
	uint vertexOffset;		// 本帧区间在本帧蒙皮顶点缓冲里的起点
	uint prevVertexOffset;	// 上一帧区间在上一帧蒙皮顶点缓冲里的起点
	uint vertexCount;		// 顶点数量
};

layout(set = 0, binding = 3) readonly buffer SkinnedMotionDatas
{
	SkinnedMotionData motiondata[];
};

struct SkinnedVertex
{
	vec4 position;
	vec4 normal;
	vec4 texCoords;
	vec4 tangent;
	vec4 bitangent;
};

layout(set = 0, binding = 4) readonly buffer PrevSkinnedVertices
{
	SkinnedVertex prevVertices[];
};

void main()
{
	uint instanceIndex = uint(gl_BaseInstance);
	mat4 model = renderdata[instanceIndex].model;
	mat4 prevModel = renderdata[instanceIndex].prevModel;
	materialIndex = renderdata[instanceIndex].materialIndex;

	vec4 worldPos = model * vec4(aPos, 1.0f);
	FragPos = worldPos.xyz;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 T = normalize(normalMatrix * aTangent	);
	vec3 B = normalize(normalMatrix * aBitangent	);
	vec3 N = normalize(normalMatrix * aNormal		);

	vec4 clipPos = camera.projView * worldPos;

	MotionVector = vec2(0.0);

	SkinnedMotionData motion = motiondata[instanceIndex];
	if (motion.enabled != 0)
	{
		uint localIndex = uint(gl_VertexIndex) - motion.vertexOffset;
		if (localIndex < motion.vertexCount)
		{
			vec3 prevPos = prevVertices[motion.prevVertexOffset + localIndex].position.xyz;
			vec4 prevWorldPos = prevModel * vec4(prevPos, 1.0f);
			vec4 prevClipPos = prevcamera.projView * prevWorldPos;

			vec2 uv_curr = clipPos.xy / clipPos.w * 0.5 + 0.5;
			vec2 uv_prev = prevClipPos.xy / prevClipPos.w * 0.5 + 0.5;
			MotionVector = uv_curr - uv_prev;
		}
	}

	TBN = mat3(T, B, N);
	FragNormal = N;
	gl_Position = clipPos;
	FragTextureCoords = aTexCoords;
}
