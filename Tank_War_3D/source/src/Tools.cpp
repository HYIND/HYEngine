#include "Helper/Tools.h"
#include <chrono>
#include <random>

#define _USE_MATH_DEFINES
#include <math.h>

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include <codecvt>
#include <locale>
#include <format>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/random.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>


std::string Tool::WStringToUTF8(const std::wstring& wstr) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.to_bytes(wstr);
}

std::wstring Tool::UTF8ToWString(const std::string& str) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.from_bytes(str);
}

int64_t Tool::GetTimestampMircoseconds()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

int64_t Tool::GetTimestampMilliseconds()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int64_t Tool::GetTimestampSecond()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

std::string Tool::GetFormatSecondStr(int64_t timestamp_seconds)
{
	auto tp = std::chrono::time_point_cast<std::chrono::seconds>(
		std::chrono::system_clock::from_time_t(
			static_cast<std::time_t>(timestamp_seconds)
		)
	);
	return std::format("{:%Y-%m-%d %H:%M:%S}", tp);
}

std::string Tool::GenerateSimpleUuid()
{
	// 获取当前时间戳（毫秒精度）
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

	// 初始化随机数生成器
	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_int_distribution<uint16_t> dis(0, 0xFFFF);

	// 分解时间戳
	uint32_t time_low = static_cast<uint32_t>(millis & 0xFFFFFFFF);     // 低32位
	uint16_t time_mid = static_cast<uint16_t>((millis >> 32) & 0xFFFF); // 中16位
	uint16_t time_hi = static_cast<uint16_t>((millis >> 48) & 0x0FFF);  // 高12位

	// 生成4位随机数 (16位)
	uint16_t rand_num = dis(gen) & 0xFFFF;

	// 组合成UUID格式
	std::stringstream ss;
	ss << std::hex << std::setfill('0')
		<< std::setw(8) << time_low                         /* << "-" */
		<< std::setw(4) << time_mid                         /* << "-" */
		<< std::setw(4) << time_hi                          /* << "-" */
		<< std::setw(4) << (rand_num & 0x0FFF) /* << "-" */ // 使用12位随机数
		<< std::setw(4) << (rand_num >> 4);                 // 使用剩余4位

	return ss.str();
}

json Tool::ParseJson(const Buffer& buf)
{
	json result;
	std::string js_str(buf.Byte(), buf.Length());
	try
	{
		result = json::parse(js_str);
	}
	catch (...)
	{
		std::cout << "Tool ParseJson error : " << js_str << "\n";
	}
	return result;
}

float Tool::RadianToAngle(float radian)
{
	return radian / M_PI * 180.f;
}

float Tool::AngleToRadian(float angle)
{
	return angle / 180.f * M_PI;
}

glm::quat Tool::RandomSpread(const glm::quat& baseQuat, float spreadAngle) {
	static std::random_device rd;
	static std::mt19937 gen(rd());
	std::uniform_real_distribution<float> disAngle(0.0f, 2.0f * glm::pi<float>());
	std::uniform_real_distribution<float> disRadius(0.0f, 1.0f);

	float spreadRadians = glm::radians(spreadAngle);

	float azimuth = disAngle(gen);
	float r = disRadius(gen);
	float theta = glm::acos(1.0f - r * (1.0f - glm::cos(spreadRadians)));

	// 局部坐标系中的偏移（相对于 +Z）
	glm::vec3 localDir = glm::normalize(glm::vec3(
		glm::sin(theta) * glm::cos(azimuth),
		glm::sin(theta) * glm::sin(azimuth),
		glm::cos(theta)
	));

	// 构造从 +Z 到 localDir 的旋转（在局部坐标系中）
	glm::quat deltaQuat = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), localDir);

	// 组合：先应用 baseQuat，再应用 deltaQuat（局部旋转）
	// 对于四元数，右乘表示在局部坐标系中旋转
	glm::quat resultQuat = baseQuat * deltaQuat;

	return resultQuat;
}

glm::quat Tool::SafeQuatLookAt(const glm::vec3& direction)
{
	glm::vec3 dir = glm::normalize(direction);

	if (glm::abs(dir.y) > 0.9999f) {
		glm::vec3 up = (dir.y > 0) ?
			glm::vec3(0.0f, 0.0f, -1.0f) :
			glm::vec3(0.0f, 0.0f, 1.0f);
		return glm::quatLookAt(dir, up);
	}

	return glm::quatLookAt(dir, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::vec3 Tool::RandomLinear(const glm::vec3& minVal, const glm::vec3& maxVal)
{
	float t = glm::linearRand(0.f, 1.f);
	return minVal + t * (maxVal - minVal);
}

float Tool::RandomLinear(float minVal, float maxVal)
{
	return glm::linearRand(minVal, maxVal);
}

float Tool::LinearLerp(float value1, float value2, float t)
{
	return glm::mix(value1, value2, 1.f - t);
}

glm::vec3 Tool::LinearLerp(const glm::vec3& value1, const glm::vec3& value2, float t)
{
	return glm::mix(value1, value2, 1.f - t);
}

glm::quat Tool::getQuatFromRotate(float angle, const glm::vec3& axis)
{
	return glm::quat_cast(glm::rotate(glm::mat4(1.0f), glm::radians(angle), axis));
}
