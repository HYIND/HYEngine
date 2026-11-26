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

	class RetireSkinned :public VKWrapper::IVKResource
	{
	public:
		RetireSkinned(const std::string& uuid)
			:uuid(uuid)
		{}
		virtual void Destroy() {
			IndirectDrawManager::Instance()->DeleteSkinned(uuid);
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

	// 延迟回收的是槽位，不是贴图指针：在途帧的材质数据里可能还写着这个槽位，
	// 等它们用完再换成占位图；指针映射在 UnregisterTexture 里已经删掉了
	class RetireBindlessTexture :public VKWrapper::IVKResource
	{
	public:
		RetireBindlessTexture(BindlessIndex slot)
			:slot(slot)
		{}
		virtual void Destroy() {
			BindlessTextureManager::Instance()->ReleaseSlot(slot);
		}

	public:
		BindlessIndex slot = BindlessIndexNull;
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
	// 空贴图没有 image，取描述符会解引用空指针；不注册，材质那边按「这一槽没有贴图」处理
	if (!tex || tex->IsEmpty())
		return BindlessIndexNull;

	// 换下来的旧描述符直接覆盖：image / view / sampler 释放时自己会走 Retire，在途帧还能用
	BindlessIndex result = BindlessIndexNull;
	{
		LockGuard guard(_mutex);
		if (auto it = _textureEntrys.find(tex); it != _textureEntrys.end())
		{
			const BindlessIndex index = it->second;
			if (_entrys[index].version != tex->GetDescBindEntryVersion())
			{
				auto entry = tex->GetDescBindEntry(vk::ImageAspectFlagBits::eColor);
				if (entry.image && entry.imageView && entry.sampler)
				{
					_entrys[index] = std::move(entry);
					_snapshot.reset();
				}
			}
			result = index;
		}
		else
		{
			// 先置注册标记再取描述符：取描述符之后贴图再换 sampler / view，就会走 MarkDirty，下一帧刷新
			tex->_bindlessRegistered.store(true, std::memory_order_seq_cst);
			TextureDescBindEntry entry = tex->GetDescBindEntry(vk::ImageAspectFlagBits::eColor);
			if (!entry.image || !entry.imageView || !entry.sampler)
				tex->_bindlessRegistered.store(false, std::memory_order_seq_cst);
			else
			{
				if (_idleSlot.empty())
				{
					result = static_cast<BindlessIndex>(_entrys.size());
					_entrys.push_back(std::move(entry));
				}
				else
				{
					// 空闲槽位里是占位图（ReleaseSlot 换上的），直接覆盖
					result = _idleSlot.front();
					_idleSlot.pop();
					_entrys[result] = std::move(entry);
				}
				_textureEntrys[tex] = result;
				_snapshot.reset();
			}
		}
	}
	return result;
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

	// 映射立刻删：这个地址马上可能被新的 Texture2D 复用，不能让新贴图命中旧映射，
	// 也不能让几帧后的延迟回收删掉新贴图的映射
	BindlessIndex index = BindlessIndexNull;
	{
		LockGuard guard(_mutex);
		auto it = _textureEntrys.find(tex);
		if (it == _textureEntrys.end())
			return;
		index = it->second;
		_textureEntrys.erase(it);
	}
	{
		std::lock_guard dirtyGuard(_dirtyMutex);
		_dirtyTextures.erase(tex);
	}

	// 槽位里的描述符（持有旧的 image / view / sampler）留到在途帧用完再换掉
	VKCONTEXT->Retire(new ReitreRes::RetireBindlessTexture(index));
}

std::vector<TextureDescBindEntry> BindlessTextureManager::GetTextureDescBindEntrys() const
{
	SharedLockGuard guard(_mutex);
	return _entrys;
}

std::shared_ptr<const TextureArraySnapshot> BindlessTextureManager::GetSnapshot() const
{
	{
		SharedLockGuard guard(_mutex);
		if (_snapshot)
			return _snapshot;
	}

	// 表变过：重建一份。几个线程同时发现为空时只有第一个真的建
	LockGuard guard(_mutex);
	if (!_snapshot)
	{
		auto snapshot = std::make_shared<TextureArraySnapshot>();
		snapshot->version = NextTextureArraySnapshotVersion();
		snapshot->entries = _entrys;
		_snapshot = std::move(snapshot);
	}
	return _snapshot;
}

void BindlessTextureManager::MarkDirty(const Texture2D* tex)
{
	if (!tex)
		return;
	std::lock_guard guard(_dirtyMutex);
	_dirtyTextures.insert(tex);
}

uint32_t BindlessTextureManager::RefreshChangedTextures()
{
	// 先把脏集合换出来再刷新：刷新期间又有贴图变了，会进新的集合，下一帧再刷
	std::unordered_set<const Texture2D*> dirty;
	{
		std::lock_guard guard(_dirtyMutex);
		if (_dirtyTextures.empty())
			return 0;
		dirty.swap(_dirtyTextures);
	}

	uint32_t refreshed = 0;
	LockGuard guard(_mutex);
	for (const Texture2D* tex : dirty)
	{
		// 还在映射表里才能碰：Texture2D 析构时第一件事就是在写锁里删掉自己的映射
		auto it = _textureEntrys.find(tex);
		if (it == _textureEntrys.end())
			continue;
		const BindlessIndex index = it->second;
		if (index >= _entrys.size() || _entrys[index].version == tex->GetDescBindEntryVersion())
			continue;
		auto entry = tex->GetDescBindEntry(vk::ImageAspectFlagBits::eColor);
		if (!entry.image || !entry.imageView || !entry.sampler)
			continue;
		_entrys[index] = std::move(entry);
		++refreshed;
	}
	if (refreshed > 0)
		_snapshot.reset();
	return refreshed;
}

// 空闲槽位里放的占位图：1×1 透明黑，上传后固定成只读布局。
std::shared_ptr<Texture2D> BindlessTextureManager::CreatePlaceholderTexture()
{
	TextureConfig config;
	config.minFilter = vk::Filter::eNearest;
	config.magFilter = vk::Filter::eNearest;
	auto texture = std::make_shared<Texture2D>(1u, 1u, vk::Format::eR8G8B8A8Unorm, config, 1u);
	if (!texture || texture->IsEmpty())
		return nullptr;

	uint8_t pixel[4] = { 0, 0, 0, 0 };
	texture->UpdateTextureData(pixel);
	if (!texture->MakeSampledImmutable())
		std::cerr << "[ BindlessTextureManager ] WARNING: 占位图切只读失败，空闲槽位每次绑定都会走布局记录\n";
	return texture;
}

void BindlessTextureManager::ReleaseSlot(BindlessIndex index)
{
	// 占位图的上传、切只读都会等 GPU：在锁外建，免得拿着写锁等，挡住各 Pass 的 Bind
	std::shared_ptr<Texture2D> placeholder;
	{
		SharedLockGuard guard(_mutex);
		placeholder = _placeholderTexture;
	}
	if (!placeholder && !VKCONTEXT->IsShutdown())
		placeholder = CreatePlaceholderTexture();

	std::shared_ptr<Texture2D> duplicate;	// 别的线程抢先建好了：多建的这张在锁外放掉（声明在锁之前，析构在锁之后）
	LockGuard guard(_mutex);
	if (!_placeholderTexture)
		_placeholderTexture = placeholder;
	else if (placeholder != _placeholderTexture)
		duplicate = std::move(placeholder);

	if (index >= _entrys.size())
		return;

	// 占位图建不出来（设备已经关了等）：槽位里留着旧描述符（对象仍然有效，只是晚些释放），照常回收槽位
	if (_placeholderTexture)
	{
		_entrys[index] = _placeholderTexture->GetDescBindEntry(vk::ImageAspectFlagBits::eColor);
		_snapshot.reset();
	}
	_idleSlot.push(index);
}

BindlessTextureManager::BindlessTextureManager() {
	_aspect = vk::ImageAspectFlagBits::eColor;
	// 单例一直活到进程退出：设备销毁前由 VKContext 回调释放它持有的描述符和占位图
	VKCONTEXT->AddShutdownCallback([this] { Release(); });
}

void BindlessTextureManager::Release()
{
	{
		std::lock_guard dirtyGuard(_dirtyMutex);
		_dirtyTextures.clear();
	}

	std::vector<TextureDescBindEntry> entries;
	std::shared_ptr<Texture2D> placeholder;
	std::shared_ptr<const TextureArraySnapshot> snapshot;
	{
		LockGuard guard(_mutex);
		snapshot = std::move(_snapshot);
		for (const auto& [tex, index] : _textureEntrys)
			tex->_bindlessRegistered.store(false, std::memory_order_seq_cst);
		_textureEntrys.clear();
		entries.swap(_entrys);
		std::queue<uint32_t>().swap(_idleSlot);
		placeholder = std::move(_placeholderTexture);
	}
	// 锁外放掉：image / view / sampler 释放时会 Retire；占位图析构时还会回来调 UnregisterTexture
	entries.clear();
	snapshot.reset();
	placeholder.reset();
}

IndirectDrawManager::IndirectDrawManager()
{
	// 单例一直活到进程退出：设备销毁前由 VKContext 回调释放段缓冲
	VKCONTEXT->AddShutdownCallback([this] { Release(); });
}

void IndirectDrawManager::Release()
{
	WithWriteLock({ SharedLockType::Mesh, SharedLockType::Material, SharedLockType::Skinned }, [this]()
		{
			_VertexManager.Release();
			_IndexManager.Release();
			_MaterialManager.Release();
			_ShadowMaterialManager.Release();
			_SkinnedManager.Release();
		});
}

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
		auto shadowData = material.GetShadowMaterialCompData();
		_ShadowMaterialManager.SetSegment(uuid, (void*)version, &shadowData, sizeof(shadowData));
	}
}

void IndirectDrawManager::RetireMaterial(Material& material)
{
	VKCONTEXT->Retire(new ReitreRes::RetireMaterial(material.GetUUID()));
}

void IndirectDrawManager::SetupSkinned(SkinnedMesh& mesh)
{
	auto guard = LockGuard(_skinnedMutex);
	SetupSkinned_LockFree(mesh);
}

void IndirectDrawManager::RetireSkinned(SkinnedMesh& mesh)
{
	VKCONTEXT->Retire(new ReitreRes::RetireSkinned(mesh.GetUUID()));
}

bool IndirectDrawManager::GetSkinnedIndex(SkinnedMesh& mesh, uint32_t& index)
{
	auto guard = SharedLockGuard(_skinnedMutex);
	return GetSkinnedIndex_LockFree(mesh, index);
}

std::shared_ptr<VertexBufferBlock> IndirectDrawManager::GetSkinnedVertexBlock() const
{
	return _SkinnedManager.GetBuffer()->GetBlock();
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

std::shared_ptr<StorageBlock> IndirectDrawManager::GetMaterialSSBO() const {
	return _MaterialManager.GetBuffer()->GetBlock();
}

std::shared_ptr<StorageBlock> IndirectDrawManager::GetShadowMaterialSSBO() const {
	return _ShadowMaterialManager.GetBuffer()->GetBlock();
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
		auto shadowData = material.GetShadowMaterialCompData();
		_ShadowMaterialManager.SetSegment(uuid, (void*)version, &shadowData, sizeof(shadowData));
	}
}

void IndirectDrawManager::SetupSkinned_LockFree(SkinnedMesh& mesh)
{
	const auto& skinned = mesh.GetSkinnedData();
	const auto uuid = mesh.GetUUID();
	if (skinned.empty())
	{
		_SkinnedManager.RemoveSegment(uuid);
		return;
	}

	const auto version = mesh.GetVerticesIndicesVsrsion();
	_SkinnedManager.SetSegment(uuid, reinterpret_cast<void*>(static_cast<uintptr_t>(version)), skinned.data(), skinned.size() * sizeof(SkinnedData));
}

bool IndirectDrawManager::GetMaterialIndex_LockFree(Material& material, uint64_t& index) const
{
	SegmentData materialdata;
	if (!_MaterialManager.FindSegment(material.GetUUID(), materialdata))
		return false;
	index = materialdata.first / sizeof(MaterialData);
	return true;
}

bool IndirectDrawManager::GetSkinnedIndex_LockFree(SkinnedMesh& mesh, uint32_t& index)
{
	SegmentData skinnedData;
	if (!_SkinnedManager.FindSegment(mesh.GetUUID(), skinnedData))
		return false;

	index = static_cast<uint32_t>(skinnedData.first / sizeof(SkinnedData));
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
	_ShadowMaterialManager.RemoveSegment(uuid);
}

void IndirectDrawManager::DeleteSkinned(const std::string& uuid)
{
	auto guard = LockGuard(_skinnedMutex);
	_SkinnedManager.RemoveSegment(uuid);
}
