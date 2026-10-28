#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "UI/UiTypes.h"

namespace UI
{
	using Value = std::variant<std::monostate, bool, uint32_t, int64_t, double, std::string>;

	struct Message
	{
		std::string topic;
		std::vector<std::pair<std::string, Value>> args;

		void Set(std::string_view name, Value value);

		// 有符号走 int64_t，无符号且不超过 32 位走 uint32_t
		template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>, int> = 0>
		void Set(std::string_view name, T value)
		{
			if constexpr (std::is_signed_v<T>)
				Set(name, Value(static_cast<int64_t>(value)));
			else if constexpr (sizeof(T) <= sizeof(uint32_t))
				Set(name, Value(static_cast<uint32_t>(value)));
			else
				Set(name, Value(static_cast<int64_t>(value)));
		}

		void Set(std::string_view name, bool value);
		void Set(std::string_view name, double value);
		void Set(std::string_view name, const std::string& value);
		void Set(std::string_view name, const char* value);

		const Value* Find(std::string_view name) const;

		double GetNumber(std::string_view name, double fallback = 0.0) const;
		int64_t GetInteger(std::string_view name, int64_t fallback = 0) const;
		uint32_t GetUint32(std::string_view name, uint32_t fallback = 0) const;
		bool GetFlag(std::string_view name, bool fallback = false) const;
		std::string GetText(std::string_view name) const;
	};

	// topic := event.command，args := node / value / itemId / text / legacyId
	Message FromUiEvent(const UiEvent& event);
	UiEvent ToUiEvent(const Message& message);
}
