#pragma once

#include <stdint.h>
#include <iostream>
#include <glm/glm.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace Tool
{
	std::string AnsiToUTF8(const std::string& ansiStr);
	std::string WStringToUTF8(const std::wstring& wstr);
	std::wstring UTF8ToWString(const std::string& str);

	int64_t GetTimestampMircoseconds();
	int64_t GetTimestampMilliseconds();
	int64_t GetTimestampSecond();

	std::string GetFormatSecondStr(int64_t timestamp_seconds);

	std::string GenerateSimpleUuid();

	float RadianToAngle(float radian);
	float AngleToRadian(float angle);

	glm::quat SafeQuatLookAt(const glm::vec3& direction);

	glm::quat RandomSpread(const glm::quat& baseQuat, float spreadAngle);

	glm::vec3 RandomLinear(const glm::vec3& minVal, const glm::vec3& maxVal);
	float RandomLinear(float minVal, float maxVal);

	float LinearLerp(float value1, float value2, float t);
	glm::vec3 LinearLerp(const glm::vec3& value1, const glm::vec3& value2, float t);

	glm::quat GetQuatFromRotate(float angle, const glm::vec3& axis);
	glm::vec3 GetDirectionFromRotate(const glm::quat& q);

	// 将色温（开尔文）转换为线性 RGB 值
	// 输入: temperature (K), 有效范围 1000K ~ 40000K
	// 输出: R, G, B (线性空间, 范围 0.0 ~ 1.0)
	glm::vec3 ColorTemperatureToRGB(float temperature);

	// 父子路径判断，
	// 如果child或者parent是相对目录，基于base拼接成完整的绝对路径
	bool IsSubDirectory(const fs::path& child, const fs::path& parent, const fs::path& base = fs::current_path());

	// 直接父子判断
	bool IsImmediateSubDirectory(const fs::path& child, const fs::path& parent, const fs::path& base = fs::current_path());
}

