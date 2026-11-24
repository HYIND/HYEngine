#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Sprite2D.h"

// 静态图解码用 stb（本编译单元私有一份实现，和 Texture2D.cpp 等文件一样走 STB_IMAGE_STATIC）；GIF 用下面自己写的解码
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>

#include <cstring>
#include <fstream>
#include <filesystem>

using namespace Render2D;

namespace
{
	// 一张图集页的边长上限（所有设备都支持的保守值）
	constexpr uint32_t kMaxAtlasDimension = 4096;
	// 每帧四周挤出 1 像素的边，线性采样时相邻帧不会互相渗色
	constexpr uint32_t kFramePadding = 1;
	// 和浏览器一致：GIF 里写 0 / 10ms 的帧按 100ms 播
	constexpr float kMinFrameDurationMs = 20.0f;
	constexpr float kFallbackFrameDurationMs = 100.0f;

	bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out)
	{
		// 路径按 UTF-8 解释（工程路径可能带中文）
		std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())), std::ios::binary | std::ios::ate);
		if (!file.is_open())
			return false;
		const std::streamsize size = file.tellg();
		if (size <= 0)
			return false;
		out.resize(static_cast<size_t>(size));
		file.seekg(0, std::ios::beg);
		return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
	}

	void SetError(std::string* error, std::string message)
	{
		if (error)
			*error = std::move(message);
	}

	// ---------- GIF 解码 ----------
	// 不用 stbi_load_gif：stb 把处置方式 2 当成“恢复到画上一帧之前的样子”，而规范（和浏览器）是把该帧区域清成透明，
	// 结果第一帧会一直垫在后面的帧下面；它处理处置方式 3 时取上一帧的指针也算错了。
	// 这里按规范实现：0/1 保留画布；2 把该帧区域清成透明；3 恢复到画这一帧之前的画布。
	// 画布初始全透明（和浏览器一致，不用逻辑屏幕的背景色）。
	struct GifFrames
	{
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<std::vector<uint8_t>> frames;	// 每帧 width * height * 4，RGBA8
		std::vector<float> durationsMs;
	};

	class GifReader
	{
	public:
		GifReader(const uint8_t* data, size_t size) : _data(data), _size(size) {}

		bool Eof() const { return _pos >= _size; }
		bool Overrun() const { return _overrun; }
		size_t Remaining() const { return _pos < _size ? _size - _pos : 0; }

		uint8_t U8()
		{
			if (_pos >= _size)
			{
				_overrun = true;
				return 0;
			}
			return _data[_pos++];
		}

		uint16_t U16()
		{
			const uint16_t lo = U8();
			const uint16_t hi = U8();
			return static_cast<uint16_t>(lo | (hi << 8));
		}

		void Skip(size_t n)
		{
			if (n > Remaining())
			{
				_overrun = true;
				_pos = _size;
				return;
			}
			_pos += n;
		}

		void ReadPalette(size_t count, std::vector<uint8_t>& out)
		{
			out.resize(count * 3);
			for (auto& c : out)
				c = U8();
		}

		// 读一串数据子块（长度字节 + 数据，直到长度为 0）；out 为空时只跳过
		void SubBlocks(std::vector<uint8_t>* out)
		{
			for (;;)
			{
				if (Eof())
				{
					_overrun = true;
					return;
				}
				const uint8_t len = U8();
				if (len == 0)
					return;
				if (out)
				{
					const size_t n = std::min<size_t>(len, Remaining());
					out->insert(out->end(), _data + _pos, _data + _pos + n);
				}
				Skip(len);
			}
		}

	private:
		const uint8_t* _data = nullptr;
		size_t _size = 0;
		size_t _pos = 0;
		bool _overrun = false;
	};

	// 解一帧的 LZW 数据，输出调色板索引。返回实际解出的像素数（数据截断时会少于 pixelCount）
	size_t DecodeGifLzw(const std::vector<uint8_t>& data, int minCodeSize, size_t pixelCount, std::vector<uint8_t>& out)
	{
		out.assign(pixelCount, 0);
		if (minCodeSize < 1 || minCodeSize > 11)
			return 0;

		constexpr int kMaxCodes = 4096;
		constexpr uint16_t kNoPrefix = 0xFFFF;
		const int clearCode = 1 << minCodeSize;
		const int endCode = clearCode + 1;

		std::vector<uint16_t> prefix(kMaxCodes, kNoPrefix);
		std::vector<uint8_t> suffix(kMaxCodes, 0);
		std::vector<uint8_t> first(kMaxCodes, 0);
		std::vector<uint8_t> stack(kMaxCodes + 1);
		for (int i = 0; i < clearCode; ++i)
			suffix[i] = first[i] = static_cast<uint8_t>(i);

		int codeSize = minCodeSize + 1;
		int nextCode = clearCode + 2;
		int prev = -1;
		uint32_t bitBuffer = 0;
		int bitCount = 0;
		size_t pos = 0;
		size_t written = 0;

		while (written < pixelCount)
		{
			while (bitCount < codeSize)
			{
				if (pos >= data.size())
					return written;
				bitBuffer |= static_cast<uint32_t>(data[pos++]) << bitCount;
				bitCount += 8;
			}
			const int code = static_cast<int>(bitBuffer & ((1u << codeSize) - 1u));
			bitBuffer >>= codeSize;
			bitCount -= codeSize;

			if (code == clearCode)
			{
				codeSize = minCodeSize + 1;
				nextCode = clearCode + 2;
				prev = -1;
				continue;
			}
			if (code == endCode)
				break;

			if (prev < 0)
			{
				// 清除码之后的第一个码必须是字面值
				if (code >= clearCode)
					break;
			}
			else
			{
				if (code > nextCode)
					break;	// 数据损坏
				if (nextCode < kMaxCodes)
				{
					// 新条目 = 上一个串 + 当前串的首字符（当前码就是这个新条目时，首字符取上一个串的）
					prefix[nextCode] = static_cast<uint16_t>(prev);
					first[nextCode] = first[prev];
					suffix[nextCode] = (code == nextCode) ? first[prev] : first[code];
					++nextCode;
					if (nextCode == (1 << codeSize) && codeSize < 12)
						++codeSize;
				}
				else if (code == nextCode)
				{
					break;
				}
			}

			int c = code;
			int n = 0;
			while (prefix[c] != kNoPrefix && n < kMaxCodes)
			{
				stack[n++] = suffix[c];
				c = prefix[c];
			}
			stack[n++] = suffix[c];
			while (n > 0 && written < pixelCount)
				out[written++] = stack[--n];

			prev = code;
		}
		return written;
	}

	bool DecodeGif(const std::vector<uint8_t>& bytes, GifFrames& result, std::string& error)
	{
		if (bytes.size() < 13 || std::memcmp(bytes.data(), "GIF", 3) != 0)
		{
			error = "不是 GIF 文件";
			return false;
		}

		GifReader reader(bytes.data(), bytes.size());
		reader.Skip(6);
		const uint32_t canvasW = reader.U16();
		const uint32_t canvasH = reader.U16();
		const uint8_t screenFlags = reader.U8();
		reader.Skip(2);	// 背景色索引、像素宽高比：不使用
		if (canvasW == 0 || canvasH == 0)
		{
			error = "GIF 画布尺寸为 0";
			return false;
		}

		std::vector<uint8_t> globalPalette;
		if (screenFlags & 0x80)
			reader.ReadPalette(size_t(2) << (screenFlags & 7), globalPalette);

		const size_t canvasPixels = static_cast<size_t>(canvasW) * canvasH;
		std::vector<uint8_t> canvas(canvasPixels * 4u, 0u);
		std::vector<uint8_t> saved;		// 处置方式 3：画这一帧之前的画布
		std::vector<uint8_t> lzwData;
		std::vector<uint8_t> indices;
		std::vector<uint8_t> localPalette;

		// 图形控制扩展，只作用于紧跟着的那一帧
		int disposal = 0;
		bool hasTransparent = false;
		uint8_t transparentIndex = 0;
		float delayMs = 0.0f;

		result = {};
		result.width = canvasW;
		result.height = canvasH;

		bool ended = false;
		while (!ended && !reader.Eof())
		{
			const uint8_t tag = reader.U8();
			if (tag == 0x21)	// 扩展块
			{
				const uint8_t label = reader.U8();
				if (label == 0xF9)
				{
					const uint8_t len = reader.U8();
					if (len >= 4)
					{
						const uint8_t flags = reader.U8();
						delayMs = static_cast<float>(reader.U16()) * 10.0f;
						transparentIndex = reader.U8();
						disposal = (flags >> 2) & 7;
						hasTransparent = (flags & 1) != 0;
						reader.Skip(len - 4u);
					}
					else
					{
						reader.Skip(len);
					}
				}
				reader.SubBlocks(nullptr);
			}
			else if (tag == 0x2C)	// 图像
			{
				const uint32_t fx = reader.U16();
				const uint32_t fy = reader.U16();
				const uint32_t fw = reader.U16();
				const uint32_t fh = reader.U16();
				const uint8_t flags = reader.U8();
				const std::vector<uint8_t>* palette = &globalPalette;
				if (flags & 0x80)
				{
					reader.ReadPalette(size_t(2) << (flags & 7), localPalette);
					palette = &localPalette;
				}
				const int minCodeSize = reader.U8();
				lzwData.clear();
				reader.SubBlocks(&lzwData);
				if (reader.Overrun() && lzwData.empty())
					break;

				if (disposal == 3)
					saved = canvas;

				const size_t framePixels = static_cast<size_t>(fw) * fh;
				const size_t decoded = framePixels > 0 ? DecodeGifLzw(lzwData, minCodeSize, framePixels, indices) : 0;
				const size_t paletteCount = palette->size() / 3;
				const bool interlaced = (flags & 0x40) != 0;

				// 隔行扫描：解出来的第 i 行对应帧内的哪一行（四遍：0,8,16.. / 4,12.. / 2,6.. / 1,3..）
				auto rowOf = [&](uint32_t i) -> uint32_t
				{
					if (!interlaced)
						return i;
					const uint32_t pass1 = (fh + 7) / 8;
					if (i < pass1)
						return i * 8;
					i -= pass1;
					const uint32_t pass2 = (fh + 3) / 8;
					if (i < pass2)
						return 4 + i * 8;
					i -= pass2;
					const uint32_t pass3 = (fh + 1) / 4;
					if (i < pass3)
						return 2 + i * 4;
					i -= pass3;
					return 1 + i * 2;
				};

				for (size_t p = 0; p < decoded; ++p)
				{
					const uint8_t index = indices[p];
					if ((hasTransparent && index == transparentIndex) || index >= paletteCount)
						continue;
					const uint32_t x = fx + static_cast<uint32_t>(p % fw);
					const uint32_t y = fy + rowOf(static_cast<uint32_t>(p / fw));
					if (x >= canvasW || y >= canvasH)
						continue;	// 帧区域超出画布的部分裁掉
					uint8_t* dst = canvas.data() + (static_cast<size_t>(y) * canvasW + x) * 4u;
					const uint8_t* color = palette->data() + static_cast<size_t>(index) * 3u;
					dst[0] = color[0];
					dst[1] = color[1];
					dst[2] = color[2];
					dst[3] = 255;
				}

				result.frames.push_back(canvas);
				result.durationsMs.push_back(delayMs);

				// 按这一帧的处置方式准备下一帧的画布
				if (disposal == 2)
				{
					const uint32_t x1 = std::min(fx + fw, canvasW);
					const uint32_t y1 = std::min(fy + fh, canvasH);
					for (uint32_t y = fy; y < y1 && fx < x1; ++y)
						std::memset(canvas.data() + (static_cast<size_t>(y) * canvasW + fx) * 4u, 0, static_cast<size_t>(x1 - fx) * 4u);
				}
				else if (disposal == 3 && !saved.empty())
				{
					canvas.swap(saved);
				}

				disposal = 0;
				hasTransparent = false;
				delayMs = 0.0f;
			}
			else
			{
				// 0x3B 是结束符；其它未知块多半是文件尾部的垃圾数据，已解出的帧照常使用
				ended = true;
			}
		}

		if (result.frames.empty())
		{
			error = "GIF 里没有可用的帧";
			return false;
		}
		return true;
	}
}

// ============================== 2D 图像贴图 ==============================

TextureConfig Render2D::MakeImageTextureConfig(const TextureConfig& importConfig)
{
	TextureConfig config;
	config.minFilter = importConfig.minFilter;
	config.magFilter = importConfig.magFilter;
	config.gammaCorrection = importConfig.gammaCorrection;
	config.wrapU = config.wrapV = config.wrapW = vk::SamplerAddressMode::eClampToEdge;
	config.anisotropy = false;
	return config;
}

std::shared_ptr<Texture2D> Render2D::LoadImageTexture(const std::string& fullPath, const TextureConfig& importConfig, std::string* error)
{
	std::error_code ec;
	if (fullPath.empty() || !std::filesystem::exists(std::filesystem::path(reinterpret_cast<const char8_t*>(fullPath.c_str())), ec))
	{
		SetError(error, std::format("图片文件不存在：{}", fullPath));
		return nullptr;
	}
	auto texture = std::make_shared<Texture2D>(fullPath, MakeImageTextureConfig(importConfig), false);
	if (!texture || texture->IsEmpty())
	{
		SetError(error, std::format("加载图片失败：{}", fullPath));
		return nullptr;
	}
	return texture;
}

// ============================== Sprite ==============================

Sprite::Sprite(std::shared_ptr<Texture2D> texture, std::vector<SpriteRegion> regions)
	: _texture(std::move(texture)), _regions(std::move(regions))
{}

std::shared_ptr<Texture2D> Sprite::GetTexture() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _texture;
}

void Sprite::SetTexture(std::shared_ptr<Texture2D> texture)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_texture = std::move(texture);
}

std::vector<SpriteRegion> Sprite::GetRegions() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _regions;
}

void Sprite::SetRegions(std::vector<SpriteRegion> regions)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_regions = std::move(regions);
}

std::vector<std::string> Sprite::GetRegionNames() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	std::vector<std::string> names;
	names.reserve(_regions.size());
	for (const auto& region : _regions)
		names.push_back(region.name);
	return names;
}

bool Sprite::HasRegion(const std::string& name) const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return std::any_of(_regions.begin(), _regions.end(), [&](const SpriteRegion& region) { return region.name == name; });
}

SpriteFrame Sprite::Resolve(const std::string& region) const
{
	std::lock_guard<std::mutex> lock(_mutex);

	const SpriteRegion* found = nullptr;
	if (!region.empty())
	{
		auto it = std::find_if(_regions.begin(), _regions.end(), [&](const SpriteRegion& r) { return r.name == region; });
		if (it != _regions.end())
			found = &*it;
	}
	if (!found && !_regions.empty())
		found = &_regions.front();

	return MakeFrame(_texture, found);
}

SpriteFrame Sprite::MakeFrame(const std::shared_ptr<Texture2D>& texture, const SpriteRegion* region)
{
	SpriteFrame frame;
	if (!texture || texture->IsEmpty())
		return frame;

	const float texW = static_cast<float>(texture->GetWidth());
	const float texH = static_cast<float>(texture->GetHeight());
	if (texW <= 0.0f || texH <= 0.0f)
		return frame;

	glm::vec4 rect(0.0f, 0.0f, texW, texH);
	if (region && region->rect.z > 0.0f && region->rect.w > 0.0f)
	{
		// 区域越界时裁到贴图范围内
		const float x0 = std::clamp(region->rect.x, 0.0f, texW);
		const float y0 = std::clamp(region->rect.y, 0.0f, texH);
		const float x1 = std::clamp(region->rect.x + region->rect.z, x0, texW);
		const float y1 = std::clamp(region->rect.y + region->rect.w, y0, texH);
		rect = glm::vec4(x0, y0, x1 - x0, y1 - y0);
	}

	frame.texture = texture;
	frame.size = glm::vec2(rect.z, rect.w);
	frame.uvRect = glm::vec4(rect.x / texW, rect.y / texH, (rect.x + rect.z) / texW, (rect.y + rect.w) / texH);
	if (region)
	{
		frame.pivot = region->pivot;
		// 边距不能超过区域本身
		frame.border = glm::max(region->border, glm::vec4(0.0f));
		frame.border.x = std::min(frame.border.x, rect.z);
		frame.border.z = std::min(frame.border.z, rect.z - frame.border.x);
		frame.border.y = std::min(frame.border.y, rect.w);
		frame.border.w = std::min(frame.border.w, rect.w - frame.border.y);
	}
	return frame;
}

// ============================== AnimatedImage ==============================

std::shared_ptr<AnimatedImage> AnimatedImage::LoadFromFile(const std::string& path, const TextureConfig& config, std::string* error)
{
	std::vector<uint8_t> bytes;
	if (!ReadFileBytes(path, bytes))
	{
		SetError(error, std::format("读取动图文件失败：{}", path));
		return nullptr;
	}

	const bool isGif = bytes.size() >= 6 && std::memcmp(bytes.data(), "GIF", 3) == 0;
	if (isGif)
	{
		GifFrames gif;
		std::string reason;
		if (!DecodeGif(bytes, gif, reason))
		{
			SetError(error, std::format("解码 GIF 失败：{}（{}）", path, reason));
			return nullptr;
		}

		std::vector<const uint8_t*> frames(gif.frames.size());
		std::vector<float> durations(gif.frames.size());
		for (size_t i = 0; i < gif.frames.size(); ++i)
		{
			frames[i] = gif.frames[i].data();
			const float delay = gif.durationsMs[i];
			durations[i] = delay < kMinFrameDurationMs ? kFallbackFrameDurationMs : delay;
		}
		return FromFrames(frames, gif.width, gif.height, durations, config, error);
	}

	// 不是 GIF：按单帧静态图处理，方便把普通贴图也当动图资源用
	int width = 0, height = 0, comp = 0;
	stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &comp, 4);
	if (!pixels || width <= 0 || height <= 0)
	{
		if (pixels) stbi_image_free(pixels);
		SetError(error, std::format("解码图片失败：{}", path));
		return nullptr;
	}
	std::vector<const uint8_t*> frames{ pixels };
	auto image = FromFrames(frames, static_cast<uint32_t>(width), static_cast<uint32_t>(height), { kFallbackFrameDurationMs }, config, error);
	stbi_image_free(pixels);
	return image;
}

std::shared_ptr<AnimatedImage> AnimatedImage::FromFrames(const std::vector<const uint8_t*>& rgbaFrames, uint32_t width, uint32_t height,
	const std::vector<float>& durationsMs, const TextureConfig& config, std::string* error)
{
	if (rgbaFrames.empty() || width == 0 || height == 0)
	{
		SetError(error, "动图没有帧");
		return nullptr;
	}

	const uint32_t cellW = width + kFramePadding * 2;
	const uint32_t cellH = height + kFramePadding * 2;
	if (cellW > kMaxAtlasDimension || cellH > kMaxAtlasDimension)
	{
		SetError(error, std::format("动图单帧尺寸 {}x{} 超过图集上限 {}", width, height, kMaxAtlasDimension));
		return nullptr;
	}

	const uint32_t maxCols = std::max(1u, kMaxAtlasDimension / cellW);
	const uint32_t maxRows = std::max(1u, kMaxAtlasDimension / cellH);
	const uint32_t framesPerPage = maxCols * maxRows;
	const uint32_t totalFrames = static_cast<uint32_t>(rgbaFrames.size());

	const TextureConfig atlasConfig = MakeImageTextureConfig(config);
	const vk::Format format = atlasConfig.gammaCorrection ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;

	auto image = std::make_shared<AnimatedImage>();
	image->_frameSize = glm::uvec2(width, height);
	image->_frames.resize(totalFrames);

	std::vector<uint8_t> pagePixels;
	uint32_t frameIndex = 0;
	uint32_t pageIndex = 0;
	while (frameIndex < totalFrames)
	{
		const uint32_t count = std::min(framesPerPage, totalFrames - frameIndex);
		const uint32_t cols = std::min(maxCols, count);
		const uint32_t rows = (count + cols - 1) / cols;
		const uint32_t pageW = cols * cellW;
		const uint32_t pageH = rows * cellH;

		pagePixels.assign(static_cast<size_t>(pageW) * pageH * 4u, 0u);
		auto pixelAt = [&](uint32_t x, uint32_t y) -> uint8_t* { return pagePixels.data() + (static_cast<size_t>(y) * pageW + x) * 4u; };

		for (uint32_t i = 0; i < count; ++i)
		{
			const uint8_t* src = rgbaFrames[frameIndex + i];
			const uint32_t cellX = (i % cols) * cellW;
			const uint32_t cellY = (i / cols) * cellH;
			const uint32_t dstX = cellX + kFramePadding;
			const uint32_t dstY = cellY + kFramePadding;

			// 内容
			for (uint32_t y = 0; y < height; ++y)
				std::memcpy(pixelAt(dstX, dstY + y), src + static_cast<size_t>(y) * width * 4u, static_cast<size_t>(width) * 4u);

			// 挤出边：上下两行、左右两列（含四个角）
			for (uint32_t p = 1; p <= kFramePadding; ++p)
			{
				std::memcpy(pixelAt(dstX, dstY - p), pixelAt(dstX, dstY), static_cast<size_t>(width) * 4u);
				std::memcpy(pixelAt(dstX, dstY + height - 1 + p), pixelAt(dstX, dstY + height - 1), static_cast<size_t>(width) * 4u);
			}
			for (uint32_t y = dstY - kFramePadding; y < dstY + height + kFramePadding; ++y)
			{
				for (uint32_t p = 1; p <= kFramePadding; ++p)
				{
					std::memcpy(pixelAt(dstX - p, y), pixelAt(dstX, y), 4u);
					std::memcpy(pixelAt(dstX + width - 1 + p, y), pixelAt(dstX + width - 1, y), 4u);
				}
			}

			Frame& frame = image->_frames[frameIndex + i];
			frame.page = pageIndex;
			frame.uvRect = glm::vec4(
				static_cast<float>(dstX) / pageW,
				static_cast<float>(dstY) / pageH,
				static_cast<float>(dstX + width) / pageW,
				static_cast<float>(dstY + height) / pageH);
			const size_t durationIndex = std::min<size_t>(frameIndex + i, durationsMs.empty() ? 0 : durationsMs.size() - 1);
			frame.durationMs = durationsMs.empty() ? kFallbackFrameDurationMs : std::max(1.0f, durationsMs[durationIndex]);
		}

		auto page = std::make_shared<Texture2D>(pageW, pageH, format, atlasConfig, 1u);
		if (!page || page->IsEmpty())
		{
			SetError(error, "创建动图图集纹理失败");
			return nullptr;
		}
		page->UpdateTextureData(pagePixels.data());
		image->_pages.push_back(std::move(page));

		frameIndex += count;
		++pageIndex;
	}

	image->_totalDuration = 0.0f;
	for (const auto& frame : image->_frames)
		image->_totalDuration += frame.durationMs;

	return image;
}

uint32_t AnimatedImage::FrameIndexAt(float timeMs, int loopCount) const
{
	if (_frames.size() <= 1 || _totalDuration <= 0.0f)
		return 0;

	if (timeMs < 0.0f)
		timeMs = 0.0f;

	if (loopCount > 0 && timeMs >= _totalDuration * static_cast<float>(loopCount))
		return static_cast<uint32_t>(_frames.size() - 1);

	float t = std::fmod(timeMs, _totalDuration);
	for (uint32_t i = 0; i < _frames.size(); ++i)
	{
		if (t < _frames[i].durationMs)
			return i;
		t -= _frames[i].durationMs;
	}
	return static_cast<uint32_t>(_frames.size() - 1);
}

bool AnimatedImage::IsFinished(float timeMs, int loopCount) const
{
	return loopCount > 0 && _totalDuration > 0.0f && timeMs >= _totalDuration * static_cast<float>(loopCount);
}

SpriteFrame AnimatedImage::GetFrameSprite(uint32_t index, const glm::vec2& pivot) const
{
	SpriteFrame result;
	if (_frames.empty())
		return result;

	const Frame& frame = _frames[std::min<size_t>(index, _frames.size() - 1)];
	if (frame.page >= _pages.size())
		return result;

	result.texture = _pages[frame.page];
	result.uvRect = frame.uvRect;
	result.size = glm::vec2(_frameSize);
	result.pivot = pivot;
	return result;
}

SpriteFrame AnimatedImage::Evaluate(float timeMs, int loopCount, const glm::vec2& pivot) const
{
	return GetFrameSprite(FrameIndexAt(timeMs, loopCount), pivot);
}
