#pragma once

#include <vector>
#include <variant>
#include "glm/glm.hpp"
#include "RenderEngine/D2DTools.h"
#include "VulkanRenderEngine/General/VKRenderContext.h"
#include "RenderEngine/D2DRenderContext.h"
#include "VulkanRenderEngine/Base/TextureCube.h"
#include "UI/UiTypes.h"

namespace Render
{
	struct Render3DFrameData
	{
		std::vector<std::shared_ptr<VKRenderContext::RenderContext>> Scene3D_Contexts;

		std::shared_ptr<TextureCube> skybox;

		glm::mat4 projection;
		glm::mat4 view;
		glm::vec3 position;
		glm::vec3 direction;
		glm::vec3 directionUp;
		glm::vec3 directionRight;
		float nearPlane = 0.1f;
		float farPlane = 500.f;
		float fov = 80.f;

		void reset();
	};

	struct Render2DFrameData
	{
		std::vector<std::shared_ptr<D2DRenderContext::RenderContext>> D2D_Contexts;

		void reset();
	};

	// UI 这一帧要画的东西：由 RenderSystem 从 UIViewRuntime 收集，按 layer 排好序。
	struct RenderUIFrameData
	{
		struct View
		{
			std::shared_ptr<UI::ParseResult> document;      // 解析好的 UI 文档（含每节点有效样式），共享只读
			std::shared_ptr<UI::UiInstanceState> instance;  // 该实例自己的运行时状态（开关/滑块等）
			int layer = 0;                                  // 大的后画
		};

		std::vector<View> views;

		void reset();
	};
}