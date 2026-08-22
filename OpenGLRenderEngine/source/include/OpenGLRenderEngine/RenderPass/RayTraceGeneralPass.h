#pragma once

#include "OpenGLRenderEngine/General/RayTraceGeneralData.h"
#include "OpenGLRenderEngine/General/GeneralSegmentBuffer.h"
#include "RenderPassBase.h"

class RayTraceGeneralPass;

class RayTraceGeneralBuffer
{
public:
	RayTraceGeneralBuffer();

	std::shared_ptr<SSBO> GetTraiangles();
	std::shared_ptr<SSBO> GetTraiangleExt();
	std::shared_ptr<SSBO> GetMeshBVHNode();
	std::shared_ptr<SSBO> GetMeshmatData();
	std::shared_ptr<SSBO> GetWorldBVHNode();

private:
	SegmentBufferManager<SSBOSegmentBuffer, std::string> _traiangleBufferManager;
	SegmentBufferManager<SSBOSegmentBuffer, std::string> _traiangleExtBufferManager;
	SegmentBufferManager<SSBOSegmentBuffer, std::string> _meshBVHNodeBufferManager;

	std::shared_ptr<SSBO> _SSBO_MeshmatData;
	std::shared_ptr<SSBO> _SSBO_WorldBVHNode;

	friend RayTraceGeneralPass;
};

class RayTraceGeneralPass :public RenderPassBase
{
public:
	RayTraceGeneralPass();
	~RayTraceGeneralPass();

	virtual bool ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);
	virtual void FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);

	std::shared_ptr<RayTraceGeneralBuffer> GetGeneralBuffer();

private:
	bool SetupGeneralBuffer(RenderState& state);

private:
	std::shared_ptr<RayTraceGeneralBuffer> _buffers;
	GLsync _setupfence;
};
