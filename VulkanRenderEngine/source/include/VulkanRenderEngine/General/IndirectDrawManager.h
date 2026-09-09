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

public:
	static std::shared_ptr<BindlessTextureManager> Instance();

	BindlessIndex RegisterOrUpdateTexture(const std::shared_ptr<Texture2D>& tex);
	BindlessIndex RegisterOrUpdateTexture(const Texture2D* tex);
	BindlessIndex GetTextureIndex(const std::shared_ptr<Texture2D>& tex);
	BindlessIndex GetTextureIndex(const Texture2D* tex);
	void UnregisterTexture(const std::shared_ptr<Texture2D>& tex);
	void UnregisterTexture(const Texture2D* tex);

	std::vector<TextureDescBindEntry> GetTextureDescBindEntrys() const;

	void ClearDirtyTexture();
private:
	BindlessTextureManager() {}

private:
	std::vector<TextureDescBindEntry> _entrys;

	std::vector<const Texture2D*> _texturePtrs;
	std::unordered_map<const Texture2D*, uint32_t> _textureEntrys;

	std::vector<const Texture2D*> dirtyTexture;

	mutable CriticalSectionLock _mutex;
};

class IndirectDrawManager
{
public:
	static std::shared_ptr<IndirectDrawManager> Instance();

	// Mesh相关
	void setupMesh(Mesh& mesh);
	void deleteMesh(Mesh& mesh);
	bool GetIndirectDrawMeta(Mesh& mesh, IndirectDrawMeta& meta);
	std::shared_ptr<VertexBufferBlock> GetVertexBlock();
	std::shared_ptr<IndexBufferBlock> GetIndexBlock();

	// Material相关
	void setupMaterial(Material& material);
	void deleteMaterial(Material& material);
	bool GetMaterialIndex(Material& material, uint64_t& index);
	bool GetMaterialIndex(Material& material, uint32_t& index);
	std::shared_ptr<StorageBlock> GetMaterialSSBO();

private:
	IndirectDrawManager();

private:
	SegmentBufferManager<VertexBufferSegmentBuffer, std::string> _VertexManager;
	SegmentBufferManager<IndexBufferSegmentBuffer, std::string> _IndexManager;
	CriticalSectionLock _meshMutex;

	SegmentBufferManager<StorageSegmentBuffer, std::string> _MaterialManager;
	CriticalSectionLock _materialMutex;

	SegmentBufferManager<StorageSegmentBuffer, std::string> _AnimatorManager;
	CriticalSectionLock _animatorMutex;
};

