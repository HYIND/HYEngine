#pragma once

#include "Manager/ResourceManager.h"

namespace D2DRenderContext
{
	struct BaseRenderData
	{
		float x = 0;
		float y = 0;
		float width = 0;
		float height = 0;
		float rotation = 90.f;
	};

	struct SpriteRenderData :public BaseRenderData
	{
		float opacity = 1.0f;
		ID2D1Bitmap* bitmap = nullptr;
	};

	struct GIFAnimationRenderData :public BaseRenderData
	{
		float opacity = 1.0f;

		int64_t startTime = 0;
		float giftotalTime = 1.f;
		int loopCount = 1;

		GIFINFO* gifInfo = nullptr;
	};

	struct DebugLineRenderData :public BaseRenderData
	{
		glm::vec2 line_pos1;
		glm::vec2 line_pos2;
	};

	enum class RenderContextType
	{
		DebugLine,
		Sprite,
		GIFAnimation
	};

	struct RenderContext
	{
		RenderContextType type;
		int layer = 0;
		int internalZOrder = 0;

		std::shared_ptr<BaseRenderData> data;

		bool operator<(const RenderContext& other) const;
	};
};
