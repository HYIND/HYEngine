#include "RenderEngine/RenderFrameData.h"

using namespace Render;

void Render3DFrameData::reset()
{
	Scene3D_Contexts.clear();

	skybox.reset();
	ddgiVolumes.clear();

	projection = glm::mat4(1.0f);
	view = glm::mat4(1.0f);
	position = glm::vec3(0.0f);
	direction = glm::vec3(0.0f, 0.0f, -1.0f);
	directionUp = glm::vec3(0.0f, 1.0f, 0.0f);
	directionRight = glm::vec3(1.0f, 0.0f, 0.0f);
	nearPlane = 0.1f;
	farPlane = 500.f;
	fov = 80.f;
}

void RenderUIFrameData::reset()
{
	views.clear();
}
