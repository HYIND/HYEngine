#pragma once

#include "RenderGraphContext.h"
#include "RenderGraphResourceManager.h"
#include "PassNode.h"
#include "DependencySolver.h"
#include "ThreadPool.h"

namespace OpenGLRenderGraph
{

	class RenderGraph
	{
	public:
		RenderGraph(const std::string& name = "");
		~RenderGraph();


		RenderGraphResource CreateTexture(const TextureDesc& desc, const ResourceName& name);	// 资源声明
		ExternalResource CreateExternalTexture(const ResourceName& name);						// 外部资源声明	

		void InjectExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture);	// 注入外部资源
		std::shared_ptr<Texture2D> GetExternalTexture(const ResourceName& name) const;
		void RemoveExternalTexture(const ResourceName& name);

		PassNode* AddPass(const std::string& name);		// 添加Pass
		PassNode* AddFence(const std::string& name);	// 添加栅栏

		void Compile();						// 编译（分析依赖和生命周期）
		void Execute(RenderState& state);	// 执行

		void Clear();// 清空

		PassNode* GetPass(const std::string& name) const;
		void SetPassEnabled(const std::string& name, bool enabled);// 配置

		void SetRenderTargetFBO(GLuint fbo);

	private:
		std::string _name;

		ResourceManager _resManager;

		std::vector<PassNode*> _passes;
		std::vector<std::unique_ptr<PassNode>> _ownedPasses;
		std::vector<PassNode*> _sortedPasses;

		std::unordered_map<RenderGraphResource, ResourceUsage> _lifecycles;

		bool _needsCompile = true;
		int _compiledVersion = 0;

		GLuint _renderTargetFBO = 0;

		ThreadPool _frameParallelPool;

		int64_t _lastCleanupTimeAccumulator;
		int64_t _CleanupThresold = 10;
	};
}