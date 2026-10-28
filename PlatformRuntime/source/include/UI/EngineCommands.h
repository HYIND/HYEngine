#pragma once

// 引擎在消息总线上提供的基础命令。加一条：Table 加一行 + 一个 Make 函数 + ApplyEngineCommand 一个分支。

#include "UI/Message.h"

namespace UI
{
	// 引擎在总线上用的"模块名"
	inline constexpr const char* EngineModuleId = "engine";

	namespace EngineCommands
	{
		inline constexpr const char* TargetFps = "engine.settings.target_fps";
		inline constexpr const char* MasterVolume = "engine.settings.master_volume";
		inline constexpr const char* ChannelVolume = "engine.settings.channel_volume";

		struct Desc
		{
			const char* name;
			const char* summary;
		};

		inline constexpr Desc Table[] =
		{
			{ TargetFps,     "value = 目标帧率，0 表示不限帧" },
			{ MasterVolume,  "value = 主音量 0-100" },
			{ ChannelVolume, "channel，value = 0-100" },
		};

		inline Message MakeTargetFps(int fps)
		{
			Message message;
			message.topic = TargetFps;
			message.Set("value", fps);
			return message;
		}

		inline Message MakeMasterVolume(int percent)
		{
			Message message;
			message.topic = MasterVolume;
			message.Set("value", percent);
			return message;
		}

		inline Message MakeChannelVolume(uint32_t channel, int percent)
		{
			Message message;
			message.topic = ChannelVolume;
			message.Set("channel", channel);
			message.Set("value", percent);
			return message;
		}
	}
}
