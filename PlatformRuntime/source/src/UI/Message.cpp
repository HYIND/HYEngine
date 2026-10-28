#include "UI/Message.h"

#include <algorithm>

namespace UI
{
	void Message::Set(std::string_view name, Value value)
	{
		for (auto& arg : args)
		{
			if (std::string_view(arg.first) == name)
			{
				arg.second = std::move(value);
				return;
			}
		}
		args.emplace_back(std::string(name), std::move(value));
	}

	void Message::Set(std::string_view name, bool value)
	{
		Set(name, Value(value));
	}

	void Message::Set(std::string_view name, double value)
	{
		Set(name, Value(value));
	}

	void Message::Set(std::string_view name, const std::string& value)
	{
		Set(name, Value(value));
	}

	void Message::Set(std::string_view name, const char* value)
	{
		Set(name, Value(std::string(value ? value : "")));
	}

	const Value* Message::Find(std::string_view name) const
	{
		for (const auto& arg : args)
		{
			if (std::string_view(arg.first) == name)
				return &arg.second;
		}
		return nullptr;
	}

	double Message::GetNumber(std::string_view name, double fallback) const
	{
		const Value* value = Find(name);
		if (!value)
			return fallback;

		if (const auto* number = std::get_if<double>(value))
			return *number;
		if (const auto* integer = std::get_if<int64_t>(value))
			return static_cast<double>(*integer);
		if (const auto* unsigned32 = std::get_if<uint32_t>(value))
			return static_cast<double>(*unsigned32);
		if (const auto* flag = std::get_if<bool>(value))
			return *flag ? 1.0 : 0.0;
		return fallback;
	}

	int64_t Message::GetInteger(std::string_view name, int64_t fallback) const
	{
		const Value* value = Find(name);
		if (!value)
			return fallback;

		if (const auto* integer = std::get_if<int64_t>(value))
			return *integer;
		if (const auto* unsigned32 = std::get_if<uint32_t>(value))
			return static_cast<int64_t>(*unsigned32);
		if (const auto* number = std::get_if<double>(value))
			return static_cast<int64_t>(*number);
		if (const auto* flag = std::get_if<bool>(value))
			return *flag ? 1 : 0;
		return fallback;
	}

	uint32_t Message::GetUint32(std::string_view name, uint32_t fallback) const
	{
		const Value* value = Find(name);
		if (!value)
			return fallback;

		if (const auto* unsigned32 = std::get_if<uint32_t>(value))
			return *unsigned32;
		if (const auto* integer = std::get_if<int64_t>(value))
			return static_cast<uint32_t>(*integer);
		if (const auto* number = std::get_if<double>(value))
			return static_cast<uint32_t>(*number);
		if (const auto* flag = std::get_if<bool>(value))
			return *flag ? 1u : 0u;
		return fallback;
	}

	bool Message::GetFlag(std::string_view name, bool fallback) const
	{
		const Value* value = Find(name);
		if (!value)
			return fallback;

		if (const auto* flag = std::get_if<bool>(value))
			return *flag;
		return GetNumber(name, fallback ? 1.0 : 0.0) != 0.0;
	}

	std::string Message::GetText(std::string_view name) const
	{
		const Value* value = Find(name);
		if (!value)
			return std::string();

		if (const auto* text = std::get_if<std::string>(value))
			return *text;
		return std::string();
	}

	Message FromUiEvent(const UiEvent& event)
	{
		Message message;
		message.topic = event.command;
		message.Set("node", event.node);
		message.Set("value", static_cast<double>(event.value));
		message.Set("itemId", event.itemId);
		message.Set("text", event.text);
		message.Set("legacyId", event.legacyId);
		return message;
	}

	UiEvent ToUiEvent(const Message& message)
	{
		UiEvent event;
		event.command = message.topic;
		event.node = message.GetText("node");
		event.value = static_cast<float>(message.GetNumber("value"));
		event.itemId = message.GetText("itemId");
		event.text = message.GetText("text");
		event.legacyId = static_cast<int>(message.GetInteger("legacyId"));
		return event;
	}
}
