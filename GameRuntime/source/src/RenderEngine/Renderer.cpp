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

void OpenGLRenderFrameDataAnalysisHelp::AnalysisRenderFrameData(std::shared_ptr<RenderFrameData>& framedata, RenderState& state)
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
		case OpenGLRenderContext::RenderContextType::Model:
		{
			processSceneModel(
				state,
				state.objects.sceneRenderData,
				std::static_pointer_cast<OpenGLRenderContext::SceneModelRenderData>(context->data)
			);
			break;
		}
		case OpenGLRenderContext::RenderContextType::DirLight:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::DirLightRenderData>(context->data);
			if (ptr && ptr->light)
			{
				auto info = std::make_shared<DirLightInfo>();
				info->light = ptr->light;
				info->renderCube = ptr->renderCube;
				dirLightInfos.push_back(std::move(info));
			}
			break;
		}
		case OpenGLRenderContext::RenderContextType::PointLight:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::PointLightRenderData>(context->data);
			if (ptr && ptr->light)
			{
				auto info = std::make_shared<PointLightInfo>();
				info->light = ptr->light;
				info->renderCube = ptr->renderCube;
				pointLightInfos.push_back(std::move(info));
			}
			break;
		}
		case OpenGLRenderContext::RenderContextType::SpotLight:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::SpotLightRenderData>(context->data);
			if (ptr && ptr->light)
			{
				auto info = std::make_shared<SpotLightInfo>();
				info->light = ptr->light;
				info->renderCube = ptr->renderCube;
				spotLightInfos.push_back(std::move(info));
			}
			break;
		}
		case OpenGLRenderContext::RenderContextType::FirstPersonModel:
		{
			processFirstPersonModel(
				state,
				state.objects.firstPersonRenderData,
				std::static_pointer_cast<OpenGLRenderContext::FirstPersonRenderData>(context->data)
			);
			break;
		}
		case OpenGLRenderContext::RenderContextType::Effect:
		{
			auto ptr = std::static_pointer_cast<OpenGLRenderContext::SceneEffectRenderData>(context->data);
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

void OpenGLRenderFrameDataAnalysisHelp::processSceneModel(
	RenderState& state,
	OpenGLRenderObjectData::SceneRenderData& renderData,
	const std::shared_ptr<OpenGLRenderContext::SceneModelRenderData>& data
)
{
	using OpaqueMeshItem = OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem;
	using TransparentMeshItem = OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem;
	using OpaqueSkinnedModelItem = OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem;
	using TransparentSkinnedMeshItem = OpenGLRenderObjectData::SceneRenderData::TransparentSkinnedMeshItem;

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
		auto shadredAnimatorViews = std::make_shared<std::vector<OpenGLRenderContext::AnimatorView>>(data->animatorViews);

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

void OpenGLRenderFrameDataAnalysisHelp::processFirstPersonModel(
	RenderState& state,
	OpenGLRenderObjectData::FirstPersonRenderData& renderData,
	const std::shared_ptr<OpenGLRenderContext::FirstPersonRenderData>& data)
{
	using OpaqueMeshItem = OpenGLRenderObjectData::FirstPersonRenderData::OpaqueMeshItem;
	using TransparentMeshItem = OpenGLRenderObjectData::FirstPersonRenderData::TransparentMeshItem;
	using OpaqueSkinnedModelItem = OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem;
	using TransparentSkinnedMeshItem = OpenGLRenderObjectData::FirstPersonRenderData::TransparentSkinnedMeshItem;

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
		auto shadredAnimatorViews = std::make_shared<std::vector<OpenGLRenderContext::AnimatorView>>(std::move(data->animatorViews));

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
	:_redBrush(nullptr), _optionChange(false), _isVulkanInit(false), _earlyThreadStop(true)
{
	SetRenderTarget(rt);
	SetBuffers(buffers);
}

Render::Renderer::~Renderer()
{
}

void Renderer::SetRenderTarget(ID2D1DeviceContext* rt)
{
	_renderTarget = rt;
}

void Renderer::SetBuffers(RenderTripleBufferPtr buffers)
{
	_buffers = buffers;
}

void Render::Renderer::EarlyProcessLoop()
{
	while (!_earlyThreadStop)
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

		OpenGLRenderFrameDataAnalysisHelp::AnalysisRenderFrameData(framedata, data.state);
		if (_optionChange)
		{
			render->SetOption(_option);
			_optionChange = false;
		}

		data.D2D_Contexts = std::move(framedata->D2D_Contexts);

		render->EarlyProcess(data.state);

		_earlyDataBuffers.submitWriteBuffer();
	}
}

void Renderer::renderFrame()
{
	if (!_buffers)
		return;
	if (_earlyThreadStop || !_earlyProcessThread)
	{
		_earlyThreadStop = false;
		_earlyProcessThread = std::make_shared<std::thread>(&Renderer::EarlyProcessLoop, this);
	}

	auto render = _vulkanRenderer;
	if (!render)
		return;

	auto& data = _earlyDataBuffers.acquireReadBuffer();

	if (_isVulkanInit)
		renderOpenGLFrame(data.render, data.state);

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

void Render::Renderer::renderOpenGLFrame(std::shared_ptr<VulkanRenderer>& render, RenderState& state)
{
	if (!render || !_sharedTexture)
		return;

	render->Draw(state);

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
}

void Render::Renderer::InitVulkanRender(uint32_t scr_width, uint32_t scr_height)
{
	if (_vulkanRenderer)
		return;

	auto r = std::make_shared<VulkanRenderer>();

	if (!_sharedTexture)
		_sharedTexture = CreateSharedTexture(g_pD3DDevice, scr_width, scr_height, DXGI_FORMAT_B8G8R8A8_UNORM);

	if (!_sharedTexture)
		return;

	std::vector<std::string> extensions;
	extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

	auto vulkanInstance = VulkanRenderer::CreateInstance(extensions);
	auto vkRenderer = VulkanRenderer::CreateForSharedTexture(vulkanInstance, _sharedTexture);

	if (vkRenderer != nullptr)
	{
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
