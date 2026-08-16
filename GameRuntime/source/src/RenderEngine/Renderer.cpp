#include "RenderEngine/Renderer.h"
#include "RenderEngine/D2DTools.h"
#include "Helper/Tools.h"
#include "OpenGLRenderEngine/OpenGLRenderContextManager.h"
#include "OpenGLRenderEngine/SharedTexture.h"
#include <glm/gtc/matrix_access.hpp>
#include <memory>
#include <algorithm>
#include <ranges>

using namespace Render;

SharedTexture g_sharedTexture;

glm::vec2 MapPosToRenderPos(const glm::vec2& mapPos)
{
	RECT rect = RENDERCONTEXMANAGER->GetRECT();


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

Renderer::Renderer(ID2D1DeviceContext* rt, RenderTripleBufferPtr buffers)
	:_redBrush(nullptr), _usebitmap1(false), _optionChange(false), _isOpenGLInit(false)
{
	SetRenderTarget(rt);
	SetBuffers(buffers);

	ID2D1DeviceContext* pDeviceContext = nullptr;
	HRESULT hr = _renderTarget->QueryInterface(IID_PPV_ARGS(&pDeviceContext));
	if (SUCCEEDED(hr) && pDeviceContext)
	{
		pDeviceContext->Release();
		_usebitmap1 = true;
	}
}

Render::Renderer::~Renderer()
{
	if (_openGLBitmap)
		_openGLBitmap->Release();
	if (_openGLBitmap1)
		_openGLBitmap1->Release();
}

void Renderer::SetRenderTarget(ID2D1DeviceContext* rt)
{
	_renderTarget = rt;
}

void Renderer::SetBuffers(RenderTripleBufferPtr buffers)
{
	_buffers = buffers;
}

void Renderer::renderFrame(float delatTime)
{
	if (!_buffers)
		return;

	auto framedata = _buffers->acquireReadBuffer();

	if (_isOpenGLInit)
	{
		RENDERCONTEXMANAGER->WithMainOpenGLShared([&]()-> void
			{
				auto gurad = THREADCONTEXT->GetBindGuard();
				renderOpenGLFrame(delatTime, framedata);
			}
		);
	}

	renderD2DFrame(delatTime, framedata);
}

void Render::Renderer::renderD2DFrame(float delatTime, std::shared_ptr<RenderFrameData>& framedata)
{
	if (!framedata)
		return;

	auto& contexts = framedata->D2D_Contexts;

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

void Render::Renderer::renderOpenGLFrame(float delatTime, std::shared_ptr<RenderFrameData>& framedata)
{
	if (!framedata)
		return;

	RenderState state = RenderStateBuilder()
		.SetCamera(framedata->projection, framedata->view,
			framedata->position, framedata->direction, framedata->directionUp, framedata->directionRight,
			framedata->nearPlane, framedata->farPlane, framedata->fov)
		.Build();

	AnalysisRenderFrameData(framedata, state);

	if (auto r = _openglRenderer)
	{
		if (_optionChange)
		{
			r->SetOption(_option);
			_optionChange = false;
		}

		if (g_sharedTexture.InteropDevice)
		{
			auto res = wglDXLockObjectsNV(g_sharedTexture.InteropDevice, 1, &g_sharedTexture.InteropObject);
			r->Draw(state);
			res = wglDXUnlockObjectsNV(g_sharedTexture.InteropDevice, 1, &g_sharedTexture.InteropObject);

			ID3D11Texture2D* pBackBuffer = nullptr;
			auto hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
			if (SUCCEEDED(hr))
			{
				g_pD3DContext->CopyResource(pBackBuffer, g_sharedTexture.d3dTexture);	// 将共享纹理复制到后台缓冲区
				pBackBuffer->Release();
			}
		}
		else
		{
			r->Draw(state);

			_renderTarget->BeginDraw();
			if (_usebitmap1)
			{
				ConvertGLTextureToD2DBitmap1();
				auto bitmap = _openGLBitmap1;
				if (bitmap)
				{
					RECT main_rect = RENDERCONTEXMANAGER->GetRECT();
					_renderTarget->DrawBitmap(bitmap,
						D2D1::RectF(main_rect.left, main_rect.top, main_rect.right, main_rect.bottom),
						1.f);
				}
			}
			else
			{
				ConvertGLTextureToD2DBitmap();
				auto bitmap = _openGLBitmap;
				if (bitmap)
				{
					RECT main_rect = RENDERCONTEXMANAGER->GetRECT();
					_renderTarget->DrawBitmap(bitmap,
						D2D1::RectF(main_rect.left, main_rect.top, main_rect.right, main_rect.bottom),
						1.f);
				}
			}
			_renderTarget->EndDraw();
		}
	}
}

void Render::Renderer::InitOpenGLRender(int scr_width, int scr_height)
{
	if (_openglRenderer)
		return;

	auto r = std::make_shared<OpenGLRenderer>();

	if (!g_sharedTexture.InteropDevice)
		CreateSharedTexture(g_pD3DDevice, scr_width, scr_height, DXGI_FORMAT_B8G8R8A8_UNORM, &g_sharedTexture);

	if (g_sharedTexture.InteropDevice)
		r->Init(scr_width, scr_height, &g_sharedTexture);
	else
		r->Init(scr_width, scr_height);

	_openglRenderer = r;

	_isOpenGLInit = true;
}

int Render::Renderer::GetOpenGLWidth()
{
	auto r = _openglRenderer;
	if (!r)
		return 0;
	return r->GetWidth();
}

int Render::Renderer::GetOpenGLHeight()
{
	auto r = _openglRenderer;
	if (!r)
		return 0;
	return r->GetHeight();
}

std::shared_ptr<OpenGLRenderer> Render::Renderer::GetOpenGLRender()
{
	return _openglRenderer;
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

void Render::Renderer::AnalysisRenderFrameData(std::shared_ptr<RenderFrameData>& framedata, RenderState& state)
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

void Renderer::processSceneModel(
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
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
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
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
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

void Renderer::processFirstPersonModel(
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
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
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
			if (opacity <= 0.f)
				continue;

			bool isTransprant = opacity < 1.0f || mode == AlphaMode::Blend;
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

void Render::Renderer::ConvertGLTextureToD2DBitmap()
{
	if (!_openglRenderer)
		return;

	GLuint textureId = _openglRenderer->GetColorBuffer();
	int width = _openglRenderer->GetWidth();
	int height = _openglRenderer->GetHeight();

	// 1. 读取 OpenGL 纹理数据
	std::vector<BYTE> pixels(width * height * 4);

	glBindTexture(GL_TEXTURE_2D, textureId);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

	// 2. 转换颜色格式（RGBA -> BGRA）
	for (int i = 0; i < width * height; i++) {
		int index = i * 4;
		std::swap(pixels[index], pixels[index + 2]); // R <-> B
	}

	// 3. 翻转 Y 轴（OpenGL -> Direct2D）
	int rowBytes = width * 4;
	for (int y = 0; y < height / 2; y++) {
		int srcY = y;
		int destY = (height - 1) - y;
		int srcIdx = y * rowBytes;
		int destIdx = destY * rowBytes;
		std::swap_ranges(
			pixels.begin() + srcIdx,
			pixels.begin() + srcIdx + rowBytes,
			pixels.begin() + destIdx
		);
	}

	if (_openGLBitmap)
	{
		_openGLBitmap->Release();
		_openGLBitmap = NULL;
	}

	D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
	);

	ID2D1Bitmap* d2dBitmap = nullptr;
	HRESULT hr = _renderTarget->CreateBitmap(
		D2D1::SizeU(width, height),
		pixels.data(),
		width * 4,  // stride
		props,
		&d2dBitmap
	);

	if (FAILED(hr))
		d2dBitmap = nullptr;

	_openGLBitmap = d2dBitmap;
}

void Render::Renderer::ConvertGLTextureToD2DBitmap1()
{

	if (!_openglRenderer)
		return;

	GLuint textureId = _openglRenderer->GetColorBuffer();
	int width = _openglRenderer->GetWidth();
	int height = _openglRenderer->GetHeight();

	// 1. 读取 OpenGL 纹理数据
	std::vector<BYTE> pixels(width * height * 4);

	glBindTexture(GL_TEXTURE_2D, textureId);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

	// 2. 转换颜色格式（RGBA -> BGRA）
	for (int i = 0; i < width * height; i++) {
		int index = i * 4;
		std::swap(pixels[index], pixels[index + 2]); // R <-> B
	}

	// 3. 翻转 Y 轴（OpenGL -> Direct2D）
	int rowBytes = width * 4;
	for (int y = 0; y < height / 2; y++) {
		int srcY = y;
		int destY = (height - 1) - y;
		int srcIdx = y * rowBytes;
		int destIdx = destY * rowBytes;
		std::swap_ranges(
			pixels.begin() + srcIdx,
			pixels.begin() + srcIdx + rowBytes,
			pixels.begin() + destIdx
		);
	}

	if (!_openGLBitmap1)
	{
		ID2D1DeviceContext* pDeviceContext = nullptr;
		HRESULT hr = _renderTarget->QueryInterface(IID_PPV_ARGS(&pDeviceContext));
		if (SUCCEEDED(hr) && pDeviceContext)
		{
			D2D1_BITMAP_PROPERTIES1 props1 = D2D1::BitmapProperties1(
				D2D1_BITMAP_OPTIONS_TARGET,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
			);

			ID2D1Bitmap1* d2dBitmap = nullptr;
			hr = pDeviceContext->CreateBitmap(
				D2D1::SizeU(width, height),
				pixels.data(),
				width * 4,  // stride
				props1,
				&d2dBitmap);

			if (FAILED(hr))
				d2dBitmap = nullptr;

			_openGLBitmap1 = d2dBitmap;
			pDeviceContext->Release();
		}
	}
	else
	{
		D2D1_RECT_U dstRect = D2D1::RectU(0, 0, width, height);
		HRESULT hr = _openGLBitmap1->CopyFromMemory(
			&dstRect,                          // 目标区域
			pixels.data(),
			width * 4                          // stride
		);
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
