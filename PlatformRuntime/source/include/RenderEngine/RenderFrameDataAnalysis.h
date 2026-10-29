#pragma once

#include "RenderEngine/RenderFrameData.h"
#include "VulkanRenderEngine/General/RenderState.h"

#include <memory>

namespace Render
{
	class RenderFrameDataAnalysisHelp
	{

	public:
		static void AnalysisRenderFrameData(std::shared_ptr<Render3DFrameData>& framedata, RenderState& state);

	private:
		static void ProcessStaticModel(
			VKRenderObjectData::SceneStaticRenderData& renderData,
			const std::shared_ptr<VKRenderContext::StaticModelRenderData>& data
		);
		static void ProcessSkinnedModel(
			VKRenderObjectData::SceneSkinnedRenderData& renderData,
			const std::shared_ptr<VKRenderContext::SkinnedModelRenderData>& data
		);
	};

}
