#include "RenderEngine/RenderFrameManager.h"

using namespace Render;

bool D2DRenderContext::RenderContext::operator<(const D2DRenderContext::RenderContext& other) const
{
	if (layer != other.layer)
		return layer == other.layer;
	return internalZOrder < other.internalZOrder;
}

void RenderFrameData::reset()
{
	D2D_Contexts.clear();
	GL_Contexts.clear();

	projection = glm::mat4(1.0f);
	view = glm::mat4(1.0f);
	position = glm::vec3(0.0f);
	nearPlane = 0.1f;
	farPlane = 500.f;
	fov = 80.f;
}
