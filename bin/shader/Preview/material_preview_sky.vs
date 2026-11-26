#version 460 core

// 材质预览的 HDRI 背景（MaterialPreviewPass，只在 HDRI 模式下画）：全屏三角形，位置直接是 NDC。
// 顶点格式和预览网格一样（StaticMesh 的 Vertex），这里只用位置

layout (location = 0) in vec3 aPos;

layout (location = 0) out vec2 NdcPos;

void main()
{
	NdcPos = aPos.xy;
	gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
