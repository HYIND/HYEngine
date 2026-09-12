#pragma once

#include "VulkanRenderEngine/General/GeneralSegmentBuffer.h"
#include "RenderPassBase.h"

class RTCoreRayTraceGeneralPass;

class RTCoreRayTraceGeneralBuffer
{
public:
	RTCoreRayTraceGeneralBuffer();

	vk::AccelerationStructureKHR GetAccelerationStructure() const;
	std::shared_ptr<StorageBlock> GetInstancesInfosBlock() const;

private:
	SegmentBufferManager<StorageSegmentBuffer, std::string> _blasBufferManager;	//blas数据缓冲区

	std::shared_ptr<StorageBlock> _tlasBlock;									//tlas数据缓冲区
	std::shared_ptr<StorageBlock> _tlasInstancesBlock;							//tlas数据需要引用的实例
	vk::AccelerationStructureKHR _tlasAccelerationStructure = VK_NULL_HANDLE;	//tlasHandle

	std::shared_ptr<StorageBlock> _tlasInstancesInfosBlock;						//tlas自定义实例数据

	std::shared_ptr<StorageBlock> _scratchBlock;								//暂存临时缓冲区

	friend RTCoreRayTraceGeneralPass;
};

class RTCoreRayTraceGeneralPass :public RenderPassBase
{
public:
	RTCoreRayTraceGeneralPass();
	~RTCoreRayTraceGeneralPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	std::shared_ptr<RTCoreRayTraceGeneralBuffer> GetGeneralBuffer();

private:
	bool SetupGeneralBuffer(RenderState& state);

private:
	std::shared_ptr<RTCoreRayTraceGeneralBuffer> _buffers;
	std::shared_ptr<VKWrapper::VKFence> _fence;
};
