#pragma once

#include "VulkanRenderEngine/RenderGraph/RenderGraphContext.h"
#include "VulkanRenderEngine/General/RenderState.h"

class RenderPassBase
{
public:
	virtual ~RenderPassBase() = default;

	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state) {};							// 每帧开始时，可执行准备任务，由Graph并行调度所有Pass，注意state线程安全问题
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) { return true; }			// Excute前预判断是否需要执行Execute，但不影响FrameBegin，FrameEnd
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) = 0;	// 执行渲染
	virtual void FrameEnd(RenderGraph::FrameDataRegistry& registry, RenderState& state) {};								// 每帧结束时，可执行清理任务，按照pass执行顺序，逆序调度

protected:
	bool _enabled = true;
};

inline std::unique_ptr<RenderPassBase> MakeLambdaPass(std::function<void(const RenderGraph::PassFrameContext&, RenderState&)> func)
{
	class LambdaPass : public RenderPassBase
	{
	public:
		LambdaPass(std::function<void(const RenderGraph::PassFrameContext&, RenderState&)> f) : m_func(f) {}
		virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override {
			if (m_func) m_func(ctx, state);
		}
	private:
		std::function<void(const RenderGraph::PassFrameContext&, RenderState&)> m_func;
	};
	return std::make_unique<LambdaPass>(func);
}
