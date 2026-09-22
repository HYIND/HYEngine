#include "vkstdafx.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

namespace ReitreRes
{
	class RetireMesh :public VKWrapper::IVKResource
	{
	public:
		RetireMesh(const std::string& uuid)
			:uuid(uuid)
		{}
		virtual void Destroy() {
			IndirectDrawManager::Instance()->DeleteMesh(uuid);
		}

	public:
		std::string uuid;
	};

	class RetireMaterial :public VKWrapper::IVKResource
	{
	public:
		RetireMaterial(const std::string& uuid)
			:uuid(uuid)
		{}
		virtual void Destroy() {
			IndirectDrawManager::Instance()->DeleteMaterial(uuid);
		}

	public:
		std::string uuid;
	};

	class RetireBindlessTexture :public VKWrapper::IVKResource
	{
	public:
		RetireBindlessTexture(const Texture2D* tex)
			:tex(tex)
		{}
		virtual void Destroy() {
			BindlessTextureManager::Instance()->DeleteTexture(tex);
		}

	public:
		const Texture2D* tex = nullptr;
	};
}

std::shared_ptr<Texture2D> BindlessTextureManager::_placeholderTexture;

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
		auto [storedTex, index] = *it;
		TextureDescBindEntry& entry = _entrys[index];
		if (_entrys[index].version != storedTex->GetDescBindEntryVersion())
		{
			entry = std::move(storedTex->GetDescBindEntry(vk::ImageAspectFlagBits::eColor));
		}
		return index;
	}
	else
	{
		TextureDescBindEntry entry = tex->GetDescBindEntry(vk::ImageAspectFlagBits::eColor);
		if (!entry.image || !entry.imageView || !entry.sampler)
			return BindlessIndexNull;

		if (_idleSlot.empty())
		{
			BindlessIndex newIndex = _entrys.size();

			_entrys.push_back(std::move(entry));
			_textureEntrys[tex] = newIndex;
			return newIndex;
		}
		else
		{
			BindlessIndex idleIndex = _idleSlot.front();
			_idleSlot.pop();

			_entrys[idleIndex] = std::move(entry);
			_textureEntrys[tex] = idleIndex;
			return idleIndex;
		}
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

	SharedLockGuard guard(_mutex);
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

	{
		SharedLockGuard guard(_mutex);
		if (auto it = _textureEntrys.find(tex); it == _textureEntrys.end())
			return;
	}

	VKCONTEXT->Retire(new ReitreRes::RetireBindlessTexture(tex));
}

std::vector<TextureDescBindEntry> BindlessTextureManager::GetTextureDescBindEntrys() const
{
	SharedLockGuard guard(_mutex);
	return _entrys;
}

void BindlessTextureManager::DeleteTexture(const Texture2D* tex)
{
	LockGuard guard(_mutex);
	auto it = _textureEntrys.find(tex);
	if (it == _textureEntrys.end())
		return;

	if (!_placeholderTexture)
		_placeholderTexture = std::make_shared<Texture2D>(1, 1);

	uint32_t index = it->second;
	_idleSlot.push(index);
	_entrys[index] = _placeholderTexture->GetDescBindEntry(vk::ImageAspectFlagBits::eColor);
	_textureEntrys.erase(it);
}

BindlessTextureManager::BindlessTextureManager() {
	_aspect = vk::ImageAspectFlagBits::eColor;
}

IndirectDrawManager::IndirectDrawManager() {}

std::shared_ptr<IndirectDrawManager> IndirectDrawManager::Instance()
{
	static std::shared_ptr<IndirectDrawManager> instance = std::shared_ptr<IndirectDrawManager>(new IndirectDrawManager());
	return instance;
}

void IndirectDrawManager::SetupMesh(Mesh& mesh)
{
	auto uuid = mesh.GetUUID();
	auto version = mesh.GetVerticesIndicesVsrsion();

	auto guard = LockGuard(_meshMutex);
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

void IndirectDrawManager::RetireMesh(Mesh& mesh)
{
	VKCONTEXT->Retire(new ReitreRes::RetireMesh(mesh.GetUUID()));
}

bool IndirectDrawManager::GetIndirectDrawMeta(Mesh& mesh, IndirectDrawMeta& meta)
{
	auto uuid = mesh.GetUUID();
	SegmentData vbodata, ebodata;

	{
		auto guard = SharedLockGuard(_meshMutex);
		if (!_VertexManager.FindSegment(uuid, vbodata) || !_IndexManager.FindSegment(uuid, ebodata))
			return false;
	}

	meta.indexCount = ebodata.count / sizeof(unsigned int);
	meta.firstIndex = ebodata.first / sizeof(unsigned int);
	meta.vertexOffset = vbodata.first / sizeof(Vertex);

	return true;
}

std::shared_ptr<VertexBufferBlock> IndirectDrawManager::GetVertexBlock() const {
	return _VertexManager.GetBuffer()->GetBlock();
}

std::shared_ptr<IndexBufferBlock> IndirectDrawManager::GetIndexBlock() const {
	return _IndexManager.GetBuffer()->GetBlock();
}

void IndirectDrawManager::SetupMaterial(Material& material)
{
	for (auto& [type, tex] : material.GetTextures())
		BindlessTextureManager::Instance()->RegisterOrUpdateTexture(tex);

	auto uuid = material.GetUUID();
	auto version = material.GetVersion();

	auto guard = LockGuard(_materialMutex);
	SegmentData segmentData;
	if (!_MaterialManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
	{
		auto data = material.GetMaterialCompData();
		_MaterialManager.SetSegment(uuid, (void*)version, &data, sizeof(data));
	}
}

void IndirectDrawManager::RetireMaterial(Material& material)
{
	VKCONTEXT->Retire(new ReitreRes::RetireMaterial(material.GetUUID()));
}

bool IndirectDrawManager::GetMaterialIndex(Material& material, uint64_t& index) const
{
	auto guard = SharedLockGuard(_materialMutex);
	return GetMaterialIndex_LockFree(material, index);
}

bool IndirectDrawManager::GetMaterialIndex(Material& material, uint32_t& index) const
{
	auto guard = SharedLockGuard(_materialMutex);
	return GetMaterialIndex_LockFree(material, index);
}

std::shared_ptr<StorageBlock> IndirectDrawManager::GetMaterialSSBO() {
	return _MaterialManager.GetBuffer()->GetBlock();
}

void IndirectDrawManager::WithMeshWriteLock(const std::function<void()>& call)
{
	LockGuard guard(_meshMutex);
	if (call)
		call();
}


void IndirectDrawManager::WithMaterialWriteLock(const std::function<void()>& call)
{
	LockGuard guard(_materialMutex);
	if (call)
		call();
}

void IndirectDrawManager::WithMeshMaterialWriteLock(const std::function<void()>& call)
{
	LockGuard guard1(_meshMutex);
	LockGuard guard2(_materialMutex);
	if (call)
		call();
}

void IndirectDrawManager::SetupMesh_LockFree(Mesh& mesh)
{
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

void IndirectDrawManager::SetupMaterial_LockFree(Material& material)
{
	for (auto& [type, tex] : material.GetTextures())
		BindlessTextureManager::Instance()->RegisterOrUpdateTexture(tex);

	auto uuid = material.GetUUID();
	auto version = material.GetVersion();

	SegmentData segmentData;
	if (!_MaterialManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
	{
		auto data = material.GetMaterialCompData();
		_MaterialManager.SetSegment(uuid, (void*)version, &data, sizeof(data));
	}
}

void IndirectDrawManager::WithMeshSharedLock(const std::function<void()>& call)
{
	SharedLockGuard guard(_meshMutex);
	if (call)
		call();
}

void IndirectDrawManager::WithMaterialSharedLock(const std::function<void()>& call)
{
	SharedLockGuard guard(_materialMutex);
	if (call)
		call();
}

void IndirectDrawManager::WithMeshMaterialSharedLock(const std::function<void()>& call)
{
	SharedLockGuard guard1(_meshMutex);
	SharedLockGuard guard2(_materialMutex);
	if (call)
		call();
}

bool IndirectDrawManager::GetMaterialIndex_LockFree(Material& material, uint64_t& index) const
{
	SegmentData materialdata;
	if (!_MaterialManager.FindSegment(material.GetUUID(), materialdata))
		return false;
	index = materialdata.first / sizeof(MaterialData);
	return true;
}

bool IndirectDrawManager::GetMaterialIndex_LockFree(Material& material, uint32_t& index) const
{
	uint64_t temp;
	bool result = GetMaterialIndex_LockFree(material, temp);
	if (result)
		index = temp;
	return result;
}

bool IndirectDrawManager::GetIndirectDrawMeta_LockFree(Mesh& mesh, IndirectDrawMeta& meta)
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

void IndirectDrawManager::DeleteMesh(const std::string& uuid)
{
	auto guard = LockGuard(_meshMutex);
	_VertexManager.RemoveSegment(uuid);
	_IndexManager.RemoveSegment(uuid);
}

void IndirectDrawManager::DeleteMaterial(const std::string& uuid)
{
	auto guard = LockGuard(_materialMutex);
	_MaterialManager.RemoveSegment(uuid);
}
