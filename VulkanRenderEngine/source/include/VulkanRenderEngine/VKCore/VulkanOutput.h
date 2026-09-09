#pragma once

#include "vkstdafx.h"
#include <sstream>

inline struct outStream_t {
	inline static std::stringstream ss;
	struct returnedStream_t {
		returnedStream_t operator<<(const std::string& string) const {
			ss << string;
			return {};
		}
	};
	returnedStream_t operator<<(const std::string& string) const {
		ss.clear();
		ss << string;
		return {};
	}
} outStream;

//情况1：根据函数返回值确定是否抛异常
#ifdef VK_RESULT_THROW
class result_t {
	VkResult result;
public:
	static void(*callback_throw)(VkResult);
	result_t(VkResult result) :result(result) {}
	result_t(result_t&& other) noexcept :result(other.result) { other.result = VK_SUCCESS; }
	~result_t() noexcept(false) {
		if (uint32_t(result) < VK_RESULT_MAX_ENUM)
			return;
		if (callback_throw)
			callback_throw(result);
		throw result;
	}
	operator VkResult() {
		VkResult result = this->result;
		this->result = VK_SUCCESS;
		return result;
	}
};
inline void(*result_t::callback_throw)(VkResult);

//情况2：若抛弃函数返回值，让编译器发出警告
#elifdef VK_RESULT_NODISCARD
struct [[nodiscard]] result_t {
	VkResult result;
	result_t(VkResult result) :result(result) {}
	operator VkResult() const { return result; }
};
//在本文件中关闭弃值提醒
#pragma warning(disable:4834)
#pragma warning(disable:6031)

//情况3：啥都不干
#else
using result_t = VkResult;
#endif