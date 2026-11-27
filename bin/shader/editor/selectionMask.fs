#version 460 core

// 编辑器选中遮罩（EditorSelectionMaskPass）。顶点着色器复用 geometrypass_StaticMesh.vs / geometrypass_SkinnedMesh.vs，
// 带 EDITOR_ENTITY_ID 宏编译（输出 EntityId = entityId + 1）。没有深度测试，画出选中物体的完整轮廓。
// 输出（混合 = max，多个选中物体重叠时取并集）：
//   r = 1：被选中物体覆盖
//   g = 1：这个像素上看得见的就是它自己（实体 ID 缓冲里是它），0 = 被别的东西挡住（包括挡在前面的透明物体）
//   b = 1：是主选中

#include "shader/dataDef/MaterialTextureDef.comp"

layout (location = 2) in vec2 FragTextureCoords;
layout (location = 7) flat in uint materialIndex;
layout (location = 9) flat in uint EntityId;

layout (location = 0) out vec4 outMask;

layout(set = 0, binding = 5) uniform usampler2D entityIdTex;	// GeometryPass 的实体 ID 缓冲（entityId + 1，0 = 没有物体）

layout(push_constant) uniform MaskParams
{
	uint primaryId;				// 主选中的 entityId + 1，0 = 不区分
	float opacityThreshold;		// 材质透明度低于它的地方不算覆盖：不透明 0.01（同 G-Buffer），透明 = GeometryPass::TransparentPickOpacity
} params;

void main()
{
	// 和写实体 ID 时的规则一致：镂空 / 太透明的地方不算物体
	MaterialData material = materials[materialIndex];
	if (calculateOpacity(material, FragTextureCoords) < params.opacityThreshold)
		discard;

	uint visibleId = texelFetch(entityIdTex, ivec2(gl_FragCoord.xy), 0).r;
	float visible = (visibleId == EntityId) ? 1.0 : 0.0;
	float primary = (params.primaryId != 0u && EntityId == params.primaryId) ? 1.0 : 0.0;
	outMask = vec4(1.0, visible, primary, 1.0);
}
