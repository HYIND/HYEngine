#pragma once

#include "OpenGLRenderEngine/RenderGraph/RenderGraphContext.h"
#include "OpenGLRenderEngine/General/RenderState.h"

class RenderPassBase
{
public:
	virtual ~RenderPassBase() = default;

	virtual bool ShouldExecute(RenderState& state) const { return true; }	// Excute前预判断是否需要执行Execute，但不影响FrameBegin，FrameEnd
	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state) = 0;	// 执行渲染

	virtual void FrameBegin(RenderState& state) {};							// 每帧开始时，可执行准备任务
	virtual void FrameEnd(RenderState& state) {};							// 每帧结束时，可执行清理任务

protected:
	bool _enabled = true;
};

inline std::unique_ptr<RenderPassBase> MakeLambdaPass(std::function<void(const OpenGLRenderGraph::PassContext&, RenderState&)> func)
{
	class LambdaPass : public RenderPassBase
	{
	public:
		LambdaPass(std::function<void(const OpenGLRenderGraph::PassContext&, RenderState&)> f) : m_func(f) {}
		virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state) override {
			if (m_func) m_func(ctx, state);
		}
	private:
		std::function<void(const OpenGLRenderGraph::PassContext&, RenderState&)> m_func;
	};
	return std::make_unique<LambdaPass>(func);
}
