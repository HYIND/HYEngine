#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

// 本批次用到的贴图；纯色块用 1x1 白图
layout(set = 0, binding = 1) uniform sampler2D Textures[];

layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec4 FragColor;
layout(location = 2) flat in uint FragTexIndex;

layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 color = texture(Textures[nonuniformEXT(FragTexIndex)], FragUV) * FragColor;

#ifdef BLEND_MULTIPLY
	// 正片叠底：混合方程是 dst * src，透明处按 1 处理（不影响底图）
	color.rgb = mix(vec3(1.0), color.rgb, color.a);
#endif

	OutColor = color;
}
