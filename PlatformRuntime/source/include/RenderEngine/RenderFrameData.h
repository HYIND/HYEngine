#pragma once

#include <vector>
#include <variant>
#include "glm/glm.hpp"
#include "VulkanRenderEngine/General/VKRenderContext.h"
#include "VulkanRenderEngine/General/VKRenderContext2D.h"
#include "VulkanRenderEngine/Base/TextureCube.h"
#include "VulkanRenderEngine/General/DDGIVolumeDesc.h"
#include "UI/UiTypes.h"
#include "Render/Image2DSource.h"

namespace Render
{
	struct Render3DFrameData
	{
		std::vector<std::shared_ptr<VKRenderContext::RenderContext>> Scene3D_Contexts;

		std::shared_ptr<TextureCube> skybox;

		// 场景里启用的 DDGIVolumeComponent
		std::vector<DDGIVolumeDesc> ddgiVolumes;

		// 全部给默认值：没有相机写入的帧也得是一台合法相机（glm 默认构造不初始化）
		glm::mat4 projection = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
		glm::vec3 directionUp = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 directionRight = glm::vec3(1.0f, 0.0f, 0.0f);
		float nearPlane = 0.1f;
		float farPlane = 500.f;
		float fov = 80.f;

		void reset();
	};

	using Render2DFrameData = VKRenderContext::Render2DFrame;

	// 场景一帧的渲染数据：3D 和 2D 放在同一个三缓冲里，一次提交、一次取走，保证两者来自同一个逻辑帧。
	// UI 不在这里：UI 在宿主线程上画、要用最新的数据，单独走 RendererUI 的三缓冲。
	struct RenderSceneFrameData
	{
		std::shared_ptr<Render3DFrameData> scene3D = std::make_shared<Render3DFrameData>();
		std::shared_ptr<Render2DFrameData> scene2D = std::make_shared<Render2DFrameData>();

		void reset()
		{
			if (scene3D)
				scene3D->reset();
			if (scene2D)
				scene2D->reset();
		}
	};

	// UI 这一帧要画的东西：由 RenderSystem 从 UIViewRuntime 收集，按 layer 排好序。
	struct RenderUIFrameData
	{
		struct View
		{
			std::shared_ptr<UI::ParseResult> document;      // 解析好的 UI 文档（含每节点有效样式），共享只读
			std::shared_ptr<UI::UiInstanceState> instance;  // 该实例自己的运行时状态（开关/滑块等）
			std::shared_ptr<const Render2D::UIImageTable> images;	// 文档里图像引用的解析结果（Sprite / Texture / 动图）
			int layer = 0;                                  // 大的后画
		};

		std::vector<View> views;

		void reset();
	};
}