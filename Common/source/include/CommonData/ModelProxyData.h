#pragma once
#include <glm/glm.hpp>

// 模型代理，提供外部访问部分模型数据的功能，具体实现由Render提供
class ModelProxyData
{
public:
	struct AABB
	{
		glm::vec3 min = glm::vec3(0.f);
		glm::vec3 max = glm::vec3(0.f);
	};

public:
	virtual AABB GetAABB() { return AABB(); };
};
