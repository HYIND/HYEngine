#version 460 core

// 编辑器：把透明物体（AlphaMode::Blend）写进实体 ID 缓冲（GeometryPass 画完不透明物体之后，见 GeometryPass::RenderTransparentEntityIds）。
// 顶点着色器复用 geometrypass_StaticMesh.vs / geometrypass_SkinnedMesh.vs（带 EDITOR_ENTITY_ID，输出 EntityId = entityId + 1）。
// 深度：用不透明深度的一份拷贝做测试并写入，所以留下的是鼠标下最近的那个透明面；它在不透明物体前面时覆盖不透明物体的 ID。
// 太透明的地方（< EDITOR_TRANSPARENT_PICK_OPACITY）不算物体，点得穿

#include "shader/dataDef/MaterialTextureDef.comp"

// 实际值由 GeometryPass 注入（GeometryPass::TransparentPickOpacity），这里只是没注入时的默认值
#ifndef EDITOR_TRANSPARENT_PICK_OPACITY
#define EDITOR_TRANSPARENT_PICK_OPACITY 0.02
#endif

layout (location = 2) in vec2 FragTextureCoords;
layout (location = 7) flat in uint materialIndex;
layout (location = 9) flat in uint EntityId;

layout (location = 0) out uint gEntityId;

void main()
{
	MaterialData material = materials[materialIndex];
	if (calculateOpacity(material, FragTextureCoords) < EDITOR_TRANSPARENT_PICK_OPACITY)
		discard;
	gEntityId = EntityId;
}
