#pragma once

#include "VulkanRenderEngine/General/GeneralSegmentBuffer.h"
#include "RenderPassBase.h"

class RTCoreRayTraceGeneralPass;

class BlasHandleManager
{
	struct BlasHandleData {
		std::weak_ptr<Mesh> meshWeakPtr;
		uint32_t meshVersion = 0;
		std::string meshUUID;
		vk::AccelerationStructureKHR handle;
		uint32_t offset;
	};

public:
	BlasHandleManager();

	void RemoveBlasData(std::shared_ptr<Mesh>& mesh, std::shared_ptr<VKCore::VulkanDevice>& device);

	bool FindBlasData(std::shared_ptr<Mesh>& mesh, BlasHandleData& data);

	bool UpdateBlasData(
		std::vector<std::shared_ptr<Mesh>>& meshs,
		std::shared_ptr<VertexBufferBlock>& vertexBlock,
		std::shared_ptr<IndexBufferBlock>& indexBlock,
		std::shared_ptr<VKCore::VulkanDevice>& device,
		std::shared_ptr<StorageBlock>& scratchBlock
	);

private:
	SegmentBufferManager<StorageSegmentBuffer, std::string> _blasBufferManager;	//blas数据缓冲区
	std::unordered_map<Mesh*, BlasHandleData> _blasHandles;
	vk::DeviceAddress _lastVertexAddress = 0;
	vk::DeviceAddress _lastIndexAddress = 0;

	friend RTCoreRayTraceGeneralPass;
};

class RTCoreRayTraceGeneralBuffer
{
public:
	RTCoreRayTraceGeneralBuffer();

	vk::AccelerationStructureKHR GetAccelerationStructure() const;
	std::shared_ptr<StorageBlock> GetInstancesInfosBlock() const;

private:
	BlasHandleManager _blasManager;

	std::shared_ptr<StorageBlock> _tlasBlock;									//tlas数据缓冲区
	std::shared_ptr<StorageBlock> _tlasInstancesBlock;							//tlas数据需要引用的实例
	vk::AccelerationStructureKHR _tlasAccelerationStructure = VK_NULL_HANDLE;	//tlasHandle

	std::shared_ptr<StorageBlock> _tlasInstancesInfosBlock;						//tlas自定义实例数据

	friend RTCoreRayTraceGeneralPass;
};

class RTCoreRayTraceGeneralPass :public RenderPassBase
{

public:
	RTCoreRayTraceGeneralPass();
	~RTCoreRayTraceGeneralPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	std::shared_ptr<RTCoreRayTraceGeneralBuffer> GetGeneralBuffer();

private:
	bool SetupGeneralBuffer(RenderState& state);

private:
	std::shared_ptr<RTCoreRayTraceGeneralBuffer> _buffers;
	std::shared_ptr<VKWrapper::VKFence> _fence;
	std::shared_ptr<StorageBlock> _scratchBlock;	//暂存临时缓冲区
};
