#version 460 core

// Weighted Blended OIT 的第二段：把累积缓冲还原成「近似正确顺序」的 alpha 颜色，
// 再用普通的 srcAlpha / oneMinusSrcAlpha 混到场景色上。

layout(set = 0, binding = 1) uniform sampler2D oitAccumTexture;
layout(set = 0, binding = 2) uniform sampler2D oitRevealTexture;

layout(location = 0) in vec2 FragUV;

layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 accum = texture(oitAccumTexture, FragUV);
	float reveal = texture(oitRevealTexture, FragUV).r;

	vec3 color = accum.rgb / max(accum.a, 1e-4);	// 加权平均
	float alpha = clamp(1.0 - reveal, 0.0, 1.0);	// 透过率取反 = 覆盖度

	if (alpha <= 1e-4)
		discard;

	OutColor = vec4(color, alpha);
}
