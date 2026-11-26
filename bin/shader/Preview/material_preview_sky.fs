#version 460 core

// 材质预览的 HDRI 背景：按像素方向采样背景 cube，曝光 / ACES / gamma 和 material_preview.fs 完全一致

layout (location = 0) in vec2 NdcPos;

layout (set = 0, binding = 0) uniform samplerCube backgroundCube;

// 和 MaterialPreviewPass.cpp 的 SkyPushConstants 一致
layout (push_constant) uniform SkyParams
{
	mat4 invViewProjRotation;	// (投影 × 只有旋转的视图矩阵) 的逆
	vec4 params;				// x：曝光，y：环境旋转（弧度），z：环境亮度，w：背景 mip 级
} pc;

layout (location = 0) out vec4 FragColor;

vec3 ToEnvironment(vec3 dir)
{
	float s = sin(pc.params.y);
	float c = cos(pc.params.y);
	return vec3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);
}

vec3 ACESFilm(vec3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
	vec4 farPoint = pc.invViewProjRotation * vec4(NdcPos, 1.0, 1.0);
	vec3 dir = normalize(farPoint.xyz / farPoint.w);

	vec3 color = textureLod(backgroundCube, ToEnvironment(dir), pc.params.w).rgb * max(pc.params.z, 0.0);

	float exposure = pc.params.x > 0.0 ? pc.params.x : 1.0;
	color = ACESFilm(color * exposure);
	color = pow(color, vec3(1.0 / 2.2));
	FragColor = vec4(color, 1.0);
}
