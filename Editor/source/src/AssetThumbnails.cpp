#include "AssetThumbnails.h"

#include "Sprite2DEditor.h"
#include "IconManager.h"
#include "Project/ProjectManager.h"
#include "Project/AssetDescription.h"
#include "Project/AssetLoader.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/RenderPass/MaterialPreviewPass.h"
#include "VulkanRenderEngine/Base/Material.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	constexpr uint32_t kThumbnailSize = 128;							// 最长边
	constexpr size_t kAnimatedBudgetBytes = 4u * 1024u * 1024u;		// 一个动图缩略图的像素总量上限
	constexpr uint32_t kAnimatedSizes[] = { 128, 96, 64, 48, 32 };		// 动图帧多时依次尝试的每帧最长边
	constexpr size_t kMaxEntries = 3000;
	constexpr size_t kMaxBytes = 256u * 1024u * 1024u;
	constexpr uint64_t kCancelQueuedAfterFrames = 30;					// 这么多帧没画、还在排队的任务直接取消
	constexpr auto kRecheckInterval = std::chrono::seconds(2);
	constexpr uint32_t kMaterialThumbnailSize = 128;					// 材质球输出尺寸
	constexpr uint64_t kRuntimeEvictAfterFrames = 600;				// 运行时材质球这么多帧没画就释放（重画很便宜）
	constexpr size_t kRuntimeMaxEntries = 512;						// 运行时材质球最多缓存这么多个（超出按最近使用淘汰）
	constexpr uint32_t kRuntimeMaxSubmitsPerFrame = 4;				// 每帧最多提交这么多个新的运行时材质球任务
	constexpr uint32_t kMaterialSourceMaxSide = 256;

	using Clock = std::chrono::steady_clock;

	struct ThumbnailData
	{
		std::shared_ptr<Render2D::AnimatedImage> image;		// 静态图就是只有一帧的动图
		std::string sourcePath;
		fs::file_time_type sourceTime{};

		// 材质：MaterialPreviewPass 渲染出来的材质球（image 为空）
		std::shared_ptr<Texture2D> rendered;
		// 材质依赖的文件（贴图的元文件和原图）：任一改了就重新渲染
		struct Dependency
		{
			std::string path;
			fs::file_time_type time{};
		};
		std::vector<Dependency> dependencies;

		bool HasContent() const { return image || rendered; }
	};
	using ThumbnailRequest = std::shared_ptr<AssetLoadTask<std::shared_ptr<ThumbnailData>>>;

	fs::file_time_type FileTime(const std::string& path)
	{
		std::error_code ec;
		const auto time = fs::last_write_time(fs::path(path), ec);
		return ec ? fs::file_time_type{} : time;
	}

	// ------------------------------ 后台任务（在加载器线程上执行） ------------------------------

	struct SourceInfo
	{
		AssetPath path;
		TextureConfig config;
	};

	bool ReadSource(AssetType type, const std::string& metaPath, SourceInfo& out)
	{
		switch (type)
		{
		case AssetType::Texture:
		{
			TextureAssetDescription desc;
			if (!desc.LoadFromFile(metaPath))
				return false;
			out.path = desc.GetPath();
			out.config = desc.GetConfig();
			break;
		}
		case AssetType::Sprite:
		{
			SpriteAssetDescription desc;
			if (!desc.LoadFromFile(metaPath))
				return false;
			out.path = desc.GetPath();
			out.config = desc.GetConfig();
			break;
		}
		case AssetType::AnimatedImage:
		{
			AnimatedImageAssetDescription desc;
			if (!desc.LoadFromFile(metaPath))
				return false;
			out.path = desc.GetPath();
			out.config = desc.GetConfig();
			break;
		}
		default:
			return false;
		}
		return !out.path.empty();
	}

	glm::uvec2 FitSize(uint32_t width, uint32_t height, uint32_t maxSide)
	{
		const uint32_t longest = std::max(width, height);
		if (longest <= maxSide)
			return glm::uvec2(width, height);	// 不放大
		const double scale = static_cast<double>(maxSide) / static_cast<double>(longest);
		return glm::uvec2(
			std::max(1u, static_cast<uint32_t>(std::lround(width * scale))),
			std::max(1u, static_cast<uint32_t>(std::lround(height * scale))));
	}

	std::vector<uint8_t> Shrink(const uint8_t* src, uint32_t width, uint32_t height, const glm::uvec2& size)
	{
		std::vector<uint8_t> out(static_cast<size_t>(size.x) * size.y * 4u);
		Render2D::ResizeRGBA(src, width, height, out.data(), size.x, size.y);
		return out;
	}

	// 刚建好的小贴图还没有别人用：这里直接切成采样布局，编辑器画的时候就不用再单独提交布局转换
	void MakeSampleable(const Render2D::AnimatedImage& image)
	{
		auto cmd = VKCONTEXT->GetCommandBuffer();
		if (!cmd)
			return;
		cmd->Begin();
		for (const auto& page : image.GetPages())
		{
			if (page && !page->IsEmpty())
				page->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
		}
		cmd->End();
		cmd->SubmitNowAndWait();
	}

	std::shared_ptr<ThumbnailData> BuildThumbnail(AssetType type, const std::string& metaPath, const std::string& projectFolder)
	{
		SourceInfo source;
		if (!ReadSource(type, metaPath, source))
			return nullptr;

		auto data = std::make_shared<ThumbnailData>();
		data->sourcePath = (fs::path(projectFolder) / fs::path(source.path)).string();
		data->sourceTime = FileTime(data->sourcePath);

		// 只给 ImGui 画：始终 UNORM（不做 sRGB 转换，和原图看起来一样）；过滤方式跟资产，NEAREST 的放大显示时仍然清晰
		TextureConfig config;
		config.minFilter = source.config.magFilter;
		config.magFilter = source.config.magFilter;
		config.gammaCorrection = false;

		std::vector<std::vector<uint8_t>> frames;
		std::vector<float> durations;
		glm::uvec2 frameSize(0u);
		std::string error;

		bool decoded = false;
		if (type == AssetType::AnimatedImage)
		{
			// 逐帧解码、逐帧缩小：内存里只留缩小后的帧，整帧画布每帧复用
			Render2D::GifInfo info;
			uint32_t stride = 1;
			uint32_t index = 0;
			decoded = Render2D::DecodeGifFile(data->sourcePath, [&](const uint8_t* rgba, uint32_t width, uint32_t height, float delayMs)
				{
					if (index == 0)
					{
						// 第一次回调前 info 已经填好：按帧数挑每帧尺寸，总量不超过预算；最小一档也放不下就抽帧
						const size_t count = std::max<size_t>(1u, info.frameCount);
						size_t bytes = 0;
						for (uint32_t side : kAnimatedSizes)
						{
							frameSize = FitSize(width, height, side);
							bytes = count * frameSize.x * frameSize.y * 4u;
							if (bytes <= kAnimatedBudgetBytes)
								break;
						}
						if (bytes > kAnimatedBudgetBytes)
							stride = static_cast<uint32_t>((bytes + kAnimatedBudgetBytes - 1) / kAnimatedBudgetBytes);
					}

					if (index % stride == 0 || frames.empty())
					{
						frames.push_back(Shrink(rgba, width, height, frameSize));
						durations.push_back(delayMs);
					}
					else
					{
						durations.back() += delayMs;	// 被抽掉的帧，时长并到前一帧上，整体节奏不变
					}
					++index;
					return true;
				}, &info, &error);
		}

		if (!decoded)
		{
			// 静态图；动图资产的原文件不是 GIF 时也走这里（和加载动图资产时的处理一致）
			frames.clear();
			durations.clear();
			Render2D::ImageRGBA image;
			if (!Render2D::DecodeImageFile(data->sourcePath, image, &error))
			{
				std::cerr << std::format("[ Thumbnail ] 生成缩略图失败：{}\n", error);
				return nullptr;
			}
			frameSize = FitSize(image.width, image.height, kThumbnailSize);
			frames.push_back(Shrink(image.pixels.data(), image.width, image.height, frameSize));
			durations.push_back(100.0f);
		}

		std::vector<const uint8_t*> framePointers;
		framePointers.reserve(frames.size());
		for (const auto& frame : frames)
			framePointers.push_back(frame.data());

		// 不建采样器：缩略图只交给 ImGui 画
		data->image = Render2D::AnimatedImage::FromFrames(framePointers, frameSize.x, frameSize.y, durations, config, &error, false);
		if (!data->image)
		{
			std::cerr << std::format("[ Thumbnail ] 生成缩略图失败（{}）：{}\n", data->sourcePath, error);
			return nullptr;
		}
		MakeSampleable(*data->image);
		return data;
	}

	// ------------------------------ 材质球（在加载器线程上执行） ------------------------------
	// 依赖贴图不走 LoadAsset / 资产表：从原图独立解码、缩小，建一张带采样器的小贴图，只在这个任务里用，
	// 渲染完跟着任务一起释放。扫过的材质不会把贴图留在内存里，内存里留下的只有渲染结果

	std::shared_ptr<Texture2D> LoadMaterialSourceTexture(const AssetMeta& textureMeta, const std::string& projectFolder,
		std::vector<ThumbnailData::Dependency>& dependencies)
	{
		const std::string textureMetaPath = (fs::path(projectFolder) / fs::path(textureMeta.path)).string();
		dependencies.push_back({ textureMetaPath, FileTime(textureMetaPath) });

		TextureAssetDescription desc;
		if (!desc.LoadFromFile(textureMetaPath) || desc.GetPath().empty())
			return nullptr;
		const std::string sourcePath = (fs::path(projectFolder) / fs::path(desc.GetPath())).string();
		dependencies.push_back({ sourcePath, FileTime(sourcePath) });

		std::vector<uint8_t> pixels;
		glm::uvec2 size(0u);
		{
			Render2D::ImageRGBA image;
			std::string error;
			if (!Render2D::DecodeImageFile(sourcePath, image, &error))
			{
				std::cerr << std::format("[ Thumbnail ] 材质贴图解码失败（{}）：{}\n", sourcePath, error);
				return nullptr;
			}
			size = FitSize(image.width, image.height, kMaterialSourceMaxSide);
			pixels = Shrink(image.pixels.data(), image.width, image.height, size);
		}	// 原图像素在这里释放

		// 要给着色器采样：必须带采样器；sRGB / UNORM 跟资产配置走（和运行时加载贴图一致）
		TextureConfig config = desc.GetConfig();
		config.anisotropy = false;
		const vk::Format format = config.gammaCorrection ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
		auto texture = std::make_shared<Texture2D>(size.x, size.y, format, config, 1, true);
		if (texture->IsEmpty())
			return nullptr;
		texture->UpdateTextureData(pixels.data());
		texture->MakeSampledImmutable();
		return texture;
	}

	std::shared_ptr<ThumbnailData> BuildMaterialThumbnail(const std::string& metaPath, const std::string& projectFolder,
		const std::shared_ptr<AssetDatabase>& database)
	{
		MaterialAssetDescription desc;
		if (!desc.LoadFromFile(metaPath))
			return nullptr;

		auto data = std::make_shared<ThumbnailData>();
		MaterialPreviewInput input;
		input.properties = desc.GetProperties();

		// 同一张贴图用在多个槽里只解码一次
		std::unordered_map<AssetGUID, std::shared_ptr<Texture2D>> loaded;
		for (const auto& [type, guid] : desc.GetTexture())
		{
			if (guid.empty())
				continue;
			auto cached = loaded.find(guid);
			if (cached != loaded.end())
			{
				if (cached->second)
					input.textures[type] = cached->second;
				continue;
			}

			// 找不到 / 不是贴图的槽当作没有贴图，和运行时材质的表现一致
			std::shared_ptr<Texture2D> texture;
			auto textureMeta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			if (textureMeta && textureMeta->type == AssetType::Texture)
				texture = LoadMaterialSourceTexture(*textureMeta, projectFolder, data->dependencies);
			loaded[guid] = texture;
			if (texture)
				input.textures[type] = texture;
		}

		// 只给 ImGui 画：不建采样器
		TextureConfig outputConfig;
		outputConfig.minFilter = vk::Filter::eLinear;
		outputConfig.magFilter = vk::Filter::eLinear;
		data->rendered = std::make_shared<Texture2D>(kMaterialThumbnailSize, kMaterialThumbnailSize, vk::Format::eR8G8B8A8Unorm, outputConfig, 1, false);
		if (data->rendered->IsEmpty() || !MaterialPreviewPass::Instance().Render(input, MaterialPreviewView{}, data->rendered))
		{
			std::cerr << std::format("[ Thumbnail ] 材质球渲染失败：{}\n", metaPath);
			return nullptr;
		}
		return data;
	}

	// 运行时材质（模型组件的材质槽）：输入在主线程上从运行时材质拍下来（属性 + 已加载的贴图），任务里只渲染。
	// 只用只读的贴图（资产贴图、模型内嵌贴图加载完都是 MakeSampledImmutable 的）：后台线程采样它们不用转换布局，
	// 不会和帧流水线抢；万一有不是只读的贴图就不放进预览（这个槽在材质球上当作没有贴图）
	MaterialPreviewInput CaptureRuntimeMaterial(const Material& material)
	{
		MaterialPreviewInput input;
		input.properties = material.GetProperties();
		for (const auto& [type, texture] : material.GetTextures())
		{
			if (texture && !texture->IsEmpty() && texture->GetSampler() && texture->IsSampledImmutable())
				input.textures[type] = texture;
		}
		return input;
	}

	std::shared_ptr<ThumbnailData> BuildRuntimeMaterialThumbnail(const MaterialPreviewInput& input)
	{
		auto data = std::make_shared<ThumbnailData>();
		TextureConfig outputConfig;
		outputConfig.minFilter = vk::Filter::eLinear;
		outputConfig.magFilter = vk::Filter::eLinear;
		data->rendered = std::make_shared<Texture2D>(kMaterialThumbnailSize, kMaterialThumbnailSize, vk::Format::eR8G8B8A8Unorm, outputConfig, 1, false);
		if (data->rendered->IsEmpty() || !MaterialPreviewPass::Instance().Render(input, MaterialPreviewView{}, data->rendered))
			return nullptr;
		return data;
	}

	// 决定运行时材质球要不要重画：材质版本（属性 / 贴图槽）+ 每张贴图本身（换图、重新导入）
	uint64_t RuntimeMaterialKey(const Material& material)
	{
		uint64_t hash = 1469598103934665603ull;
		auto mix = [&hash](uint64_t value)
			{
				hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
			};
		mix(material.GetVersion());
		for (const auto& [type, texture] : material.GetTextures())
		{
			mix(static_cast<uint64_t>(type));
			mix(reinterpret_cast<uintptr_t>(texture.get()));
			mix(texture ? texture->GetDescBindEntryVersion() : 0u);
		}
		return hash;
	}

	// ------------------------------ 缓存（只在编辑器主线程） ------------------------------

	struct Entry
	{
		AssetType type = AssetType::Unknown;
		std::string metaPath;
		fs::file_time_type metaTime{};
		ThumbnailRequest request;
		std::shared_ptr<ThumbnailData> data;
		bool failed = false;
		bool dirty = false;
		uint64_t lastUsedFrame = 0;
		Clock::time_point lastCheck{};
		size_t bytes = 0;		// data 占的显存（按图集页实际尺寸），计入 State::totalBytes
	};

	struct RuntimeEntry
	{
		std::weak_ptr<Material> material;
		ThumbnailRequest request;
		uint64_t requestKey = 0;		// 正在跑的任务对应的材质状态
		std::shared_ptr<ThumbnailData> data;
		uint64_t dataKey = 0;			// 正在显示的结果对应的材质状态
		uint64_t failedKey = 0;			// 这个状态渲染失败过：材质再变之前不重试
		bool failed = false;
		uint64_t lastUsedFrame = 0;
	};

	struct State
	{
		bool enabled = true;
		std::unordered_map<AssetGUID, Entry> entries;
		std::unordered_map<const Material*, RuntimeEntry> runtimeEntries;	// 按运行时材质对象缓存，不算进 totalBytes
		uint64_t frame = 0;
		uint64_t generation = 0;	// 每次清空加一：旧的后台任务和新请求的 key 不会撞上
		uint64_t serial = 0;
		std::string projectFolder;
		size_t totalBytes = 0;		// 所有 entry.bytes 之和

		uint32_t runtimeSubmitsThisFrame = 0;
		uint64_t runtimeRenderCount = 0;	// 累计提交过多少次运行时材质球渲染（排查用：选中一个模型后应该只涨一次）
		bool runtimeWarned = false;
	};

	State& GetState()
	{
		static State state;
		return state;
	}

	void ReleaseTextures(const std::shared_ptr<ThumbnailData>& data)
	{
		if (!data)
			return;
		if (data->image)
		{
			for (const auto& page : data->image->GetPages())
				Sprite2DEditor::TextureCache().Remove(page);
		}
		if (data->rendered)
			Sprite2DEditor::TextureCache().Remove(data->rendered);
	}

	size_t ImageBytes(const ThumbnailData& data)
	{
		size_t bytes = 0;
		if (data.image)
		{
			for (const auto& page : data.image->GetPages())
			{
				if (page)
					bytes += static_cast<size_t>(page->GetWidth()) * page->GetHeight() * 4u;
			}
		}
		if (data.rendered)
			bytes += static_cast<size_t>(data.rendered->GetWidth()) * data.rendered->GetHeight() * 4u;
		return bytes;
	}

	// 换上新结果 / 释放结果时同步维护字节数
	void SetData(State& state, Entry& entry, std::shared_ptr<ThumbnailData> data)
	{
		ReleaseTextures(entry.data);
		state.totalBytes -= std::min(state.totalBytes, entry.bytes);
		entry.data = std::move(data);
		entry.bytes = entry.data ? ImageBytes(*entry.data) : 0;
		state.totalBytes += entry.bytes;
	}

	void ReleaseEntry(State& state, Entry& entry)
	{
		// 还在排队的取消；已经在执行的只能让它做完，结果没人要，最后一个引用放掉时自己释放
		if (entry.request)
		{
			entry.request->Cancel();
			entry.request.reset();
		}
		SetData(state, entry, nullptr);
	}

	void ReleaseRuntimeEntry(RuntimeEntry& entry)
	{
		if (entry.request)
		{
			entry.request->Cancel();
			entry.request.reset();
		}
		ReleaseTextures(entry.data);
		entry.data.reset();
	}

	void ClearEntries(State& state)
	{
		for (auto& [guid, entry] : state.entries)
			ReleaseEntry(state, entry);
		state.entries.clear();
		for (auto& [material, entry] : state.runtimeEntries)
			ReleaseRuntimeEntry(entry);
		state.runtimeEntries.clear();
		state.totalBytes = 0;
		++state.generation;
	}

	void Submit(State& state, ProjectManager* projectManager, const AssetGUID& guid, Entry& entry)
	{
		entry.metaTime = FileTime(entry.metaPath);
		entry.lastCheck = Clock::now();
		const std::string key = std::format("thumb:{}:{}:{}", state.generation, guid, ++state.serial);
		entry.request = projectManager->GetAssetLoader().Request<std::shared_ptr<ThumbnailData>>(
			key, AssetLoadPriority::Low, AssetLoadLane::Thumbnail,
			[type = entry.type, metaPath = entry.metaPath, folder = state.projectFolder, database = projectManager->GetDatabase()]()
			{
				if (type == AssetType::Material)
					return BuildMaterialThumbnail(metaPath, folder, database);
				return BuildThumbnail(type, metaPath, folder);
			});
	}

	void Poll(State& state, Entry& entry)
	{
		if (!entry.request || !entry.request->IsFinished())
			return;
		if (entry.request->IsDone())
		{
			auto result = entry.request->GetResult();
			if (result && result->HasContent())
			{
				SetData(state, entry, std::move(result));
				entry.failed = false;
			}
			else
			{
				entry.failed = !entry.data;		// 重新生成失败时继续显示旧的
			}
		}
		// 被取消（例如切换工程时加载器取消了排队的任务）：下次画到时重新提交
		entry.request.reset();
	}

	bool SourceChanged(const Entry& entry)
	{
		if (FileTime(entry.metaPath) != entry.metaTime)
			return true;
		if (!entry.data)
			return false;
		if (!entry.data->sourcePath.empty() && FileTime(entry.data->sourcePath) != entry.data->sourceTime)
			return true;
		for (const auto& dependency : entry.data->dependencies)
		{
			if (FileTime(dependency.path) != dependency.time)
				return true;
		}
		return false;
	}
}

namespace AssetThumbnails
{
	bool IsSupported(AssetType type)
	{
		return type == AssetType::Texture || type == AssetType::Sprite || type == AssetType::AnimatedImage
			|| type == AssetType::Material;
	}

	void SetEnabled(bool enabled)
	{
		auto& state = GetState();
		if (state.enabled == enabled)
			return;
		state.enabled = enabled;
		if (!enabled)
			ClearEntries(state);
	}

	bool IsEnabled()
	{
		return GetState().enabled;
	}

	void BeginFrame(ProjectManager* projectManager)
	{
		auto& state = GetState();
		++state.frame;
		state.runtimeSubmitsThisFrame = 0;

		// 切换 / 关闭工程：缩略图全部作废
		const std::string folder = (projectManager && projectManager->IsProjectOpen()) ? projectManager->GetProjectFolderFullPath() : std::string();
		if (folder != state.projectFolder)
		{
			ClearEntries(state);
			state.projectFolder = folder;
		}
		if (!state.enabled)
			return;

		// 运行时材质球：材质对象没了、或者很久没画的释放；很久没画还在排队的取消
		for (auto it = state.runtimeEntries.begin(); it != state.runtimeEntries.end();)
		{
			RuntimeEntry& entry = it->second;
			const uint64_t idle = state.frame - entry.lastUsedFrame;
			if (entry.request && idle > kCancelQueuedAfterFrames
				&& entry.request->GetState() == AssetLoadJob::State::Queued && entry.request->Cancel())
				entry.request.reset();
			if (entry.material.expired() || (idle > kRuntimeEvictAfterFrames && !entry.request))
			{
				ReleaseRuntimeEntry(entry);
				it = state.runtimeEntries.erase(it);
				continue;
			}
			++it;
		}

		// 个数超限：按最近使用淘汰（这一帧和上一帧画过的不动）
		if (state.runtimeEntries.size() > kRuntimeMaxEntries)
		{
			std::vector<std::pair<uint64_t, const Material*>> candidates;
			for (const auto& [material, entry] : state.runtimeEntries)
			{
				if (entry.lastUsedFrame + 1 < state.frame && !entry.request)
					candidates.emplace_back(entry.lastUsedFrame, material);
			}
			std::sort(candidates.begin(), candidates.end());
			for (const auto& [frame, material] : candidates)
			{
				if (state.runtimeEntries.size() <= kRuntimeMaxEntries)
					break;
				auto it = state.runtimeEntries.find(material);
				if (it == state.runtimeEntries.end())
					continue;
				ReleaseRuntimeEntry(it->second);
				state.runtimeEntries.erase(it);
			}
		}

		if (state.entries.empty())
			return;

		// 很久没画、还在排队的（例如快速滚动经过的格子）：取消，免得占着缩略图通道
		for (auto it = state.entries.begin(); it != state.entries.end();)
		{
			Entry& entry = it->second;
			if (entry.request && state.frame - entry.lastUsedFrame > kCancelQueuedAfterFrames
				&& entry.request->GetState() == AssetLoadJob::State::Queued && entry.request->Cancel())
			{
				entry.request.reset();
				if (!entry.data)
				{
					it = state.entries.erase(it);
					continue;
				}
			}
			++it;
		}

		// 按最近使用淘汰：个数或字节数任一超限（这一帧和上一帧画过的不动）
		auto overLimit = [&state]() { return state.entries.size() > kMaxEntries || state.totalBytes > kMaxBytes; };
		if (overLimit())
		{
			std::vector<std::pair<uint64_t, AssetGUID>> candidates;
			for (const auto& [guid, entry] : state.entries)
			{
				if (entry.lastUsedFrame + 1 < state.frame)
					candidates.emplace_back(entry.lastUsedFrame, guid);
			}
			std::sort(candidates.begin(), candidates.end());
			for (const auto& [frame, guid] : candidates)
			{
				if (!overLimit())
					break;
				auto it = state.entries.find(guid);
				if (it == state.entries.end())
					continue;
				ReleaseEntry(state, it->second);
				state.entries.erase(it);
			}
		}
	}

	Render2D::SpriteFrame Get(ProjectManager* projectManager, const AssetMeta& meta, float timeMs)
	{
		auto& state = GetState();
		if (!state.enabled || !projectManager || !projectManager->IsProjectOpen() || !IsSupported(meta.type) || meta.guid.empty())
			return {};
		if (state.projectFolder.empty())
			return {};	// 这一帧刚打开工程，等 BeginFrame 记下工程目录

		const std::string metaPath = projectManager->GetProjectAssetFullPath(meta.path);
		auto [it, inserted] = state.entries.try_emplace(meta.guid);
		Entry& entry = it->second;
		if (!inserted && (entry.type != meta.type || entry.metaPath != metaPath))
		{
			// 资产被重命名 / 换了类型：当成新的
			ReleaseEntry(state, entry);
			entry = Entry{};
			inserted = true;
		}
		if (inserted)
		{
			entry.type = meta.type;
			entry.metaPath = metaPath;
		}
		entry.lastUsedFrame = state.frame;

		Poll(state, entry);
		if (!entry.request)
		{
			const auto now = Clock::now();
			if ((entry.data || entry.failed) && !entry.dirty && now - entry.lastCheck >= kRecheckInterval)
			{
				entry.lastCheck = now;
				entry.dirty = SourceChanged(entry);
			}
			if ((!entry.data && !entry.failed) || entry.dirty)
			{
				entry.dirty = false;
				entry.failed = false;
				Submit(state, projectManager, meta.guid, entry);
			}
		}

		if (!entry.data)
			return {};
		if (entry.data->rendered)
			return Render2D::Sprite::MakeFrame(entry.data->rendered, nullptr);
		if (!entry.data->image)
			return {};
		return entry.data->image->Evaluate(timeMs, 0);
	}

	void DrawThumbnailBox(ProjectManager* projectManager, const AssetGUID& guid, float side)
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + side, p0.y + side);
		ImGui::Dummy(ImVec2(side, side));
		if (!ImGui::IsRectVisible(p0, p1))
			return;	// 不在视野里：只占位，不请求

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 255), 3.0f);

		auto database = (projectManager && !guid.empty()) ? projectManager->GetDatabase() : nullptr;
		auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;

		Render2D::SpriteFrame frame;
		if (meta && IsSupported(meta->type))
			frame = Get(projectManager, *meta, Sprite2DEditor::EditorTimeMs());

		const float inset = std::max(2.0f, side * 0.06f);
		if (frame.IsValid())
		{
			Sprite2DEditor::DrawFrameInRect(drawList, frame, ImVec2(p0.x + inset, p0.y + inset), ImVec2(p1.x - inset, p1.y - inset));
		}
		else if (meta)
		{
			if (ImTextureID icon = IconManager::Get()->GetIconByAssetType(meta->type); icon != 0)
				drawList->AddImage(icon, ImVec2(p0.x + side * 0.15f, p0.y + side * 0.15f), ImVec2(p1.x - side * 0.15f, p1.y - side * 0.15f));
		}
		drawList->AddRect(p0, p1, IM_COL32(95, 95, 95, 255), 3.0f);

		if (meta && ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			if (frame.IsValid())
				Sprite2DEditor::DrawFramePreview(frame, ImVec2(static_cast<float>(kThumbnailSize), static_cast<float>(kThumbnailSize)));
			ImGui::TextUnformatted(meta->name.c_str());
			ImGui::TextDisabled("%s", meta->path.c_str());
			ImGui::EndTooltip();
		}
	}

	Render2D::SpriteFrame GetMaterial(ProjectManager* projectManager, const std::shared_ptr<Material>& material)
	{
		auto& state = GetState();
		if (!state.enabled || !projectManager || !material)
			return {};

		// 有资产的材质：和资产浏览器用同一份缩略图（按 GUID 缓存、按磁盘上的元文件渲染），
		// 在哪边先看过，另一边就直接有，不会各渲染一遍；面板里改完松手会存盘并 Invalidate，跟着重画。
		// 只有实例材质（模型内嵌 / 覆盖，没有资产）才按运行时材质对象单独渲染
		if (const AssetGUID& guid = material->GetAssetGUID(); !guid.empty())
		{
			auto database = projectManager->GetDatabase();
			auto meta = database ? database->GetAssetMetaByGuid(guid) : nullptr;
			if (meta && meta->type == AssetType::Material)
				return Get(projectManager, *meta, 0.0f);
		}

		auto [it, inserted] = state.runtimeEntries.try_emplace(material.get());
		RuntimeEntry& entry = it->second;
		if (!inserted && entry.material.lock() != material)
		{
			// 旧材质已经释放、同一个地址上又建了新的：当成新的
			ReleaseRuntimeEntry(entry);
			entry = RuntimeEntry{};
			inserted = true;
		}
		if (inserted)
			entry.material = material;
		entry.lastUsedFrame = state.frame;

		// 收结果
		if (entry.request && entry.request->IsFinished())
		{
			auto result = entry.request->IsDone() ? entry.request->GetResult() : nullptr;
			if (result && result->rendered)
			{
				ReleaseTextures(entry.data);
				entry.data = std::move(result);
				entry.dataKey = entry.requestKey;
				entry.failed = false;
			}
			else if (entry.request->IsDone())
			{
				entry.failed = true;
				entry.failedKey = entry.requestKey;
			}
			entry.request.reset();		// 被取消的下次画到时重新提交
		}

		// 材质变了就重画；同一个状态失败过就不重试。重画期间继续显示旧的
		const uint64_t key = RuntimeMaterialKey(*material);
		const bool upToDate = entry.data && entry.dataKey == key;
		const bool knownBad = entry.failed && entry.failedKey == key;
		if (!entry.request && !upToDate && !knownBad && state.runtimeSubmitsThisFrame < kRuntimeMaxSubmitsPerFrame)
		{
			++state.runtimeSubmitsThisFrame;
			const std::string requestName = std::format("thumb-rt:{}:{}:{}", state.generation,
				reinterpret_cast<uintptr_t>(material.get()), ++state.serial);
			entry.requestKey = key;
			entry.request = projectManager->GetAssetLoader().Request<std::shared_ptr<ThumbnailData>>(
				requestName, AssetLoadPriority::Low, AssetLoadLane::Thumbnail,
				[input = CaptureRuntimeMaterial(*material)]()
				{
					return BuildRuntimeMaterialThumbnail(input);
				});

			// 同一个材质球短时间里一直在重画，说明材质版本 / 贴图版本每帧都在变：打一次日志方便排查
			if (++state.runtimeRenderCount > 2000 && !state.runtimeWarned)
			{
				state.runtimeWarned = true;
				std::cerr << std::format("[ Thumbnail ] 运行时材质球已经提交了 {} 次渲染（当前缓存 {} 个），"
					"如果没有在改材质，说明有材质每帧都在变\n", state.runtimeRenderCount, state.runtimeEntries.size());
			}
		}

		if (!entry.data || !entry.data->rendered)
			return {};
		return Render2D::Sprite::MakeFrame(entry.data->rendered, nullptr);
	}

	bool DrawMaterialBox(ProjectManager* projectManager, const std::shared_ptr<Material>& material, float side, const char* label)
	{
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1(p0.x + side, p0.y + side);
		ImGui::InvisibleButton("##material_box", ImVec2(side, side));
		const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		if (!ImGui::IsRectVisible(p0, p1))
			return doubleClicked;	// 不在视野里：只占位，不请求

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 255), 3.0f);

		const Render2D::SpriteFrame frame = GetMaterial(projectManager, material);
		const float inset = side >= 32.0f ? std::max(2.0f, side * 0.06f) : 1.0f;
		if (frame.IsValid())
		{
			Sprite2DEditor::DrawFrameInRect(drawList, frame, ImVec2(p0.x + inset, p0.y + inset), ImVec2(p1.x - inset, p1.y - inset));
		}
		else if (material)
		{
			if (ImTextureID icon = IconManager::Get()->GetIconByAssetType(AssetType::Material); icon != 0)
				drawList->AddImage(icon, ImVec2(p0.x + side * 0.15f, p0.y + side * 0.15f), ImVec2(p1.x - side * 0.15f, p1.y - side * 0.15f));
		}
		drawList->AddRect(p0, p1, IM_COL32(95, 95, 95, 255), side >= 32.0f ? 3.0f : 2.0f);

		if (material && ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			if (frame.IsValid())
				Sprite2DEditor::DrawFramePreview(frame, ImVec2(static_cast<float>(kThumbnailSize), static_cast<float>(kThumbnailSize)));
			if (label && label[0] != '\0')
				ImGui::TextUnformatted(label);
			ImGui::EndTooltip();
		}
		return doubleClicked;
	}

	void Invalidate(const AssetGUID& guid)
	{
		auto& state = GetState();
		auto it = state.entries.find(guid);
		if (it != state.entries.end())
			it->second.dirty = true;
	}

	void Clear()
	{
		ClearEntries(GetState());
	}

	void Shutdown()
	{
		auto& state = GetState();
		for (auto& [guid, entry] : state.entries)
		{
			if (entry.request && !entry.request->Cancel())
				entry.request->Wait();	// 正在执行的等它做完，免得关闭期间后台还在建贴图
			entry.request.reset();
		}
		for (auto& [material, entry] : state.runtimeEntries)
		{
			if (entry.request && !entry.request->Cancel())
				entry.request->Wait();
			entry.request.reset();
		}
		ClearEntries(state);
	}
}
