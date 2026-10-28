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
			processSceneModel(
				state,
				state.objects.sceneRenderData,
				std::static_pointer_cast<VKRenderContext::SceneModelRenderData>(context->data)
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
		case VKRenderContext::RenderContextType::FirstPersonModel:
		{
			processFirstPersonModel(
				state,
				state.objects.firstPersonRenderData,
				std::static_pointer_cast<VKRenderContext::FirstPersonRenderData>(context->data)
			);
			break;
		}
		case VKRenderContext::RenderContextType::Effect:
		{
			auto ptr = std::static_pointer_cast<VKRenderContext::SceneEffectRenderData>(context->data);
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

void RenderFrameDataAnalysisHelp::processSceneModel(
	RenderState& state,
	VKRenderObjectData::SceneRenderData& renderData,
	const std::shared_ptr<VKRenderContext::SceneModelRenderData>& data
)
{
	using OpaqueMeshItem = VKRenderObjectData::SceneRenderData::OpaqueMeshItem;
	using TransparentMeshItem = VKRenderObjectData::SceneRenderData::TransparentMeshItem;
	using OpaqueSkinnedModelItem = VKRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem;
	using TransparentSkinnedMeshItem = VKRenderObjectData::SceneRenderData::TransparentSkinnedMeshItem;

	if (!data || !data->model)
		return;

	auto& transform = data->transform;
	auto transformHistory = data->transformHistory;

	if (data->animatorViews.empty())
	{
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
				TransparentMeshItem item;
				item.transform = transform;
				item.prevTransform = transform;
				item.transformHistory = transformHistory;
				item.meshinfo = info;
				renderData.transparentMesh.push_back(item);
			}
			else
			{
				OpaqueMeshItem item;
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
	else
	{
		std::vector<StaticMeshInfo> meshInfos;
		auto shadredAnimatorViews = std::make_shared<std::vector<VKRenderContext::AnimatorView>>(data->animatorViews);

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
				TransparentSkinnedMeshItem item;
				item.transform = transform;
				item.prevTransform = transform;
				item.transformHistory = transformHistory;
				item.meshinfo = info;
				item.animators = shadredAnimatorViews;
				renderData.transparentSkinnedMesh.push_back(item);
			}
			else
			{
				meshInfos.push_back(info);
			}
		}
		if (!meshInfos.empty())
		{
			OpaqueSkinnedModelItem item;
			item.transform = transform;
			item.prevTransform = transform;
			item.transformHistory = transformHistory;
			item.models = std::move(meshInfos);
			item.animators = shadredAnimatorViews;
			renderData.opaqueSkinnedModel.push_back(item);
		}
	}
}

void RenderFrameDataAnalysisHelp::processFirstPersonModel(
	RenderState& state,
	VKRenderObjectData::FirstPersonRenderData& renderData,
	const std::shared_ptr<VKRenderContext::FirstPersonRenderData>& data)
{
	using OpaqueMeshItem = VKRenderObjectData::FirstPersonRenderData::OpaqueMeshItem;
	using TransparentMeshItem = VKRenderObjectData::FirstPersonRenderData::TransparentMeshItem;
	using OpaqueSkinnedModelItem = VKRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem;
	using TransparentSkinnedMeshItem = VKRenderObjectData::FirstPersonRenderData::TransparentSkinnedMeshItem;

	if (!data || !data->model)
		return;

	auto cameraView = data->cameraView;

	if (data->animatorViews.empty())
	{
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
				TransparentMeshItem item;
				item.cameraView = cameraView;
				item.meshinfo = info;
				renderData.transparentMesh.push_back(item);
			}
			else
			{
				OpaqueMeshItem item;
				item.cameraView = cameraView;
				item.meshinfo = info;
				renderData.opaqueMesh.push_back(item);
			}
		}
	}
	else
	{
		std::vector<StaticMeshInfo> meshInfos;
		auto shadredAnimatorViews = std::make_shared<std::vector<VKRenderContext::AnimatorView>>(std::move(data->animatorViews));

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
				TransparentSkinnedMeshItem item;
				item.cameraView = cameraView;
				item.meshinfo = info;
				item.animators = shadredAnimatorViews;
				renderData.transparentSkinnedMesh.push_back(item);
			}
			else
			{
				meshInfos.push_back(info);
			}
		}
		if (!meshInfos.empty())
		{
			OpaqueSkinnedModelItem item;
			item.cameraView = cameraView;
			item.models = std::move(meshInfos);
			item.animators = shadredAnimatorViews;
			renderData.opaqueSkinnedModel.push_back(item);
		}
	}
}




}
