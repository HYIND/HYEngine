#include "Helper/Tools.h"
#include <chrono>
#include <random>
#include <atomic>
#include <cstdio>

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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif


std::string Tool::ToLower(const std::string& str) {
	std::string result = str;
	std::transform(result.begin(), result.end(), result.begin(), ::tolower);
	return result;
}

std::string Tool::ToUpper(const std::string& str) {
	std::string result = str;
	std::transform(result.begin(), result.end(), result.begin(), ::toupper);
	return result;
}

std::string Tool::AnsiToUTF8(const std::string& str)
{
	if (str.empty()) return "";

#ifdef _WIN32
	// 尝试用 CP_UTF8 直接转换（如果是 UTF-8，不会出错）
	int wideLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (wideLen > 0) {
		std::wstring wstr(wideLen, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wstr.data(), wideLen);

		int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (utf8Len > 0) {
			std::string utf8Str(utf8Len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, utf8Str.data(), utf8Len, nullptr, nullptr);
			if (!utf8Str.empty() && utf8Str.back() == '\0') utf8Str.pop_back();
			return utf8Str;
		}
	}

	// 如果 CP_UTF8 失败，尝试 CP_ACP（ANSI）
	wideLen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
	if (wideLen <= 0) return str;

	std::wstring wstr(wideLen, L'\0');
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, wstr.data(), wideLen);

	int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (utf8Len <= 0) return str;

	std::string utf8Str(utf8Len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, utf8Str.data(), utf8Len, nullptr, nullptr);

	if (!utf8Str.empty() && utf8Str.back() == '\0') {
		utf8Str.pop_back();
	}
	return utf8Str;
#else
	return str;
#endif
}

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
	// 格式：24 位十六进制 = [毫秒时间戳 12 位][序号 8 位][随机数 4 位]
	//   时间戳：高位在前，字符串按字典序比较就是按创建时间排序；前 12 位转成十进制即毫秒时间戳。
	//           （原来先写低 32 位再写高位，按字符串排序会乱；原来的 time_hi 对当前时间恒为 0，这里并进时间戳）
	//   序号：  进程内全局原子计数，不按毫秒归零。只靠它保证进程内不重复
	//           （原来同一毫秒内只靠 16 位随机数区分，一次加载上千个网格时必然撞，导致 BLAS / 顶点段被两个网格共用）；
	//           同一毫秒内按序号即创建先后。32 位计数绕回需要 40 多亿次，绕回时时间戳早已不同。
	//   随机数：保留原来的 16 位，区分不同进程 / 机器同一时刻生成的值。
	//           每线程一个生成器（原来多线程共用一个 static mt19937，是数据竞争）。
	static std::atomic<uint32_t> s_sequence{ 0 };
	const uint32_t sequence = s_sequence.fetch_add(1, std::memory_order_relaxed);

	const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	const uint64_t timestamp = static_cast<uint64_t>(millis) & 0xFFFFFFFFFFFFull;

	thread_local std::mt19937 gen{ std::random_device{}() };
	const uint32_t rand_num = static_cast<uint32_t>(gen()) & 0xFFFFu;

	char buf[32];
	std::snprintf(buf, sizeof(buf), "%012llx%08x%04x",
		static_cast<unsigned long long>(timestamp), sequence, rand_num);
	return std::string(buf, 24);
}

float Tool::RadianToAngle(float radian)
{
	return radian / M_PI * 180.f;
}

float Tool::AngleToRadian(float angle)
{
	return angle / 180.f * M_PI;
}

glm::quat Tool::RandomSpread(const glm::quat& baseQuat, float spreadAngle) 
{
	thread_local std::random_device rd;
	thread_local std::mt19937 gen(rd());
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
	return glm::mix(value1, value2, t);
}

glm::vec3 Tool::LinearLerp(const glm::vec3& value1, const glm::vec3& value2, float t)
{
	return glm::mix(value1, value2, t);
}

glm::quat Tool::GetQuatFromRotate(float angle, const glm::vec3& axis)
{
	return glm::quat_cast(glm::rotate(glm::mat4(1.0f), glm::radians(angle), axis));
}

glm::vec3 Tool::GetDirectionFromRotate(const glm::quat& q)
{
	return  glm::normalize(q * glm::vec3(0, 0, 1));
}

glm::vec3 Tool::ColorTemperatureToRGB(float temperature)
{
	// 钳制输入范围
	temperature = std::clamp(temperature, 1000.0f, 40000.0f);

	// 对温度进行归一化，用于后续计算
	float temp = temperature / 100.0f;  // 缩小100倍，便于计算

	float R, G, B;

	// ---- 红色通道 ----
	if (temp <= 66.0f)
		R = 1.0f;
	else
		R = 1.292936186f * std::pow(temp - 60.0f, -0.1332047592f);

	// ---- 绿色通道 ----
	if (temp <= 66.0f)
		G = 0.390081579f * std::log(temp) - 0.631841444f;
	else
		G = 1.129890861f * std::pow(temp - 60.0f, -0.0755148492f);

	// ---- 蓝色通道 ----
	if (temp <= 19.0f)  // 对应 1900K 以下
		B = 0.0f;
	else if (temp <= 66.0f)
		B = 0.543206789f * std::log(temp - 10.0f) - 1.196254089f;
	else
		B = 1.0f;

	// ---- 钳制到 [0, 1] ----
	R = std::clamp(R, 0.0f, 1.0f);
	G = std::clamp(G, 0.0f, 1.0f);
	B = std::clamp(B, 0.0f, 1.0f);

	return glm::vec3(R, G, B);
}

bool Tool::IsSubDirectory(const fs::path& child, const fs::path& parent, const fs::path& base)
{
	try {
		fs::path abs_child = child.is_absolute() ? child : fs::weakly_canonical(base / child);
		fs::path abs_parent = parent.is_absolute() ? parent : fs::weakly_canonical(base / parent);

		fs::path rel = fs::relative(abs_child, abs_parent);
		if (rel.empty()) return true;
		return rel.string().find("..") != 0;
	}
	catch (const fs::filesystem_error&) {
		return false;  // 路径无效
	}
}

bool Tool::IsImmediateSubDirectory(const fs::path& child, const fs::path& parent, const fs::path& base)
{
	if (!base.is_absolute()) {
		return false;
	}

	try {
		fs::path abs_child = child.is_absolute() ? child : fs::weakly_canonical(base / child);
		fs::path abs_parent = parent.is_absolute() ? parent : fs::weakly_canonical(base / parent);
		fs::path rel = fs::relative(abs_child, abs_parent);

		// 情况1：相对路径为空 → 两者相同目录，不是父子
		if (rel.empty()) {
			return false;
		}
		// 情况2：相对路径以 ".." 开头 → child 不在 parent 下
		if (rel.string().find("..") == 0) {
			return false;
		}

		// 情况3：检查相对路径是否只有 1 个组件
		// 例如：rel = "file.txt" (1个组件) → 直接父子 ✅
		//       rel = "sub/file.txt" (2个组件) → 不是直接父子 ❌
		auto it = rel.begin();
		++it;  // 尝试移动到第二个组件
		return it == rel.end();  // 如果第二个组件不存在，说明只有1个组件

	}
	catch (const fs::filesystem_error&) {
		return false;
	}
}