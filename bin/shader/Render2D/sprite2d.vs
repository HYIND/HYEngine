#version 460 core

// Renderer2D：每个实例一个四边形，顶点由 gl_VertexIndex 生成，不需要顶点缓冲。
// corner(u, v) = origin + axisX * u + axisY * v；pivot / 尺寸 / 缩放 / 翻转 / 旋转都已在 CPU 端折算好。

struct SpriteInstance
{
	vec2 origin;
	vec2 axisX;
	vec2 axisY;
	uint texIndex;
	uint flags;
	vec4 uvRect;		// u0, v0, u1, v1
	vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer SpriteInstances
{
	SpriteInstance instances[];
};

layout(push_constant) uniform Param
{
	mat4 viewProj;		// 2D 世界（y 朝下）-> 裁剪空间
	vec2 targetSize;	// 像素
	uint frameFlags;	// bit0：顶点吸附到整像素
	uint padding;
} param;

layout(location = 0) out vec2 FragUV;
layout(location = 1) out vec4 FragColor;
layout(location = 2) flat out uint FragTexIndex;

const vec2 kCorners[6] = vec2[6](
	vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
	vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main()
{
	SpriteInstance s = instances[gl_InstanceIndex];
	vec2 corner = kCorners[gl_VertexIndex];

	vec2 world = s.origin + s.axisX * corner.x + s.axisY * corner.y;
	vec4 clip = param.viewProj * vec4(world, 0.0, 1.0);

	if ((param.frameFlags & 1u) != 0u)
	{
		vec2 pixel = (clip.xy * 0.5 + 0.5) * param.targetSize;
		pixel = floor(pixel + 0.5);
		clip.xy = pixel / param.targetSize * 2.0 - 1.0;
	}

	gl_Position = vec4(clip.xy, 0.0, 1.0);
	FragUV = mix(s.uvRect.xy, s.uvRect.zw, corner);
	FragColor = s.color;
	FragTexIndex = s.texIndex;
}
