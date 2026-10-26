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
		static void processSceneModel(
			RenderState& state,
			VKRenderObjectData::SceneRenderData& renderData,
			const std::shared_ptr<VKRenderContext::SceneModelRenderData>& data
		);
		static void processFirstPersonModel(
			RenderState& state,
			VKRenderObjectData::FirstPersonRenderData& renderData,
			const std::shared_ptr<VKRenderContext::FirstPersonRenderData>& data);
	};

}
