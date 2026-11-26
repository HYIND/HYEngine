#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/StaticMesh.h"
#include "VulkanRenderEngine/Base/SkinnedMesh.h"
#include "VulkanRenderEngine/Base/Material.h"
#include "SegmentBufferManager.h"
#include "GeneralSegmentBuffer.h"
#include "CriticalSectionLock.h"


struct IndirectDrawMeta
{
	uint32_t indexCount = 0;
	uint32_t firstIndex = 0;
	int32_t vertexOffset = 0;
};

using BindlessIndex = uint32_t;
inline constexpr BindlessIndex BindlessIndexNull = std::numeric_limits<BindlessIndex>::max();

class BindlessTextureManager : public ITextureArrayProvider
{
	static std::shared_ptr<Texture2D> _placeholderTexture;

public:
	static std::shared_ptr<BindlessTextureManager> Instance();

	// 空贴图（IsEmpty，例如原图加载失败）不注册，返回 BindlessIndexNull
	BindlessIndex RegisterOrUpdateTexture(const std::shared_ptr<Texture2D>& tex);
	BindlessIndex RegisterOrUpdateTexture(const Texture2D* tex);
	BindlessIndex GetTextureIndex(const std::shared_ptr<Texture2D>& tex);
	BindlessIndex GetTextureIndex(const Texture2D* tex);
	void UnregisterTexture(const std::shared_ptr<Texture2D>& tex);
	void UnregisterTexture(const Texture2D* tex);

	std::vector<TextureDescBindEntry> GetTextureDescBindEntrys() const;

	// 贴图的描述符版本变了（换过 image / view / sampler）：Texture2D 内部调，只置标记、不拿锁
	static void MarkDirty() noexcept { s_dirty.store(true, std::memory_order_release); }
	// 渲染帧的一步（VKRenderer3D::SetupIndirectDrawData）：有标记时把版本对不上的槽位刷新成贴图当前的描述符。
	// 返回刷新了几个槽位
	uint32_t RefreshChangedTextures();

public:
	void ReleaseSlot(BindlessIndex index);

private:
	BindlessTextureManager();

	// 调用方持有写锁：换掉一个槽位的描述符，旧的放进 retired，由调用方出锁后交给 Retire
	// （在途帧的描述符集还引用着旧的 view / sampler，而 VKImageView / VKSampler 析构时会立刻销毁句柄）
	void ReplaceEntryLocked(BindlessIndex index, TextureDescBindEntry entry, std::vector<TextureDescBindEntry>& retired);
	static void RetireEntries(std::vector<TextureDescBindEntry>& retired);

	static inline std::atomic<bool> s_dirty{ false };

private:
	std::vector<TextureDescBindEntry> _entrys;
	std::unordered_map<const Texture2D*, uint32_t> _textureEntrys;

	std::queue<uint32_t> _idleSlot;

	mutable SharedLock _mutex;
};

class IndirectDrawManager
{
public:
	static std::shared_ptr<IndirectDrawManager> Instance();

	// Mesh相关
	void SetupMesh(Mesh& mesh);
	void RetireMesh(Mesh& mesh);
	bool GetIndirectDrawMeta(Mesh& mesh, IndirectDrawMeta& meta);
	std::shared_ptr<VertexBufferBlock> GetVertexBlock() const;
	std::shared_ptr<IndexBufferBlock> GetIndexBlock() const;

	// Material相关
	void SetupMaterial(Material& material);
	void RetireMaterial(Material& material);
	bool GetMaterialIndex(Material& material, uint64_t& index) const;
	bool GetMaterialIndex(Material& material, uint32_t& index) const;
	std::shared_ptr<StorageBlock> GetMaterialSSBO() const;
	std::shared_ptr<StorageBlock> GetShadowMaterialSSBO() const;

	// Skinned相关
	void SetupSkinned(SkinnedMesh& mesh);
	void RetireSkinned(SkinnedMesh& mesh);
	bool GetSkinnedIndex(SkinnedMesh& mesh, uint32_t& index);
	std::shared_ptr<VertexBufferBlock> GetSkinnedVertexBlock() const;

public:
	void SetupMesh_LockFree(Mesh& mesh);
	void SetupMaterial_LockFree(Material& material);
	void SetupSkinned_LockFree(SkinnedMesh& mesh);

	bool GetMaterialIndex_LockFree(Material& material, uint64_t& index) const;
	bool GetMaterialIndex_LockFree(Material& material, uint32_t& index) const;
	bool GetIndirectDrawMeta_LockFree(Mesh& mesh, IndirectDrawMeta& meta);
	bool GetSkinnedIndex_LockFree(SkinnedMesh& mesh, uint32_t& index);

public:
	enum class SharedLockType :uint32_t { Mesh = 0, Material, Skinned };

	void WithWriteLock(const std::vector<SharedLockType>& lockTypes, const std::function<void()>& call)
	{
		if (!call)
			return;

		using GuardType = LockGuard<SharedLock>;
		std::vector<std::unique_ptr<GuardType>> guards;
		for (auto type : lockTypes)
		{
			if (type == SharedLockType::Mesh)
				guards.push_back(std::move(std::make_unique<GuardType>(_meshMutex)));
			else if (type == SharedLockType::Material)
				guards.push_back(std::move(std::make_unique<GuardType>(_materialMutex)));
			else if (type == SharedLockType::Skinned)
				guards.push_back(std::move(std::make_unique<GuardType>(_skinnedMutex)));
		}

		call();
	}

	void WithSharedLock(const std::vector<SharedLockType>& lockTypes, const std::function<void()>& call)
	{
		if (!call)
			return;

		using GuardType = SharedLockGuard<SharedLock>;
		std::vector<std::unique_ptr<GuardType>> guards;
		for (auto type : lockTypes)
		{
			if (type == SharedLockType::Mesh)
				guards.push_back(std::move(std::make_unique<GuardType>(_meshMutex)));
			else if (type == SharedLockType::Material)
				guards.push_back(std::move(std::make_unique<GuardType>(_materialMutex)));
			else if (type == SharedLockType::Skinned)
				guards.push_back(std::move(std::make_unique<GuardType>(_skinnedMutex)));
		}

		call();
	}

public:
	void DeleteMesh(const std::string& uuid);
	void DeleteMaterial(const std::string& uuid);
	void DeleteSkinned(const std::string& uuid);

private:
	IndirectDrawManager();

private:
	SegmentBufferManager<VertexBufferSegmentBuffer, std::string> _VertexManager;
	SegmentBufferManager<IndexBufferSegmentBuffer, std::string> _IndexManager;
	mutable SharedLock _meshMutex;

	SegmentBufferManager<StorageSegmentBuffer, std::string> _MaterialManager;
	SegmentBufferManager<StorageSegmentBuffer, std::string> _ShadowMaterialManager;
	mutable SharedLock _materialMutex;

	SegmentBufferManager<VertexBufferSegmentBuffer, std::string> _SkinnedManager;
	mutable SharedLock _skinnedMutex;
};

