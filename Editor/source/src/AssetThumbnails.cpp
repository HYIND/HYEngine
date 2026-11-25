#include "AssetThumbnails.h"

#include "Sprite2DEditor.h"
#include "IconManager.h"
#include "Project/ProjectManager.h"
#include "Project/AssetDescription.h"
#include "Project/AssetLoader.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/Texture2D.h"

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

	using Clock = std::chrono::steady_clock;

	struct ThumbnailData
	{
		std::shared_ptr<Render2D::AnimatedImage> image;		// 静态图就是只有一帧的动图
		std::string sourcePath;
		fs::file_time_type sourceTime{};
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

	struct State
	{
		bool enabled = true;
		std::unordered_map<AssetGUID, Entry> entries;
		uint64_t frame = 0;
		uint64_t generation = 0;	// 每次清空加一：旧的后台任务和新请求的 key 不会撞上
		uint64_t serial = 0;
		std::string projectFolder;
		size_t totalBytes = 0;		// 所有 entry.bytes 之和
	};

	State& GetState()
	{
		static State state;
		return state;
	}

	void ReleaseTextures(const std::shared_ptr<ThumbnailData>& data)
	{
		if (!data || !data->image)
			return;
		for (const auto& page : data->image->GetPages())
			Sprite2DEditor::TextureCache().Remove(page);
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

	void ClearEntries(State& state)
	{
		for (auto& [guid, entry] : state.entries)
			ReleaseEntry(state, entry);
		state.entries.clear();
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
			[type = entry.type, metaPath = entry.metaPath, folder = state.projectFolder]()
			{
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
			if (result && result->image)
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
		return entry.data && !entry.data->sourcePath.empty() && FileTime(entry.data->sourcePath) != entry.data->sourceTime;
	}
}

namespace AssetThumbnails
{
	bool IsSupported(AssetType type)
	{
		return type == AssetType::Texture || type == AssetType::Sprite || type == AssetType::AnimatedImage;
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

		// 切换 / 关闭工程：缩略图全部作废
		const std::string folder = (projectManager && projectManager->IsProjectOpen()) ? projectManager->GetProjectFolderFullPath() : std::string();
		if (folder != state.projectFolder)
		{
			ClearEntries(state);
			state.projectFolder = folder;
		}
		if (!state.enabled || state.entries.empty())
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

		if (!entry.data || !entry.data->image)
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
		ClearEntries(state);
	}
}
