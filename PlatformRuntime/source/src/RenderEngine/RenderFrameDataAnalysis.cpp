#include "RenderEngine/RenderFrameDataAnalysis.h"

namespace Render
{
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
			case VKRenderContext::RenderContextType::StaticModel:
			{
				ProcessStaticModel(
					state,
					state.objects.sceneRenderData,
					std::static_pointer_cast<VKRenderContext::StaticModelRenderData>(context->data)
				);
				break;
			}
			case VKRenderContext::RenderContextType::SkinnedModel:
			{
				ProcessSkinnedModel(
					state,
					state.objects.sceneSkinnedRenderData,
					std::static_pointer_cast<VKRenderContext::SkinnedModelRenderData>(context->data)
				);
				break;
			}
			case VKRenderContext::RenderContextType::DirLight:
			{
				auto ptr = std::static_pointer_cast<VKRenderContext::DirLightRenderData>(context->data);
				if (ptr && ptr->light)
				{
					auto info = std::make_shared<DirLightInfo>();
					info->light = ptr->light;
					info->renderCube = ptr->renderCube;
					dirLightInfos.push_back(std::move(info));
				}
				break;
			}
			case VKRenderContext::RenderContextType::PointLight:
			{
				auto ptr = std::static_pointer_cast<VKRenderContext::PointLightRenderData>(context->data);
				if (ptr && ptr->light)
				{
					auto info = std::make_shared<PointLightInfo>();
					info->light = ptr->light;
					info->renderCube = ptr->renderCube;
					pointLightInfos.push_back(std::move(info));
				}
				break;
			}
			case VKRenderContext::RenderContextType::SpotLight:
			{
				auto ptr = std::static_pointer_cast<VKRenderContext::SpotLightRenderData>(context->data);
				if (ptr && ptr->light)
				{
					auto info = std::make_shared<SpotLightInfo>();
					info->light = ptr->light;
					info->renderCube = ptr->renderCube;
					spotLightInfos.push_back(std::move(info));
				}
				break;
			}
			case VKRenderContext::RenderContextType::Effect:
			{
				auto ptr = std::static_pointer_cast<VKRenderContext::EffectRenderData>(context->data);
				if (ptr && ptr->properties)
					state.objects.sceneRenderData.effectItems.push_back(ptr->properties);
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
		RenderState& state,
		VKRenderObjectData::SceneStaticRenderData& renderData,
		const std::shared_ptr<VKRenderContext::StaticModelRenderData>& data
	)
	{
		using MeshItem = VKRenderObjectData::SceneStaticRenderData::MeshItem;

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

			bool isTransprant = (mode == AlphaMode::Blend);
			if (isTransprant)
			{
				MeshItem item;
				item.transform = transform;
				item.prevTransform = transform;
				item.transformHistory = transformHistory;
				item.meshinfo = info;
				renderData.transparentMesh.push_back(item);
			}
			else
			{
				MeshItem item;
				item.transform = transform;
				item.prevTransform = transform;
				item.transformHistory = transformHistory;
				item.meshinfo = info;
				renderData.opaqueMesh.push_back(item);

				auto& material = item.meshinfo.material;
				if (material->GetTwoSided())
					state.objects.sceneRenderData.opaqueMesh_renderIndex.twoSideIndex.push_back(renderData.opaqueMesh.size() - 1);
				else
					state.objects.sceneRenderData.opaqueMesh_renderIndex.oneSideIndex.push_back(renderData.opaqueMesh.size() - 1);
			}
		}

	}

	void RenderFrameDataAnalysisHelp::ProcessSkinnedModel(
		RenderState& state,
		VKRenderObjectData::SceneSkinnedRenderData& renderData,
		const std::shared_ptr<VKRenderContext::SkinnedModelRenderData>& data
	)
	{
		using SkinnedMeshItem = VKRenderObjectData::SceneSkinnedRenderData::SkinnedMeshItem;

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

			SkinnedMeshItem item;
			item.transform = transform;
			item.prevTransform = transform;
			item.transformHistory = transformHistory;
			item.meshinfo = info;
			item.palette = data->palette;

			if (mode == AlphaMode::Blend)
				renderData.transparentSkinnedMesh.push_back(item);
			else
				renderData.opaqueSkinnedMesh.push_back(item);
		}
	}
}
