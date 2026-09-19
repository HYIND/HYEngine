#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Mesh.h"
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

	BindlessIndex RegisterOrUpdateTexture(const std::shared_ptr<Texture2D>& tex);
	BindlessIndex RegisterOrUpdateTexture(const Texture2D* tex);
	BindlessIndex GetTextureIndex(const std::shared_ptr<Texture2D>& tex);
	BindlessIndex GetTextureIndex(const Texture2D* tex);
	void UnregisterTexture(const std::shared_ptr<Texture2D>& tex);
	void UnregisterTexture(const Texture2D* tex);

	std::vector<TextureDescBindEntry> GetTextureDescBindEntrys() const;

public:
	void DeleteTexture(const Texture2D* tex);

private:
	BindlessTextureManager() = default;

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
	std::shared_ptr<StorageBlock> GetMaterialSSBO();

public:
	void WithMeshSharedLock(const std::function<void()>& call);
	void WithMaterialSharedLock(const std::function<void()>& call);
	void WithMeshMaterialSharedLock(const std::function<void()>& call);

	bool GetMaterialIndex_LockFree(Material& material, uint64_t& index) const;
	bool GetMaterialIndex_LockFree(Material& material, uint32_t& index) const;
	bool GetIndirectDrawMeta_LockFree(Mesh& mesh, IndirectDrawMeta& meta);

public:
	void DeleteMesh(const std::string& uuid);
	void DeleteMaterial(const std::string& uuid);
private:
	IndirectDrawManager();

private:
	SegmentBufferManager<VertexBufferSegmentBuffer, std::string> _VertexManager;
	SegmentBufferManager<IndexBufferSegmentBuffer, std::string> _IndexManager;
	mutable SharedLock _meshMutex;

	SegmentBufferManager<StorageSegmentBuffer, std::string> _MaterialManager;
	mutable SharedLock _materialMutex;

	SegmentBufferManager<StorageSegmentBuffer, std::string> _AnimatorManager;
	mutable SharedLock _animatorMutex;
};

