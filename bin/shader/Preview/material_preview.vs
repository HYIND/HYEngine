#version 460 core

// 材质预览（MaterialPreviewPass）：单个物体的前向着色，物体固定在原点、模型矩阵为单位阵

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 FragNormal;
layout (location = 2) out vec2 FragTextureCoords;
layout (location = 3) out mat3 TBN;

// 和 material_preview.fs、MaterialPreviewPass.cpp 的 PreviewPushConstants 保持一致
layout (push_constant) uniform PreviewParams
{
	mat4 viewProj;
	vec4 cameraPos;		// xyz：相机位置
	vec4 params;		// x：曝光，yzw：保留
	vec4 keyLightDir;	// xyz：主光方向（指向光源）
	vec4 rimLightDir;	// xyz：轮廓光方向（指向光源）
} pc;

void main()
{
	FragPos = aPos;

	vec3 N = normalize(aNormal);
	vec3 T = normalize(aTangent);
	vec3 B = normalize(aBitangent);

	TBN = mat3(T, B, N);
	FragNormal = N;
	FragTextureCoords = aTexCoords;
	gl_Position = pc.viewProj * vec4(aPos, 1.0);
}
