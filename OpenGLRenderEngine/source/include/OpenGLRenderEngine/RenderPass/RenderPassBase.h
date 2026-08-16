#pragma once

#include "OpenGLRenderEngine/RenderGraph/RenderGraphContext.h"
#include "OpenGLRenderEngine/General/RenderState.h"

class RenderPassBase
{
public:
	virtual ~RenderPassBase() = default;

	virtual bool ShouldExecute(RenderState& state) const { return true; }	// Excute前预判断是否需要执行Execute，但不影响FrameBegin，FrameEnd
	virtual void Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state) = 0;	// 执行渲染

	virtual void FrameBegin(RenderState& state) {};							// 每帧开始时，可执行准备任务，由Graph并行调度所有Pass，注意state线程安全问题
	virtual void FrameEnd(RenderState& state) {};							// 每帧结束时，可执行清理任务，按照pass执行顺序，逆序调度

protected:
	bool _enabled = true;
};

inline std::unique_ptr<RenderPassBase> MakeLambdaPass(std::function<void(const OpenGLRenderGraph::PassContext&, RenderState&)> func)
{
	class LambdaPass : public RenderPassBase
	{
	public:
		LambdaPass(std::function<void(const OpenGLRenderGraph::PassContext&, RenderState&)> f) : m_func(f) {}
		virtual void Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state) override {
			if (m_func) m_func(ctx, state);
		}
	private:
		std::function<void(const OpenGLRenderGraph::PassContext&, RenderState&)> m_func;
	};
	return std::make_unique<LambdaPass>(func);
}
