#pragma once

#include "VulkanRenderEngine/General/GeneralSegmentBuffer.h"
#include "RenderPassBase.h"

class RTCoreRayTraceGeneralPass;

class BlasHandleManager
{
	struct BlasHandleData {
		std::weak_ptr<StaticMesh> meshWeakPtr;
		uint32_t meshVersion = 0;
		std::string meshUUID;
		vk::AccelerationStructureKHR handle;
		uint32_t offset;
	};

public:
	// 蒙皮网格的 BLAS 请求。
	// 蒙皮顶点每帧都在变，所以 BLAS 每帧都要更新；但拓扑（顶点数/索引数）不变，
	// 所以用 eUpdate（refit）而不是重建，BLAS 本体按「姿势身份 + 网格」跨帧复用。
	struct SkinnedBlasRequest
	{
		std::shared_ptr<SkinnedMesh> mesh;
		std::shared_ptr<VKRenderContext::SkinnedPoseHistory> poseHistory;	// 身份：同一个实体的同一份姿势
		std::shared_ptr<VertexBufferBlock> vertexBuffer;					// 本帧 SkinnedPass 算出的顶点缓冲
		uint32_t vertexStart = 0;											// 该网格在 vertexBuffer 里的起始顶点
		uint32_t vertexCount = 0;
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
	};

	BlasHandleManager();

	void RemoveBlasData(std::shared_ptr<StaticMesh>& mesh, std::shared_ptr<VKCore::VulkanDevice>& device);

	bool FindBlasData(std::shared_ptr<StaticMesh>& mesh, BlasHandleData& data);

	bool UpdateBlasData(
		std::vector<std::shared_ptr<StaticMesh>>& meshs,
		std::shared_ptr<VertexBufferBlock>& vertexBlock,
		std::shared_ptr<IndexBufferBlock>& indexBlock,
		std::shared_ptr<VKCore::VulkanDevice>& device,
		std::shared_ptr<StorageBlock>& scratchBlock
	);

	// 建立/更新蒙皮网格的 BLAS，outAddresses 与 requests 一一对应（失败项为 0）
	bool UpdateSkinnedBlas(
		std::vector<SkinnedBlasRequest>& requests,
		std::vector<vk::DeviceAddress>& outAddresses,
		std::shared_ptr<IndexBufferBlock>& indexBlock,
		std::shared_ptr<VKCore::VulkanDevice>& device,
		std::shared_ptr<StorageBlock>& scratchBlock
	);

	// 回收长期没用到的蒙皮 BLAS（实体已销毁，或者换了网格）
	void PruneSkinnedBlas(std::shared_ptr<VKCore::VulkanDevice>& device, uint64_t maxUnusedFrames);

private:
	struct SkinnedBlasData
	{
		std::weak_ptr<SkinnedMesh> meshWeakPtr;
		std::weak_ptr<VKRenderContext::SkinnedPoseHistory> poseWeakPtr;	// 指针地址可能被复用，必须校验对象还是不是同一个
		std::string key;
		vk::AccelerationStructureKHR handle = VK_NULL_HANDLE;
		vk::DeviceAddress address = 0;
		uint32_t primitiveCount = 0;
		uint32_t scratchSize = 0;
		uint64_t lastUsedFrame = 0;
	};

private:
	SegmentBufferManager<StorageSegmentBuffer, std::string> _blasBufferManager;	//blas数据缓冲区
	std::unordered_map<StaticMesh*, BlasHandleData> _blasHandles;
	vk::DeviceAddress _lastVertexAddress = 0;
	vk::DeviceAddress _lastIndexAddress = 0;

	SegmentBufferManager<StorageSegmentBuffer, std::string> _skinnedBlasBufferManager;	//蒙皮网格的blas数据缓冲区
	std::unordered_map<std::string, SkinnedBlasData> _skinnedBlasHandles;
	uint64_t _skinnedFrameIndex = 0;

	friend RTCoreRayTraceGeneralPass;
};

class RTCoreRayTraceGeneralBuffer
{
public:
	RTCoreRayTraceGeneralBuffer();

	vk::AccelerationStructureKHR GetAccelerationStructure() const;
	std::shared_ptr<StorageBlock> GetInstancesInfosBlock() const;
	std::shared_ptr<VertexBufferBlock> GetSkinnedVertexBlock() const;

private:
	BlasHandleManager _blasManager;

	std::shared_ptr<StorageBlock> _tlasBlock;									//tlas数据缓冲区
	std::shared_ptr<StorageBlock> _tlasInstancesBlock;							//tlas数据需要引用的实例
	vk::AccelerationStructureKHR _tlasAccelerationStructure = VK_NULL_HANDLE;	//tlasHandle

	std::shared_ptr<StorageBlock> _tlasInstancesInfosBlock;						//tlas自定义实例数据

	std::shared_ptr<VertexBufferBlock> _skinnedVertexBlock;						//本帧蒙皮算出来的顶点，命中着色器要用它取顶点

	friend RTCoreRayTraceGeneralPass;
};

class RTCoreRayTraceGeneralPass :public RenderPassBase
{

public:
	RTCoreRayTraceGeneralPass();
	~RTCoreRayTraceGeneralPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	std::shared_ptr<RTCoreRayTraceGeneralBuffer> GetGeneralBuffer();

private:
	bool SetupGeneralBuffer(RenderState& state);

private:
	std::shared_ptr<RTCoreRayTraceGeneralBuffer> _buffers;
	std::shared_ptr<StorageBlock> _scratchBlock;	//暂存临时缓冲区
};
