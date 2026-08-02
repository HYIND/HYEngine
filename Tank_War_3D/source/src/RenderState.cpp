#include "OpenGLRenderEngine/General/RenderState.h"

Plane::Plane(const glm::vec3& pointOnPlane, const glm::vec3& n)
	: normal(glm::normalize(n))
	, distance(-glm::dot(normal, pointOnPlane))
{
}

Plane::Plane(const glm::vec3& n, float d)
	: normal(glm::normalize(n))
	, distance(d)
{
}

float Plane::getSignedDistanceToPlane(const glm::vec3& point) const {
	return glm::dot(normal, point) + distance;
}

inline glm::vec4 GetRow(const glm::mat4& m, int row) {
	return glm::vec4(m[0][row], m[1][row], m[2][row], m[3][row]);
}

Frustum::Frustum(const glm::mat4& viewProj)
{
	// 提取各行
	glm::vec4 row0 = GetRow(viewProj, 0);
	glm::vec4 row1 = GetRow(viewProj, 1);
	glm::vec4 row2 = GetRow(viewProj, 2);
	glm::vec4 row3 = GetRow(viewProj, 3);

	auto GetPlane = [&](glm::vec4 coeff) -> Plane
		{
			glm::vec3 normal = glm::vec3(coeff);
			float len = glm::length(normal);
			if (len > 0.0f) coeff /= len;
			return Plane(coeff, coeff.w);
		};

	leftFace = GetPlane(row3 + row0);
	rightFace = GetPlane(row3 - row0);
	bottomFace = GetPlane(row3 + row1);
	topFace = GetPlane(row3 - row1);
	nearFace = GetPlane(row3 + row2);
	farFace = GetPlane(row3 - row2);

	// 8 个 NDC 角点（OpenGL：[-1,1]）
	std::array<glm::vec4, 8> ndcCorners = {
		glm::vec4(-1, -1, -1, 1),
		glm::vec4(-1, -1,  1, 1),
		glm::vec4(-1,  1, -1, 1),
		glm::vec4(-1,  1,  1, 1),
		glm::vec4(1, -1, -1, 1),
		glm::vec4(1, -1,  1, 1),
		glm::vec4(1,  1, -1, 1),
		glm::vec4(1,  1,  1, 1)
	};

	glm::mat4 invViewProj = glm::inverse(viewProj);

	for (int i = 0; i < 8; ++i)
	{
		glm::vec4 world = invViewProj * ndcCorners[i];
		world /= world.w;  // 透视除法
		corners[i] = glm::vec3(world);
	}

}

inline bool IsAABBOnOrForwardPlane(const glm::vec3& center, const glm::vec3& extents, const Plane& plane)
{
	// 投影半径：AABB 在法线方向上最远能伸展多远
	float r = extents.x * std::abs(plane.normal.x)
		+ extents.y * std::abs(plane.normal.y)
		+ extents.z * std::abs(plane.normal.z);
	// 中心到平面的有符号距离 s，若 s > -r 则不剔除
	return plane.getSignedDistanceToPlane(center) > -r;
}

bool Frustum::IsAABBOnFrustum(AABB& aabb)
{
	glm::vec3 extents = aabb.max - aabb.min;
	glm::vec3 center = aabb.min + extents * 0.5f;
	return IsAABBOnOrForwardPlane(center, extents, nearFace)
		&& IsAABBOnOrForwardPlane(center, extents, farFace)
		&& IsAABBOnOrForwardPlane(center, extents, leftFace)
		&& IsAABBOnOrForwardPlane(center, extents, rightFace)
		&& IsAABBOnOrForwardPlane(center, extents, topFace)
		&& IsAABBOnOrForwardPlane(center, extents, bottomFace);
}

bool Frustum::IsSphereOnFrustum(const glm::vec3& center, float radius)
{
	for (auto& plane : { nearFace ,farFace ,rightFace ,leftFace ,topFace ,bottomFace })
	{
		float dist = plane.getSignedDistanceToPlane(center);
		if (dist < -radius)
			return false;
	}
	return true;
}

bool Frustum::IsPointOnFrustum(const glm::vec3& pos)
{
	for (auto& plane : { nearFace ,farFace ,rightFace ,leftFace ,topFace ,bottomFace })
	{
		float dist = plane.getSignedDistanceToPlane(pos);
		if (dist < 0)
			return false;
	}
	return true;
}

bool Frustum::IsIntersectsFrustum(Frustum& other)
{
	for (auto& pos : other.corners)
	{
		if (IsPointOnFrustum(pos))
			return true;
	}

	for (auto& pos : corners)
	{
		if (other.IsPointOnFrustum(pos))
			return true;
	}

	return false;
}


RenderStateBuilder& RenderStateBuilder::SetCamera(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& dirUp, const glm::vec3& dirRight, float nearP, float farP, float fov)
{
	context.camera.projection = proj;
	context.camera.view = view;
	context.camera.position = pos;
	context.camera.direction = dir;
	context.camera.directionUp = dirUp;
	context.camera.directionRight = dirRight;
	context.camera.nearPlane = nearP;
	context.camera.farPlane = farP;
	context.camera.fov = fov;
	context.camera.frustum = Frustum(proj * view);
	return *this;
}

RenderState RenderStateBuilder::Build() { return std::move(context); }

