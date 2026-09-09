#include "vkstdafx.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

std::shared_ptr<IndirectDrawManager> IndirectDrawManager::Instance()
{
	static std::shared_ptr<IndirectDrawManager> instance = std::shared_ptr<IndirectDrawManager>(new IndirectDrawManager());
	return instance;
}

void IndirectDrawManager::setupMesh(Mesh& mesh)
{
	auto guard = LockGuard(_meshMutex);
	auto uuid = mesh.GetUUID();
	auto version = mesh.GetVerticesIndicesVsrsion();

	{
		SegmentData segmentData;
		if (!_VertexManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
		{
			auto& vertices = mesh.GetVertices();
			_VertexManager.SetSegment(uuid, (void*)version, vertices.data(), vertices.size() * sizeof(Vertex));
		}
	}

	{
		SegmentData segmentData;
		if (!_IndexManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
		{
			auto& indices = mesh.GetIndices();
			_IndexManager.SetSegment(uuid, (void*)version, indices.data(), indices.size() * sizeof(unsigned int));
		}
	}
}

void IndirectDrawManager::deleteMesh(Mesh& mesh)
{
	auto guard = LockGuard(_meshMutex);
	_VertexManager.RemoveSegment(mesh.GetUUID());
	_IndexManager.RemoveSegment(mesh.GetUUID());
}

bool IndirectDrawManager::GetIndirectDrawMeta(Mesh& mesh, IndirectDrawMeta& meta)
{
	auto uuid = mesh.GetUUID();
	SegmentData vbodata, ebodata;
	if (!_VertexManager.FindSegment(uuid, vbodata) || !_IndexManager.FindSegment(uuid, ebodata))
		return false;

	meta.indexCount = ebodata.count / sizeof(unsigned int);
	meta.firstIndex = ebodata.first / sizeof(unsigned int);
	meta.vertexOffset = vbodata.first / sizeof(Vertex);

	return true;
}

std::shared_ptr<VertexBufferBlock> IndirectDrawManager::GetVertexBlock() {
	return _VertexManager.GetBuffer()->GetBlock();
}

std::shared_ptr<IndexBufferBlock> IndirectDrawManager::GetIndexBlock() {
	return _IndexManager.GetBuffer()->GetBlock();
}

void IndirectDrawManager::setupMaterial(Material& material)
{
	auto guard = LockGuard(_materialMutex);

	auto uuid = material.GetUUID();
	auto version = material.GetVersion();
	SegmentData segmentData;
	if (!_MaterialManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
	{
		auto data = material.GetMaterialCompData();
		_MaterialManager.SetSegment(uuid, (void*)version, &data, sizeof(data));
	}
}

void IndirectDrawManager::deleteMaterial(Material& material)
{
	auto guard = LockGuard(_materialMutex);
	_MaterialManager.RemoveSegment(material.GetUUID());
}

bool IndirectDrawManager::GetMaterialIndex(Material& material, uint64_t& index)
{
	auto guard = LockGuard(_materialMutex);

	SegmentData materialdata;
	if (!_MaterialManager.FindSegment(material.GetUUID(), materialdata))
		return false;
	index = materialdata.first / sizeof(MaterialData);
	return true;
}

bool IndirectDrawManager::GetMaterialIndex(Material& material, uint32_t& index)
{
	auto guard = LockGuard(_materialMutex);

	SegmentData materialdata;
	if (!_MaterialManager.FindSegment(material.GetUUID(), materialdata))
		return false;
	index = uint32_t(materialdata.first / sizeof(MaterialData));
	return true;
}

std::shared_ptr<StorageBlock> IndirectDrawManager::GetMaterialSSBO() {
	return _MaterialManager.GetBuffer()->GetBlock();
}

IndirectDrawManager::IndirectDrawManager() {}

std::shared_ptr<BindlessTextureManager> BindlessTextureManager::Instance()
{
	static auto instance = std::shared_ptr<BindlessTextureManager>(new BindlessTextureManager());
	return instance;
}

BindlessIndex BindlessTextureManager::RegisterOrUpdateTexture(const std::shared_ptr<Texture2D>& tex)
{
	if (!tex)
		return BindlessIndexNull;
	return RegisterOrUpdateTexture(tex.get());
}

BindlessIndex BindlessTextureManager::RegisterOrUpdateTexture(const Texture2D* tex)
{
	if (!tex)
		return BindlessIndexNull;

	LockGuard guard(_mutex);
	if (auto it = _textureEntrys.find(tex); it != _textureEntrys.end())
	{
		auto [tex, index] = *it;
		TextureDescBindEntry& entry = _entrys[index];
		if (_entrys[index].version != tex->GetDescBindEntryVersion())
		{
			entry = std::move(tex->GetDescBindEntry());
		}
		return index;
	}
	else
	{
		BindlessIndex newIndex = _entrys.size();
		TextureDescBindEntry entry = tex->GetDescBindEntry();
		if (!entry.image || !entry.imageView || !entry.sampler)
			return BindlessIndexNull;

		_entrys.push_back(std::move(entry));

		_texturePtrs.push_back(tex);
		_textureEntrys[tex] = newIndex;

		return newIndex;
	}
}

BindlessIndex BindlessTextureManager::GetTextureIndex(const std::shared_ptr<Texture2D>& tex)
{
	if (!tex)
		return BindlessIndexNull;
	return GetTextureIndex(tex.get());
}

BindlessIndex BindlessTextureManager::GetTextureIndex(const Texture2D* tex)
{
	if (!tex)
		return BindlessIndexNull;

	LockGuard guard(_mutex);
	if (auto it = _textureEntrys.find(tex); it != _textureEntrys.end())
		return it->second;

	return BindlessIndexNull;
}

void BindlessTextureManager::UnregisterTexture(const std::shared_ptr<Texture2D>& tex)
{
	if (!tex)
		return;
	UnregisterTexture(tex.get());
}

void BindlessTextureManager::UnregisterTexture(const Texture2D* tex)
{
	if (!tex)
		return;

	LockGuard guard(_mutex);
	if (auto it = _textureEntrys.find(tex); it == _textureEntrys.end())
		return;

	dirtyTexture.push_back(tex);
}

std::vector<TextureDescBindEntry> BindlessTextureManager::GetTextureDescBindEntrys() const
{
	LockGuard guard(_mutex);
	return _entrys;
}

void BindlessTextureManager::ClearDirtyTexture()
{
	if (dirtyTexture.empty())
		return;

	LockGuard guard(_mutex);
	if (dirtyTexture.empty())
		return;

	for (auto& tex : dirtyTexture)
	{
		auto it = _textureEntrys.find(tex);
		if (it == _textureEntrys.end())
			continue;

		uint32_t index = it->second;
		_textureEntrys.erase(it);

		uint32_t tailIndex = _texturePtrs.size() - 1;
		if (index != tailIndex)
		{
			const Texture2D* lastPtr = _texturePtrs[tailIndex];
			_textureEntrys[lastPtr] = index;
			std::swap(_entrys[index], _entrys[tailIndex]);
			std::swap(_texturePtrs[index], _texturePtrs[tailIndex]);
		}

		_texturePtrs.pop_back();
		_entrys.pop_back();
	}

	dirtyTexture.clear();
}
