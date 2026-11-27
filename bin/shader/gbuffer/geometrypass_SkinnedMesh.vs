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
// 运动矢量在片元着色器里算：透视除法之后的屏幕坐标对三角形不是线性的，
// 在顶点里先除再插值，靠近相机的大三角形（地面）会偏。这里只传裁剪坐标，插值后再除。
layout (location = 6) out vec4 CurClipPos;
layout (location = 8) out vec4 PrevClipPos;
layout (location = 7) flat out uint materialIndex;
#ifdef EDITOR_ENTITY_ID
// 编辑器的实体 ID 缓冲：entityId + 1（UINT32_MAX 溢出成 0 = 没有物体）
layout (location = 9) flat out uint EntityId;
#endif

#include "shader/dataDef/camerauboDef.comp"

// C++ 镜像：RenderState.h 的 GeometryRenderData（std430，144 字节）
struct RenderData
{
	mat4 model;
	mat4 prevModel;
	uint materialIndex;
	uint entityId;
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
#ifdef EDITOR_ENTITY_ID
	EntityId = renderdata[instanceIndex].entityId + 1u;
#endif

	vec4 worldPos = model * vec4(aPos, 1.0f);
	FragPos = worldPos.xyz;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 T = normalize(normalMatrix * aTangent	);
	vec3 B = normalize(normalMatrix * aBitangent	);
	vec3 N = normalize(normalMatrix * aNormal		);

	vec4 clipPos = camera.projView * worldPos;

	// 上一帧蒙皮顶点不可用时，上一帧裁剪坐标取本帧的，运动矢量为 0（与原来一致）
	CurClipPos = clipPos;
	PrevClipPos = clipPos;

	SkinnedMotionData motion = motiondata[instanceIndex];
	if (motion.enabled != 0)
	{
		uint localIndex = uint(gl_VertexIndex) - motion.vertexOffset;
		if (localIndex < motion.vertexCount)
		{
			vec3 prevPos = prevVertices[motion.prevVertexOffset + localIndex].position.xyz;
			vec4 prevWorldPos = prevModel * vec4(prevPos, 1.0f);
			PrevClipPos = prevcamera.projView * prevWorldPos;
		}
	}

	TBN = mat3(T, B, N);
	FragNormal = N;
	gl_Position = clipPos;
	FragTextureCoords = aTexCoords;
}
