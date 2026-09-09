#include "glm/glm.hpp"

glm::mat4 vkPerspective(float radians, float aspect, float nearplane, float farplane);

glm::mat4 vkOrtho(float left, float right, float bottom, float top, float zNear, float zFar);
