#pragma once

#include "vkstdafx.h"
#include "RenderGraphContext.h"
#include "ResourcePool.h"
#include "RenderGraphResourceManager.h"
#include "../RenderPass/RenderPassBase.h"

class RenderPassBase;

namespace RenderGraph
{

	class PassNode
	{
	public:
		static std::string GetCurThreadPassName();

	public:
		struct ResourceData
		{
			RenderGraphResource resource;
			RenderGraphResourceLayout layout;
		};

		struct ExternalResourceData
		{
			ExternalResource resource;
			RenderGraphResourceLayout layout;
		};

	public:

		PassNode(const std::string& name);

		// 资源依赖声明
		PassNode& Input(const ResourceData& resource);
		PassNode& InputOption(const ResourceData& resource);
		PassNode& Output(const ResourceData& resource);
		PassNode& Temp(const ResourceData& resource);
		PassNode& Persistent(const ResourceData& resource);
		PassNode& External(const ExternalResourceData& resource);

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

		const std::vector<ResourceData>& GetInputs() const;
		const std::vector<ResourceData>& GetInputOptions() const;
		const std::vector<ResourceData>& GetOutputs() const;
		const std::vector<ResourceData>& GetTemps() const;
		const std::vector<ResourceData>& GetPersistents() const;
		const std::vector<ExternalResourceData>& GetExternals() const;
		const std::vector<RenderGraphResource>& GetLifeCycleResource() const;
		const std::unordered_set<PassNode*>& GetAfters() const;
		const std::unordered_set<PassNode*>& GetBefores() const;

		void FrameBegin(FrameDataRegistry& registry, RenderState& state);
		bool ShouldExecute(FrameDataRegistry& registry, RenderState& state);
		void Execute(PassFrameCmdContext& cmdCtx, FrameDataRegistry& registry, const PassFrameContext& ctx, RenderState& state);
		void FrameEnd(FrameDataRegistry& registry, RenderState& state);

	private:
		std::string _name;

		std::vector<ResourceData> _inputs;				//输入
		std::vector<ResourceData> _inputOptions;		//可选输入
		std::vector<ResourceData> _outputs;				//输出
		std::vector<ResourceData> _temps;				//临时资源
		std::vector<ResourceData> _persistents;			//持久资源
		std::vector<ExternalResourceData> _externals;				//外部资源

		std::vector<RenderGraphResource> _lifeCycleResource;	//记录需要控制生命周期的资源

		std::unordered_set<PassNode*> _afters;			//顺序依赖
		std::unordered_set<PassNode*> _befores;			//顺序依赖

		uint32_t _dependency = 0;
		std::vector<uint32_t> _nextIndexs;

		std::unique_ptr<RenderPassBase> _render;

		int _index = -1;
		int _batch = -1;

		bool _enable;

		friend class Graph;
	};

}

#include "PassNode.inl"