#include "Manager/ResourceManager.h"
#include "OpenGLRenderEngine/OpenGLRenderContextManager.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "resource.h"
#include "glm/gtc/matrix_transform.hpp"
#include "ThreadPool.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>

using namespace D2D1;

std::shared_ptr<AudioInfo> GetAudioFromResource(HINSTANCE hinstance, LPCWSTR resourceType, LPCWSTR resourceName)
{
	auto audio = std::make_shared<AudioInfo>();
	if (audio->LoadAudioFromResource(hinstance, resourceType, resourceName))
		return audio;
	return std::shared_ptr<AudioInfo>(nullptr);
}

ResourceManager::ResourceManager() {}

ResourceManager* ResourceManager::Instance() {
	static ResourceManager* m_Instance = new ResourceManager();
	return m_Instance;
}

bool CreateD3DContext(HWND hwnd)
{
	HRESULT hr = S_OK;

	// ============================================================
	// 1. 创建 D3D11 设备（带 BGRA 支持）
	// ============================================================
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};

	hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		createDeviceFlags,
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&g_pD3DDevice,
		nullptr,
		&g_pD3DContext
	);
	if (FAILED(hr)) {
		hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			createDeviceFlags,
			featureLevels,
			ARRAYSIZE(featureLevels),
			D3D11_SDK_VERSION,
			&g_pD3DDevice,
			nullptr,
			nullptr
		);
		if (FAILED(hr)) return false;
	}

	// 创建交换链

	RECT rect = RENDERCONTEXMANAGER->GetRECT();
	DXGI_SWAP_CHAIN_DESC1 scDesc = {};
	scDesc.Width = rect.right - rect.left;
	scDesc.Height = rect.bottom - rect.top;
	scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	scDesc.Stereo = FALSE;
	scDesc.SampleDesc.Count = 1;
	scDesc.SampleDesc.Quality = 0;
	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scDesc.BufferCount = 2;
	scDesc.Scaling = DXGI_SCALING_STRETCH;
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	//scDesc.Flags = 0;

	IDXGIDevice2* pDXGIDevice2 = nullptr;
	hr = g_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice2));
	if (FAILED(hr)) return false;

	IDXGIAdapter* pAdapter = nullptr;
	hr = pDXGIDevice2->GetAdapter(&pAdapter);
	pDXGIDevice2->Release();
	if (FAILED(hr)) return false;

	IDXGIFactory2* pFactory = nullptr;
	hr = pAdapter->GetParent(IID_PPV_ARGS(&pFactory));
	pAdapter->Release();
	if (FAILED(hr)) return false;

	IDXGISwapChain1* pSwapChain1 = nullptr;
	hr = pFactory->CreateSwapChainForHwnd(
		g_pD3DDevice,
		hwnd,
		&scDesc,
		nullptr,
		nullptr,
		&pSwapChain1
	);
	pFactory->Release();
	if (FAILED(hr)) return false;

	g_pSwapChain = pSwapChain1;

	return true;
}

bool Need() {
	static bool init = false;
	if (init) return true;

	HWND main_hwnd = RENDERCONTEXMANAGER->GetHwnd();

	HRESULT hr = S_OK;

	if (!CreateD3DContext(main_hwnd))
		return false;

	IDXGIDevice* pDXGIDevice = nullptr;
	hr = g_pD3DDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
	if (FAILED(hr)) return false;

	hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_MULTI_THREADED,
		IID_PPV_ARGS(&pD2DFactory)
	);
	if (FAILED(hr)) {
		pDXGIDevice->Release();
		return false;
	}

	ID2D1Device* pD2DDevice = nullptr;
	hr = pD2DFactory->CreateDevice(pDXGIDevice, &pD2DDevice);
	pDXGIDevice->Release();
	if (FAILED(hr)) return false;

	hr = pD2DDevice->CreateDeviceContext(
		D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
		&pRenderTarget
	);
	pD2DDevice->Release();
	if (FAILED(hr)) return false;

	// 获取后台缓冲区
	ID3D11Texture2D* pBackBuffer = nullptr;
	hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (FAILED(hr)) return false;

	IDXGISurface* pSurface = nullptr;
	hr = pBackBuffer->QueryInterface(IID_PPV_ARGS(&pSurface));
	pBackBuffer->Release();
	if (FAILED(hr)) return false;

	// 从 DXGI Surface 创建 D2D 位图作为渲染目标
	D2D1_BITMAP_PROPERTIES1 bitmapProps = {};
	bitmapProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
	bitmapProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

	ID2D1Bitmap1* pTargetBitmap = nullptr;
	hr = pRenderTarget->CreateBitmapFromDxgiSurface(
		pSurface,
		&bitmapProps,
		&pTargetBitmap
	);
	pSurface->Release();
	if (FAILED(hr)) return false;

	// 设置为渲染目标
	pRenderTarget->SetTarget(pTargetBitmap);
	pTargetBitmap->Release();

	if (pIDWriteFactory == nullptr) {
		DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&pIDWriteFactory)
		);
	}

	if (pIWICFactory == nullptr) {
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pIWICFactory)
		);
	}

	init = true;
	return true;
}

bool ResourceManager::InitD2DResource()
{
	Need();

	HINSTANCE hInst = RENDERCONTEXMANAGER->GetHinstance();

	ID2D1Bitmap* textBK = LoadResourceBitmap(hInst, pRenderTarget, L"PNG", MAKEINTRESOURCE(TEXTBK_PNG));
	ID2D1Bitmap* returnBP = LoadResourceBitmap(hInst, pRenderTarget, L"PNG", MAKEINTRESOURCE(RETURN_PNG));
	ID2D1Bitmap* pauseBP = LoadResourceBitmap(hInst, pRenderTarget, L"PNG", MAKEINTRESOURCE(PAUSE_PNG));
	ID2D1Bitmap* winBP = LoadResourceBitmap(hInst, pRenderTarget, L"PNG", MAKEINTRESOURCE(WIN_PNG));
	ID2D1Bitmap* failBP = LoadResourceBitmap(hInst, pRenderTarget, L"PNG", MAKEINTRESOURCE(FAIL_PNG));

	ID2D1Bitmap* opBK = LoadResourceBitmap(hInst, pRenderTarget, L"PNG", MAKEINTRESOURCE(OPBK_PNG));

	BitMapRes[ResName::textBK] = textBK;
	BitMapRes[ResName::returnBP] = returnBP;
	BitMapRes[ResName::pauseBP] = pauseBP;
	BitMapRes[ResName::winBP] = winBP;
	BitMapRes[ResName::failBP] = failBP;
	BitMapRes[ResName::opBK] = opBK;

	return true;
}

bool ResourceManager::InitAudioResource()
{
	HINSTANCE hInst = RENDERCONTEXMANAGER->GetHinstance();

	auto heal_audio = GetAudioFromResource(hInst, L"AUDIO", MAKEINTRESOURCE(Heal_Audio));
	auto default_Shoot_Audio = GetAudioFromResource(hInst, L"AUDIO", MAKEINTRESOURCE(Default_Shoot_Audio));
	auto default_Attacked_Audio = GetAudioFromResource(hInst, L"AUDIO", MAKEINTRESOURCE(Default_Attacked_Audio));
	auto Fire_Audio = GetAudioFromResource(hInst, L"AUDIO", MAKEINTRESOURCE(FIRE_AUDIO));

	auto menu_bgm = GetAudioFromResource(hInst, L"AUDIO", MAKEINTRESOURCE(MENU_BGM_AUDIO));
	auto game_bgm = GetAudioFromResource(hInst, L"AUDIO", MAKEINTRESOURCE(GAME_BGM_AUDIO));

	if (heal_audio) AUDIORes[ResName::healAudio] = heal_audio;
	if (default_Shoot_Audio) AUDIORes[ResName::defaultShootAudio] = default_Shoot_Audio;
	if (default_Attacked_Audio) AUDIORes[ResName::defaultAttackedAudio] = default_Attacked_Audio;
	if (Fire_Audio) AUDIORes[ResName::FireAudio] = Fire_Audio;

	if (menu_bgm) AUDIORes[ResName::MenuBGM] = menu_bgm;
	if (game_bgm) AUDIORes[ResName::GameBGM] = game_bgm;

	return true;
}

bool ResourceManager::InitOpenGLResource()
{
	std::call_once(initOpenGLFlag, std::bind(&ResourceManager::InitOpenGLResourceInternal, this));
	return true;
}

bool ResourceManager::InitOpenGLResourceInternal()
{
	ThreadPool pool;
	pool.start();

	auto InitSponza = [&]()->void
		{
			pool.submit([&]()->void {
				{

					auto model = std::make_shared<Model>("Test/sponza/Sponza.gltf");
					model->MakeScale(glm::vec3(0.03f));
					ModelRes[ResName::Sponza] = model;
				}
				});
		};

	auto InitDust = [&]()-> void {

		auto InitGun = [&]()-> void
			{
				pool.submit([&]()->void {
					{
						ModelRes[ResName::AK47] = std::make_shared<Model>("Test/Weapon/AK47/test.fbx");
						auto animations = AnimationLoader::LoadAnimationFile("Test/Weapon/AK47/test.fbx");
						AnimationRes[ResName::AK47_Anim_Static] = animations[0];
						AnimationRes[ResName::AK47_Anim_Draw] = animations[1];
						AnimationRes[ResName::AK47_Anim_Idle] = animations[2];
						AnimationRes[ResName::AK47_Anim_Reload] = animations[3];
						AnimationRes[ResName::AK47_Anim_Reload_Full] = animations[4];
						AnimationRes[ResName::AK47_Anim_Run] = animations[6];
						AnimationRes[ResName::AK47_Anim_Shot] = animations[7];
						AnimationRes[ResName::AK47_Anim_Walk] = animations[8];
					}
					});

				//pool.submit([&]()->void {
				//	{
				//		ModelRes[ResName::Pistol] = std::make_shared<Model>("Test/Weapon/pistol/Armpist.fbx");
				//		auto animations = AnimationLoader::LoadAnimationFile("Test/Weapon/pistol/Armpist.fbx");
				//		AnimationRes[ResName::Pistol_Anim_Static] = animations[6];
				//		AnimationRes[ResName::Pistol_Anim_Draw] = animations[6];
				//		AnimationRes[ResName::Pistol_Anim_Idle] = animations[6];
				//		AnimationRes[ResName::Pistol_Anim_Reload] = animations[7];
				//		AnimationRes[ResName::Pistol_Anim_Reload_Full] = animations[8];
				//		AnimationRes[ResName::Pistol_Anim_Run] = animations[9];
				//		AnimationRes[ResName::Pistol_Anim_Shot] = animations[3];
				//		AnimationRes[ResName::Pistol_Anim_Walk] = animations[9];
				//	}
				//	});

				//pool.submit([&]()->void {
				//	{
				//		ModelRes[ResName::Sniper] = std::make_shared<Model>("Test/Weapon/sniper/BoltActionRifle.fbx");
				//		auto animations = AnimationLoader::LoadAnimationFile("Test/Weapon/sniper/BoltActionRifle.fbx");
				//		AnimationRes[ResName::Sniper_Anim_Static] = animations[2];
				//		AnimationRes[ResName::Sniper_Anim_Draw] = animations[2];
				//		AnimationRes[ResName::Sniper_Anim_Idle] = animations[2];
				//		AnimationRes[ResName::Sniper_Anim_Reload] = animations[3];
				//		AnimationRes[ResName::Sniper_Anim_Reload_Full] = animations[4];
				//		AnimationRes[ResName::Sniper_Anim_Run] = animations[7];
				//		AnimationRes[ResName::Sniper_Anim_Shot] = animations[5];
				//		AnimationRes[ResName::Sniper_Anim_Walk] = animations[7];
				//	}
				//	});

				//pool.submit([&]()->void {
				//	{
				//		ModelRes[ResName::SMG] = std::make_shared<Model>("Test/Weapon/smg/test.fbx");
				//		auto animations = AnimationLoader::LoadAnimationFile("Test/Weapon/smg/test.fbx");
				//		//ModelRes[ResName::SMG] = std::make_shared<Model>("Test/Weapon/smg/PistolArm.fbx");
				//		//auto animations = AnimationLoader::LoadAnimationFile("Test/Weapon/smg/PistolArm.fbx");
				//		AnimationRes[ResName::SMG_Anim_Static] = animations[5];
				//		AnimationRes[ResName::SMG_Anim_Draw] = animations[7];
				//		AnimationRes[ResName::SMG_Anim_Idle] = animations[6];
				//		AnimationRes[ResName::SMG_Anim_Reload] = animations[10];
				//		AnimationRes[ResName::SMG_Anim_Reload_Full] = animations[10];
				//		AnimationRes[ResName::SMG_Anim_Run] = animations[11];
				//		AnimationRes[ResName::SMG_Anim_Shot] = animations[12];
				//		AnimationRes[ResName::SMG_Anim_Walk] = animations[13];
				//	}
				//	});
			};


		auto InitCharacter = [&]()-> void
			{

				pool.submit([&]()->void {
					{
						auto model = std::make_shared<Model>("Test/keqing/keqing.pmx");
						model->MakeScale(glm::vec3(0.25f));
						ModelRes[ResName::Keqing1] = model;
					}
					});

				pool.submit([&]()->void {
					{
						auto model = std::make_shared<Model>("Test/keqing2/keqing.pmx");
						model->MakeScale(glm::vec3(0.25f));
						ModelRes[ResName::Keqing2] = model;
					}
					});
			};

		auto InitScene = [&]()-> void
			{
				pool.submit([&]()->void {
					{
						auto model = std::make_shared<Model>("Test/newdust2/Generic_Item.obj");
						glm::mat4 mat = glm::mat4(1.0f);
						mat = glm::scale(mat, glm::vec3(10.f));
						model->MakeTransform(mat);
						ModelRes[ResName::NewDust2] = model;
					}
					});

				pool.submit([&]()->void {

					auto guard = THREADCONTEXT->GetBindGuard();

					std::array<std::string, 6> faces
					{
						"Test/skybox/box1/right.jpg",
						"Test/skybox/box1/left.jpg",
						"Test/skybox/box1/top.jpg",
						"Test/skybox/box1/bottom.jpg",
						"Test/skybox/box1/front.jpg",
						"Test/skybox/box1/back.jpg"
					};
					TextureCubeRes[ResName::skybox1] = std::make_shared<TextureCube>(faces, true);
					});
			};

		InitGun();
		InitCharacter();
		InitScene();
		};

	auto InitGameScene = [&]()-> void {
		pool.submit([&]()->void {

			auto guard = THREADCONTEXT->GetBindGuard();

			std::array<std::string, 6> faces
			{
				"Test/skybox/box2/right.png",
				"Test/skybox/box2/left.png",
				"Test/skybox/box2/top.png",
				"Test/skybox/box2/bottom.png",
				"Test/skybox/box2/front.png",
				"Test/skybox/box2/back.png"
			};
			TextureCubeRes[ResName::skybox2] = std::make_shared<TextureCube>(faces, true);
			});

		pool.submit([&]()->void {
			{
				auto model = std::make_shared<Model>("Test/keqing2/keqing.pmx");
				model->MakeScale(glm::vec3(0.25f));
				ModelRes[ResName::Keqing2] = model;
			}
			});

		};

	pool.submit([&]()->void {

		auto guard = THREADCONTEXT->GetBindGuard();

		ModelRes[ResName::Quad] = GetFloorModel();
		ModelRes[ResName::Cube] = GetCubeModel(glm::vec3(1.0f), 1.0f);
		ModelRes[ResName::Sphere] = GetSphereModel(0.5f, 72, 36);
		ModelRes[ResName::Cylinder] = GetCylinderModel(0.5f, 1.0f, 36);
		Texture2DRes[ResName::EmptyTexture] = std::make_shared<Texture2D>(1, 1);
		});


	//InitDust();
	InitGameScene();
	//InitSponza();

	pool.stop();

	return true;
}

ID2D1Bitmap* ResourceManager::GetBitMapRes(const std::string& name) {
	auto it = BitMapRes.find(name);
	if (it == BitMapRes.end())
		return nullptr;
	return it->second;
}

GIFINFO* ResourceManager::GetGIFRes(const std::string& name) {
	auto it = GIFRes.find(name);
	if (it == GIFRes.end())
		return nullptr;
	return it->second;
}

std::shared_ptr<AudioInfo> ResourceManager::GetAudioRes(const std::string& name)
{
	auto it = AUDIORes.find(name);
	if (it == AUDIORes.end())
		return std::shared_ptr<AudioInfo>(nullptr);
	return it->second;
}

std::shared_ptr<Model> ResourceManager::GetModelRes(const std::string& name)
{
	auto it = ModelRes.find(name);
	if (it == ModelRes.end())
		return nullptr;
	return it->second;
}

std::shared_ptr<Texture2D> ResourceManager::GetTexture2DRes(const std::string& name)
{
	auto it = Texture2DRes.find(name);
	if (it == Texture2DRes.end())
		return nullptr;
	return it->second;
}

std::shared_ptr<TextureCube> ResourceManager::GetTextureCubeRes(const std::string& name)
{
	auto it = TextureCubeRes.find(name);
	if (it == TextureCubeRes.end())
		return 0;
	return it->second;
}

std::shared_ptr<Animation> ResourceManager::GetAnimationRes(const std::string& name)
{
	auto it = AnimationRes.find(name);
	if (it == AnimationRes.end())
		return 0;
	return it->second;
}
