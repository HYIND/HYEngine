#pragma once

#include <stdint.h>
#include <iostream>
#include "Net/Helper/Buffer.h"
#include "nlohmann/json.hpp"
#include <glm/glm.hpp>

using json = nlohmann::json;

namespace Tool
{
	std::string WStringToUTF8(const std::wstring& wstr);
	std::wstring UTF8ToWString(const std::string& str);

	int64_t GetTimestampMircoseconds();
	int64_t GetTimestampMilliseconds();
	int64_t GetTimestampSecond();

	std::string GetFormatSecondStr(int64_t timestamp_seconds);

	std::string GenerateSimpleUuid();

	json ParseJson(const Buffer& buf);

	float RadianToAngle(float radian);
	float AngleToRadian(float angle);


	glm::quat SafeQuatLookAt(const glm::vec3& direction);

	glm::quat RandomSpread(const glm::quat& baseQuat, float spreadAngle);

	glm::vec3 RandomLinear(const glm::vec3& minVal, const glm::vec3& maxVal);
	float RandomLinear(float minVal, float maxVal);

	float LinearLerp(float value1, float value2, float t);
	glm::vec3 LinearLerp(const glm::vec3& value1, const glm::vec3& value2, float t);

	glm::quat getQuatFromRotate(float angle, const glm::vec3& axis);
}
