#pragma once

#include "RenderGraphContext.h"
#include "ResourcePool.h"
#include "RenderGraphResourceManager.h"
#include "../RenderPass/RenderPassBase.h"
#include <unordered_set>

namespace OpenGLRenderGraph
{


	class PassNode
	{
	public:

		PassNode(const std::string& name);

		// 资源依赖声明
		PassNode& Input(const RenderGraphResource& resource);
		PassNode& InputOption(const RenderGraphResource& resource);
		PassNode& Output(const RenderGraphResource& resource);
		PassNode& Temp(const RenderGraphResource& resource);
		PassNode& Persistent(const RenderGraphResource& resource);
		PassNode& External(const ExternalResource& resource);

		// 顺序依赖声明
		PassNode& After(PassNode* node);
		PassNode& Before(PassNode* node);

		// 逻辑设定
		PassNode& SetRenderPass(std::unique_ptr<RenderPassBase>&& pass);

		const std::string& GetName() const;

		bool GetEnable()const;
		void SetEnable(bool enabled);

	public:
		template<typename... Resources>
		auto Input(Resources&&... resources) -> typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type;
		template<typename... Resources>
		auto InputOption(Resources&&... resources) -> typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type;
		template<typename... Resources>
		auto Output(Resources&&... resources) -> typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type;
		template<typename... Resources>
		auto Temp(Resources&&... resources) -> typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type;
		template<typename... Resources>
		auto Persistent(Resources&&... resources) -> typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type;
		template<typename... Passes>
		auto After(Passes&&... passes) -> typename std::enable_if<(sizeof...(Passes) > 1), PassNode&>::type;
		template<typename... Passes>
		auto Before(Passes&&... passes) -> typename std::enable_if<(sizeof...(Passes) > 1), PassNode&>::type;
		template<typename... Passes>
		auto External(Passes&&... passes) -> typename std::enable_if<(sizeof...(Passes) > 1), PassNode&>::type;

	public:
		int GetIndex() const;
		int GetBatch() const;
		void SetIndex(int index);
		void SetBatch(int batch);

		const std::vector<RenderGraphResource>& GetInputs() const;
		const std::vector<RenderGraphResource>& GetInputOptions() const;
		const std::vector<RenderGraphResource>& GetOutputs() const;
		const std::vector<RenderGraphResource>& GetTemps() const;
		const std::vector<RenderGraphResource>& GetPersistents() const;
		const std::vector<ExternalResource>& GetExternals() const;
		const std::vector<RenderGraphResource>& GetLifeCycleResource() const;
		const std::unordered_set<PassNode*>& GetAfters() const;
		const std::unordered_set<PassNode*>& GetBefores() const;

		bool ShouldExecute(RenderState& state) const;
		void Execute(const PassContext& ctx, RenderState& state);

		void FrameBegin(RenderState& state);
		void FrameEnd(RenderState& state);

	public:
		bool IsDone();
		void SetDone(bool done);

	private:
		std::string _name;

		std::vector<RenderGraphResource> _inputs;				//输入
		std::vector<RenderGraphResource> _inputOptions;			//可选输入
		std::vector<RenderGraphResource> _outputs;				//输出
		std::vector<RenderGraphResource> _temps;				//临时资源
		std::vector<RenderGraphResource> _persistents;			//持久资源
		std::vector<ExternalResource> _externals;				//外部资源

		std::vector<RenderGraphResource> _lifeCycleResource;	//记录需要控制生命周期的资源

		std::unordered_set<PassNode*> _afters;			//顺序依赖
		std::unordered_set<PassNode*> _befores;			//顺序依赖

		std::unique_ptr<RenderPassBase> _render;

		int _index = -1;
		int _batch = -1;

		bool _enable;

		bool _isDone;
	};

}

#include "PassNode.inl"