#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "CommonData/AssetGUIDDef.h"

#include <functional>
#include <mutex>

// 2D 层（Renderer2D）与 UI 层（RendererUI）共用的图像资源。
//   Sprite        ：一张图片 + 若干命名区域（rect / pivot / 九宫格边距）
//   AnimatedImage ：动图（目前从 GIF 导入），加载时解码、合成整帧并拼成图集，不落盘
// 两者在运行时都解析成同一个 SpriteFrame（贴图 + UV + 像素尺寸 + 轴心 + 边距），绘制端只认 SpriteFrame。
namespace Render2D
{
	// Sprite 资产里的一个命名区域。坐标是贴图像素，原点在贴图左上角
	struct SpriteRegion
	{
		std::string name;
		glm::vec4 rect = glm::vec4(0.0f);		// x, y, w, h；w 或 h <= 0 表示整张贴图
		glm::vec2 pivot = glm::vec2(0.5f);		// 归一化轴心：(0,0) 左上，(1,1) 右下
		glm::vec4 border = glm::vec4(0.0f);		// 九宫格边距（像素）：left, top, right, bottom
	};

	// 解析后的一帧
	struct SpriteFrame
	{
		std::shared_ptr<Texture2D> texture;
		glm::vec4 uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);	// u0, v0, u1, v1
		glm::vec2 size = glm::vec2(0.0f);						// 区域的像素尺寸
		glm::vec2 pivot = glm::vec2(0.5f);
		glm::vec4 border = glm::vec4(0.0f);						// 像素：left, top, right, bottom

		bool IsValid() const { return texture && !texture->IsEmpty() && size.x > 0.0f && size.y > 0.0f; }
		bool HasBorder() const { return border.x > 0.0f || border.y > 0.0f || border.z > 0.0f || border.w > 0.0f; }
	};

	// 2D 图像（Sprite / 动图）的贴图配置：导入配置里只取采样过滤和 sRGB，
	// 环绕固定 ClampToEdge、不开各向异性（区域 / 帧挨在一起，否则会采到相邻的像素）
	TextureConfig MakeImageTextureConfig(const TextureConfig& importConfig);
	// 按 2D 图像配置加载一张图片（不生成 mip），失败返回空
	std::shared_ptr<Texture2D> LoadImageTexture(const std::string& fullPath, const TextureConfig& importConfig, std::string* error = nullptr);

	// ---------- CPU 解码工具：只要像素、不建贴图的场合用（例如编辑器缩略图） ----------
	struct ImageRGBA
	{
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<uint8_t> pixels;	// width * height * 4，RGBA8，非预乘 alpha
	};
	// 解码一张图片为 RGBA8：DDS 和引擎加载贴图一样走 DirectXTex（取第一层 mip / 第一张切片），GIF 取第一帧，其余走 stb
	bool DecodeImageFile(const std::string& path, ImageRGBA& out, std::string* error = nullptr);

	struct GifInfo
	{
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t frameCount = 0;
	};
	// 每合成一帧回调一次：整张画布 RGBA8（width * height * 4），回调返回后缓冲区会被复用；
	// delayMs 已经按和浏览器一样的规则处理过（0 / 过短的按 100ms）。回调返回 false 时中止解码
	using GifFrameCallback = std::function<bool(const uint8_t* rgba, uint32_t width, uint32_t height, float delayMs)>;
	// 逐帧解码 GIF（不会把所有整帧都留在内存里）。info 在第一次回调之前就已经填好（帧数是预扫描得到的）
	bool DecodeGifFile(const std::string& path, const GifFrameCallback& onFrame, GifInfo* info = nullptr, std::string* error = nullptr);

	// RGBA8（非预乘 alpha）缩放，dst 需要 dstW * dstH * 4 字节
	void ResizeRGBA(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, uint32_t dstW, uint32_t dstH);

	class Sprite
	{
	public:
		Sprite() = default;
		Sprite(std::shared_ptr<Texture2D> texture, std::vector<SpriteRegion> regions);

		std::shared_ptr<Texture2D> GetTexture() const;
		void SetTexture(std::shared_ptr<Texture2D> texture);

		std::vector<SpriteRegion> GetRegions() const;
		void SetRegions(std::vector<SpriteRegion> regions);
		std::vector<std::string> GetRegionNames() const;
		bool HasRegion(const std::string& name) const;

		// 区域名为空或找不到时用整张贴图
		SpriteFrame Resolve(const std::string& region = {}) const;

		static SpriteFrame MakeFrame(const std::shared_ptr<Texture2D>& texture, const SpriteRegion* region);

		void SetAssetGUID(const AssetGUID& guid) { _assetGUID = guid; }
		const AssetGUID& GetAssetGUID() const { return _assetGUID; }

	private:
		mutable std::mutex _mutex;		// 编辑器改区域（主线程）和场景线程收集渲染数据可能同时发生
		std::shared_ptr<Texture2D> _texture;
		std::vector<SpriteRegion> _regions;
		AssetGUID _assetGUID;
	};

	class AnimatedImage
	{
	public:
		struct Frame
		{
			uint32_t page = 0;				// 在第几张图集页
			glm::vec4 uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
			float durationMs = 100.0f;
		};

		// 解码动图并拼成图集（GIF；其它格式按单帧处理）。config 里只用采样过滤和 sRGB
		static std::shared_ptr<AnimatedImage> LoadFromFile(const std::string& path, const TextureConfig& config, std::string* error = nullptr);
		// 从已经合成好的 RGBA8 整帧构建（每帧 width * height * 4 字节）。
		// createSampler = false：图集页不建采样器，只能交给 ImGui 显示（编辑器缩略图），不能交给 2D 渲染器
		static std::shared_ptr<AnimatedImage> FromFrames(const std::vector<const uint8_t*>& rgbaFrames, uint32_t width, uint32_t height,
			const std::vector<float>& durationsMs, const TextureConfig& config, std::string* error = nullptr, bool createSampler = true);

		uint32_t GetFrameCount() const { return static_cast<uint32_t>(_frames.size()); }
		const Frame& GetFrame(uint32_t index) const { return _frames[index]; }
		glm::uvec2 GetFrameSize() const { return _frameSize; }
		float GetTotalDuration() const { return _totalDuration; }
		const std::vector<std::shared_ptr<Texture2D>>& GetPages() const { return _pages; }

		// loopCount <= 0 表示无限循环；有限循环播完后停在最后一帧
		uint32_t FrameIndexAt(float timeMs, int loopCount) const;
		bool IsFinished(float timeMs, int loopCount) const;
		SpriteFrame Evaluate(float timeMs, int loopCount, const glm::vec2& pivot = glm::vec2(0.5f)) const;
		SpriteFrame GetFrameSprite(uint32_t index, const glm::vec2& pivot = glm::vec2(0.5f)) const;

		void SetAssetGUID(const AssetGUID& guid) { _assetGUID = guid; }
		const AssetGUID& GetAssetGUID() const { return _assetGUID; }

		// 资产改了导入配置、换成了新对象时把旧对象标记为过期；引用方看到过期就重新解析
		void MarkStale() { _stale.store(true, std::memory_order_release); }
		bool IsStale() const { return _stale.load(std::memory_order_acquire); }

	private:
		std::vector<std::shared_ptr<Texture2D>> _pages;
		std::vector<Frame> _frames;
		glm::uvec2 _frameSize = glm::uvec2(0);
		float _totalDuration = 0.0f;
		AssetGUID _assetGUID;
		std::atomic<bool> _stale{ false };
	};
}
