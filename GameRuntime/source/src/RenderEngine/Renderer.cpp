#include "vkstdafx.h"
#include "RenderEngine/Renderer.h"
#include "RenderEngine/D2DTools.h"
#include "Helper/Tools.h"
#include "VulkanRenderEngine/VKContext.h"
#include "GeneralManager/WindowHandleManager.h"
#include <glm/gtc/matrix_access.hpp>
#include <memory>
#include <algorithm>
#include <ranges>

using namespace Render;

glm::vec2 MapPosToRenderPos(const glm::vec2& mapPos)
{
	RECT rect = WINDOWHANDLEMANAGER->GetRect();

	glm::vec2 renderPos;

	// 计算地图和渲染区域的大小
	float mapWidth = static_cast<float>(MapBoundary::right - MapBoundary::left);
	float mapHeight = static_cast<float>(MapBoundary::bottom - MapBoundary::top);
	float renderWidth = static_cast<float>(rect.right - rect.left);
	float renderHeight = static_cast<float>(rect.bottom - rect.top);

	// 归一化地图坐标 (0-1 范围)
	float normalizedX = (mapPos.x - MapBoundary::left) / mapWidth;
	float normalizedY = (mapPos.y - MapBoundary::top) / mapHeight;

	// 映射到渲染坐标
	renderPos.x = rect.left + normalizedX * renderWidth;
	renderPos.y = rect.top + normalizedY * renderHeight;

	return renderPos;
}

void RenderFrameDataAnalysisHelp::AnalysisRenderFrameData(std::shared_ptr<RenderFrameData>& framedata, RenderState& state)
{
	auto& contexts = framedata->GL_Contexts;

	auto& dirLightInfos = state.lights.dirLightInfos;
	auto& pointLightInfos = state.lights.pointLightInfos;;
	auto& spotLightInfos = state.lights.spotLightInfos;

	for (auto& context : contexts)
	{
		if (!context || !context->data)
			continue;

		switch (context->type)
		{
		case VKRenderContext::RenderContextType::Model:
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
		state.skybox.cube = framedata->skybox;
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

	auto transform = data->transformView.transformTripleBuffer->acquireReadBuffer();
	auto prevTransform = *data->transformView.prevRenderTransforms;

	*data->transformView.prevRenderTransforms = transform;

	if (data->animatorViews.empty())
	{
		for (auto& info : data->model->getMeshInfos())
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
				item.prevTransform = prevTransform;
				item.meshinfo = info;
				renderData.transparentMesh.push_back(item);
			}
			else
			{
				OpaqueMeshItem item;
				item.transform = transform;
				item.prevTransform = prevTransform;
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
		std::vector<MeshInfo> meshInfos;
		auto shadredAnimatorViews = std::make_shared<std::vector<VKRenderContext::AnimatorView>>(data->animatorViews);

		for (auto& info : data->model->getMeshInfos())
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
				item.prevTransform = prevTransform;
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
			item.prevTransform = prevTransform;
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
		for (auto& info : data->model->getMeshInfos())
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
		std::vector<MeshInfo> meshInfos;
		auto shadredAnimatorViews = std::make_shared<std::vector<VKRenderContext::AnimatorView>>(std::move(data->animatorViews));

		for (auto& info : data->model->getMeshInfos())
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


Renderer::Renderer(ID2D1DeviceContext* rt, RenderTripleBufferPtr buffers)
	:_redBrush(nullptr), _optionChange(false), _isVulkanInit(false), _pushFrameStop(true)
{
	SetRenderTarget(rt);
	SetBuffers(buffers);
}

Render::Renderer::~Renderer()
{}

void Renderer::SetRenderTarget(ID2D1DeviceContext* rt)
{
	_renderTarget = rt;
}

void Renderer::SetBuffers(RenderTripleBufferPtr buffers)
{
	_buffers = buffers;
}

void Render::Renderer::PushFrameLoop()
{
	while (!_pushFrameStop)
	{
		if (!_buffers)
		{
			std::this_thread::yield();
			continue;
		}
		auto render = _vulkanRenderer;
		if (!render)
		{
			std::this_thread::yield();
			continue;
		}

		auto& data = _earlyDataBuffers.acquireWriteBuffer();
		auto& framedata = _buffers->acquireReadBuffer();

		data.render = render;
		data.state = RenderStateBuilder()
			.SetCamera(framedata->projection, framedata->view,
				framedata->position, framedata->direction, framedata->directionUp, framedata->directionRight,
				framedata->nearPlane, framedata->farPlane, framedata->fov)
			.Build();

		RenderFrameDataAnalysisHelp::AnalysisRenderFrameData(framedata, *data.state);
		if (_optionChange)
		{
			render->SetOption(_option);
			_optionChange = false;
		}

		data.D2D_Contexts = std::move(framedata->D2D_Contexts);

		render->PushFrameState(data.state);

		_earlyDataBuffers.submitWriteBuffer();
	}
}

void Renderer::renderFrame()
{
	if (!_buffers)
		return;
	if (_pushFrameStop || !_pushFrameThread)
	{
		_pushFrameStop = false;
		_pushFrameThread = std::make_shared<std::thread>(&Renderer::PushFrameLoop, this);
	}

	auto render = _vulkanRenderer;
	if (!render)
		return;

	auto data = _earlyDataBuffers.acquireReadBuffer();

	if (_isVulkanInit)
		renderOpenGLFrame();

	renderD2DFrame(data.D2D_Contexts);

	_earlyDataBuffers.ReleaseReadBuffer();
}

void Render::Renderer::renderD2DFrame(std::vector<std::shared_ptr<D2DRenderContext::RenderContext>>& D2DContexts)
{
	if (D2DContexts.empty())
		return;

	auto& contexts = D2DContexts;

	std::sort(contexts.begin(), contexts.end(),
		[](const std::shared_ptr<D2DRenderContext::RenderContext>& a, const std::shared_ptr<D2DRenderContext::RenderContext>& b)
		{
			if (!a) return false;
			if (!b) return true;

			if (a->layer != b->layer)
				return a->layer < b->layer;
			return a->internalZOrder < b->internalZOrder;
		});

	_renderTarget->BeginDraw();
	for (auto& context : contexts)
	{
		if (!context || !context->data)
			continue;

		switch (context->type)
		{
		case D2DRenderContext::RenderContextType::Sprite:
		{
			processSprite(std::static_pointer_cast<D2DRenderContext::SpriteRenderData>(context->data));
			break;
		}
		case D2DRenderContext::RenderContextType::GIFAnimation:
		{
			processGIFAnimation(std::static_pointer_cast<D2DRenderContext::GIFAnimationRenderData>(context->data));
			break;
		}
		case D2DRenderContext::RenderContextType::DebugLine:
		{
			processDebugLines(std::static_pointer_cast<D2DRenderContext::DebugLineRenderData>(context->data));
			break;
		}
		default:
			break;
		}
	}
	_renderTarget->EndDraw();
}

void Render::Renderer::renderOpenGLFrame()
{
	auto renderer = _vulkanRenderer;
	if (!renderer || !_sharedTexture)
		return;

	auto start = Tool::GetTimestampMircoseconds();
	renderer->WaitImage([&](std::shared_ptr<Texture2D> tex) {
		if (!tex || !_sharedTexture->vulkanTexture) return;
		auto cmd = VKCONTEXT->GetCommandBuffer();
		Texture2D::BlitImageAsync(cmd, tex, _sharedTexture->vulkanTexture);
		_sharedTexture->vulkanTexture->TransitionLayout(cmd, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eTransfer);
		if (cmd->IsRecording())
			VKCONTEXT->SubmitCommandImmediatelyAndWait(cmd);
		});


	ID3D11Texture2D* pBackBuffer = nullptr;
	auto hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (SUCCEEDED(hr))
	{
		IDXGIKeyedMutex* pMutex = nullptr;
		hr = _sharedTexture->d3dTexture->QueryInterface(IID_PPV_ARGS(&pMutex));
		if (SUCCEEDED(hr) && pMutex) {
			hr = pMutex->AcquireSync(0, 0);
			if (FAILED(hr)) {
				// 超时或错误，不能继续读取
				pMutex->Release();
				pBackBuffer->Release();
				return;
			}
		}

		//g_pD3DContext->Flush();
		g_pD3DContext->CopyResource(pBackBuffer, _sharedTexture->d3dTexture);	// 将共享纹理复制到后台缓冲区
		pBackBuffer->Release();

		if (pMutex) {
			hr = pMutex->ReleaseSync(0);
			if (FAILED(hr)) {
				// 处理错误
			}
		}

	}
	std::cout << std::format("cost2 {}ms\n", (Tool::GetTimestampMircoseconds() - start) / 1000.f);
}

void Render::Renderer::InitVulkanRender(uint32_t scr_width, uint32_t scr_height)
{
	if (_vulkanRenderer)
		return;

	auto renderer = std::make_shared<VulkanRenderer>();

	std::vector<std::string> extensions;
	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

	auto vulkanInstance = VulkanRenderer::CreateInstance(extensions);
	auto vkRenderer = VulkanRenderer::CreateForOffScreen(vulkanInstance, scr_width, scr_height);

	if (vkRenderer != nullptr)
	{
		if (!_sharedTexture)
			_sharedTexture = CreateSharedTexture(g_pD3DDevice, scr_width, scr_height, DXGI_FORMAT_B8G8R8A8_UNORM);
		//_sharedTexture = CreateSharedTexture(g_pD3DDevice, scr_width, scr_height, DXGI_FORMAT_R8G8B8A8_UNORM);

		if (!_sharedTexture)
			return;

		vkRenderer->Run();
		_vulkanRenderer = vkRenderer;
		_isVulkanInit = true;
	}
}

int Render::Renderer::GetOpenGLWidth()
{
	auto r = _vulkanRenderer;
	if (!r)
		return 0;
	return r->GetWidth();
}

int Render::Renderer::GetOpenGLHeight()
{
	auto r = _vulkanRenderer;
	if (!r)
		return 0;
	return r->GetHeight();
}

std::shared_ptr<VulkanRenderer> Render::Renderer::GetOpenGLRender()
{
	return _vulkanRenderer;
}

void Renderer::processSprite(std::shared_ptr<D2DRenderContext::SpriteRenderData> data)
{
	if (!data || !data->bitmap)
		return;
	if (data->width <= 0 || data->height <= 0)
		return;
	if (data->opacity <= 0)
		return;

	float x1, y1, x2, y2;
	x1 = data->x - float(data->width / 2.f);
	y1 = data->y - float(data->height / 2.f);
	x2 = data->x + float(data->width / 2.f);
	y2 = data->y + float(data->height / 2.f);

	glm::vec2 pos1 = MapPosToRenderPos(glm::vec2(x1, y1));
	glm::vec2 pos2 = MapPosToRenderPos(glm::vec2(x2, y2));

	glm::vec2 center = MapPosToRenderPos(glm::vec2(data->x, data->y));

	_renderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(data->rotation, D2D1::Point2F(center.x, center.y)));
	_renderTarget->DrawBitmap(data->bitmap,
		D2D1::RectF(pos1.x, pos1.y, pos2.x, pos2.y),
		data->opacity);
	_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Renderer::processGIFAnimation(std::shared_ptr<D2DRenderContext::GIFAnimationRenderData> data)
{

	if (!data || !data->gifInfo) return;
	UINT framecount = data->gifInfo->getFrameCount();
	if (framecount <= 0 || data->giftotalTime <= 0.f) return;
	if (data->width <= 0 || data->height <= 0) return;
	int64_t currenttime = Tool::GetTimestampMilliseconds();
	if (data->loopCount > 0 && currenttime > data->startTime + data->loopCount * data->giftotalTime) return;
	if (data->opacity <= 0.f) return;

	float x1, y1, x2, y2;
	x1 = data->x - float(data->width / 2.f);
	y1 = data->y - float(data->height / 2.f);
	x2 = data->x + float(data->width / 2.f);
	y2 = data->y + float(data->height / 2.f);

	glm::vec2 pos1 = MapPosToRenderPos(glm::vec2(x1, y1));
	glm::vec2 pos2 = MapPosToRenderPos(glm::vec2(x2, y2));

	UINT curIndex = UINT((currenttime - data->startTime) / (data->giftotalTime / framecount)) % framecount;
	ID2D1Bitmap* bitmap = data->gifInfo->getFrame(curIndex);
	if (bitmap != nullptr)
	{
		glm::vec2 center = MapPosToRenderPos(glm::vec2(data->x, data->y));

		_renderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(data->rotation, D2D1::Point2F(center.x, center.y)));
		_renderTarget->DrawBitmap(bitmap,
			D2D1::RectF(pos1.x, pos1.y, pos2.x, pos2.y),
			data->opacity);
		_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
	}
}

void Render::Renderer::processDebugLines(std::shared_ptr<D2DRenderContext::DebugLineRenderData> data)
{
	if (!data) return;

	if (!_redBrush)
		_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0, 1), &_redBrush);

	{
		glm::vec2 pos1 = MapPosToRenderPos(data->line_pos1);
		glm::vec2 pos2 = MapPosToRenderPos(data->line_pos2);

		_renderTarget->DrawLine(
			D2D1::Point2F(
				pos1.x, pos1.y),
			D2D1::Point2F(
				pos2.x, pos2.y),
			_redBrush);
	}
}

RenderOption Render::Renderer::GetOption() const {
	return _option;
}

void Render::Renderer::SetOption(RenderOption option) {
	_option = option;
	_optionChange = true;
}

bool Render::Renderer::GetNeedUpdateOpiton() const
{
	return _optionChange;
}

void Render::Renderer::SetNeedUpdateOpiton(bool value)
{
	_optionChange = value;
}
