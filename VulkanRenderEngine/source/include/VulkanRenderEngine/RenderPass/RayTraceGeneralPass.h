#pragma once

#include "VulkanRenderEngine/General/RayTraceGeneralData.h"
#include "VulkanRenderEngine/General/GeneralSegmentBuffer.h"
#include "RenderPassBase.h"

class RayTraceGeneralPass;

class RayTraceGeneralBuffer
{
public:
	RayTraceGeneralBuffer();

	std::shared_ptr<StorageBlock> GetTraiangles();
	std::shared_ptr<StorageBlock> GetTraiangleExt();
	std::shared_ptr<StorageBlock> GetMeshBVHNode();
	std::shared_ptr<StorageBlock> GetMeshmatData();
	std::shared_ptr<StorageBlock> GetWorldBVHNode();

private:
	SegmentBufferManager<StorageSegmentBuffer, std::string> _traiangleBufferManager;
	SegmentBufferManager<StorageSegmentBuffer, std::string> _traiangleExtBufferManager;
	SegmentBufferManager<StorageSegmentBuffer, std::string> _meshBVHNodeBufferManager;

	std::shared_ptr<StorageBlock> _SSBO_MeshmatData;
	std::shared_ptr<StorageBlock> _SSBO_WorldBVHNode;

	friend RayTraceGeneralPass;
};

class RayTraceGeneralPass :public RenderPassBase
{
public:
	RayTraceGeneralPass();
	~RayTraceGeneralPass();

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state);
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state);

	std::shared_ptr<RayTraceGeneralBuffer> GetGeneralBuffer();

private:
	bool SetupGeneralBuffer(RenderState& state);

private:
	std::shared_ptr<RayTraceGeneralBuffer> _buffers;
	std::shared_ptr<VKWrapper::VKFence> _fence;
};
