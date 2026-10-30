#include "RenderEngine/RenderFrameDataAnalysis.h"

namespace Render
{
	using namespace VKRenderContext;
	using namespace VKRenderObjectData;

	void RenderFrameDataAnalysisHelp::AnalysisRenderFrameData(std::shared_ptr<Render3DFrameData>& framedata, RenderState& state)
	{
		auto& contexts = framedata->Scene3D_Contexts;

		auto& dirLightInfos = state.lights.dirLightInfos;
		auto& pointLightInfos = state.lights.pointLightInfos;;
		auto& spotLightInfos = state.lights.spotLightInfos;

		for (auto& context : contexts)
		{
			if (!context || !context->data)
				continue;

			switch (context->type)
			{
			case RenderContextType::StaticModel:
			{
				ProcessStaticModel(
					state.objects.sceneStaticRenderData,
					std::static_pointer_cast<StaticModelRenderData>(context->data)
				);
				break;
			}
			case RenderContextType::SkinnedModel:
			{
				ProcessSkinnedModel(
					state.objects.sceneSkinnedRenderData,
					std::static_pointer_cast<SkinnedModelRenderData>(context->data)
				);
				break;
			}
			case RenderContextType::DirLight:
			{
				auto ptr = std::static_pointer_cast<DirLightRenderData>(context->data);
				if (ptr && ptr->light)
				{
					auto info = std::make_shared<DirLightInfo>();
					info->light = ptr->light;
					info->renderCube = ptr->renderCube;
					dirLightInfos.push_back(std::move(info));
				}
				break;
			}
			case RenderContextType::PointLight:
			{
				auto ptr = std::static_pointer_cast<PointLightRenderData>(context->data);
				if (ptr && ptr->light)
				{
					auto info = std::make_shared<PointLightInfo>();
					info->light = ptr->light;
					info->renderCube = ptr->renderCube;
					pointLightInfos.push_back(std::move(info));
				}
				break;
			}
			case RenderContextType::SpotLight:
			{
				auto ptr = std::static_pointer_cast<SpotLightRenderData>(context->data);
				if (ptr && ptr->light)
				{
					auto info = std::make_shared<SpotLightInfo>();
					info->light = ptr->light;
					info->renderCube = ptr->renderCube;
					spotLightInfos.push_back(std::move(info));
				}
				break;
			}
			case RenderContextType::Effect:
			{
				auto ptr = std::static_pointer_cast<EffectRenderData>(context->data);
				if (ptr && ptr->properties)
					state.objects.sceneStaticRenderData.effectItems.push_back(ptr->properties);
				break;
			}
			default:
				break;
			}
		}

		if (framedata->skybox)
			state.skyboxParams.skyCube = framedata->skybox;
	}

	void RenderFrameDataAnalysisHelp::ProcessStaticModel(
		SceneStaticRenderData& renderData,
		const std::shared_ptr<StaticModelRenderData>& data
	)
	{
		using MeshItem = SceneStaticRenderData::MeshItem;

		if (!data || !data->model)
			return;

		auto& transform = data->transform;
		auto transformHistory = data->transformHistory;

		for (auto& info : data->model->GetMeshInfos())
		{
			if (!info.mesh || !info.material)
				continue;

			float opacity = info.material->GetOpacity();
			AlphaMode mode = info.material->GetAlphaMode();
			if (opacity <= 0.f || (mode == AlphaMode::Mask && opacity < info.material->GetMaskThreshold()))
				continue;

			MeshItem item;
			item.transform = transform;
			item.prevTransform = transform;
			item.transformHistory = transformHistory;
			item.meshinfo = info;

			bool isTransprant = (mode == AlphaMode::Blend);
			if (isTransprant) {
				renderData.transparentItems.push_back(item);
			}
			else
			{
				renderData.opaqueItems.push_back(item);
				auto& material = item.meshinfo.material;
				if (material->GetTwoSided())
					renderData.opaqueItems_renderIndex.twoSideIndex.push_back(renderData.opaqueItems.size() - 1);
				else
					renderData.opaqueItems_renderIndex.oneSideIndex.push_back(renderData.opaqueItems.size() - 1);
			}
		}

	}

	void RenderFrameDataAnalysisHelp::ProcessSkinnedModel(
		SceneSkinnedRenderData& renderData,
		const std::shared_ptr<SkinnedModelRenderData>& data
	)
	{
		using SkinnedMeshItem = SceneSkinnedRenderData::SkinnedMeshItem;

		if (!data || !data->model)
			return;

		auto& transform = data->transform;
		auto transformHistory = data->transformHistory;

		auto& meshInfos = data->model->GetMeshInfos();
		for (std::size_t meshIndex = 0; meshIndex < meshInfos.size(); ++meshIndex)
		{
			auto& info = meshInfos[meshIndex];

			if (!info.mesh || !info.material)
				continue;

			float opacity = info.material->GetOpacity();
			AlphaMode mode = info.material->GetAlphaMode();
			if (opacity <= 0.f || (mode == AlphaMode::Mask && opacity < info.material->GetMaskThreshold()))
				continue;

			SkinnedMeshItem item;
			item.transform = transform;
			item.prevTransform = transform;
			item.transformHistory = transformHistory;
			item.meshinfo = info;
			item.palette = (meshIndex < data->palettes.size()) ? data->palettes[meshIndex] : nullptr;

			bool isTransprant = (mode == AlphaMode::Blend);
			if (isTransprant) {
				renderData.transparentItems.push_back(item);
			}
			else
			{
				auto& material = item.meshinfo.material;
				if (material->GetTwoSided())
					renderData.opaqueTwoSideItems.push_back(item);
				else
					renderData.opaqueOneSideItems.push_back(item);
			}

		}
	}
}
