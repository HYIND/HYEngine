#include "OpenGLRenderEngine/RenderGraph/RenderGraph.h"

using namespace OpenGLRenderGraph;

TextureDesc GetDesc(const std::shared_ptr<Texture2D>& texture)
{
	if (!texture)
		return TextureDesc();

	TextureDesc desc;
	desc.width = texture->GetWidth();
	desc.height = texture->GetHeight();
	desc.format = texture->GetInternalFormat();
	desc.filterMode = texture->GetMagFilter();
	desc.wrapMode = texture->GetWrapS();
}

RenderGraph::RenderGraph(const std::string& name)
	:_name(name)
{
}

RenderGraph::~RenderGraph()
{
	Clear();
}

// 资源创建
RenderGraphResource RenderGraph::CreateTexture(const TextureDesc& desc, const ResourceName& name)
{
	RenderGraphResource res;
	res.name = name;
	res.type = ResourceType::Texture;
	res.desc = desc;
	return res;
}

ExternalResource OpenGLRenderGraph::RenderGraph::CreateExternalTexture(const ResourceName& name)
{
	ExternalResource res;
	res.name = name;
	res.type = ResourceType::Texture;
	return res;
}

// 注入外部资源
void RenderGraph::InjectExternalTexture(const ResourceName& name, std::shared_ptr<Texture2D> texture)
{
	_resManager.RegisterExternalTexture(name, texture);

	_needsCompile = true;
}

void RenderGraph::RemoveExternalTexture(const ResourceName& name) {
	_resManager.UnregisterExternalTexture(name);
	_needsCompile = true;
}

std::shared_ptr<Texture2D> RenderGraph::GetExternalTexture(const ResourceName& name) const {
	return _resManager.GetExternalTexture(name);
}

// Pass管理
PassNode* RenderGraph::AddPass(const std::string& name) {
	auto pass = std::make_unique<PassNode>(name);
	_passes.push_back(pass.get());
	_ownedPasses.push_back(std::move(pass));
	_needsCompile = true;
	return _passes.back();
}

PassNode* OpenGLRenderGraph::RenderGraph::AddFence(const std::string& name)
{
	auto pass = std::make_unique<PassNode>(name);
	pass->SetEnable(false);
	_passes.push_back(pass.get());
	_ownedPasses.push_back(std::move(pass));
	_needsCompile = true;
	return _passes.back();
}

// 编译（分析依赖和生命周期）
void RenderGraph::Compile() {
	if (!_needsCompile) return;

	// 排序
	_sortedPasses = DependencySolver::SortPasses(_passes);
	DependencySolver::PrintPasses(_name, _sortedPasses);

	// 计算生命周期
	_lifecycles = DependencySolver::CalculateLifetimes(_sortedPasses);
	DependencySolver::PrintLifecycles(_name, _lifecycles);

	_needsCompile = false;
	_compiledVersion++;
}

// 执行
void RenderGraph::Execute(RenderState& state) {
	if (_needsCompile) {
		Compile();
	}

	for (auto& pass : _sortedPasses)
		pass->FrameBegin(state);

	for (int i = 0; i < _sortedPasses.size(); i++)
	{
		auto* pass = _sortedPasses[i];
		int passIndex = i;
		if (pass->ShouldExecute(state))
		{
			PassContext ctx;
			ctx.renderTargetFBO = _renderTargetFBO;
			ctx.passName = pass->GetName();

			for (const auto& input : pass->GetInputs()) {
				ctx.inputTextures.push_back(_resManager.GetTexture(input));
			}

			for (const auto& input : pass->GetInputOptions()) {
				ctx.optionInputTextures.push_back(_resManager.TryGetTexture(input));
			}

			for (const auto& output : pass->GetOutputs()) {
				ctx.outputTextures.push_back(_resManager.GetTexture(output));
			}

			for (const auto& temp : pass->GetTemps()) {
				ctx.tempTextures.push_back(_resManager.GetTexture(temp));
			}

			for (const auto& persitent : pass->GetPersistents()) {
				ctx.persitentTextures.push_back(_resManager.GetTexture(persitent));
			}

			for (const auto& external : pass->GetExternals()) {
				if (external.type == ResourceType::Texture)
					ctx.externalTextures.push_back(_resManager.GetExternalTexture(external.name));
			}

			pass->Execute(ctx, state);
		}

		for (auto& res : pass->GetLifeCycleResource())
		{
			auto it = _lifecycles.find(res);
			if (it == _lifecycles.end())
				return;

			auto& lifecycle = it->second;
			if (lifecycle.lastPass <= passIndex)
			{
				_resManager.ReleaseTexture(res);
				//std::cout << std::format("release res [{}]\n", res.name);
			}
		}
	}


	for (auto& pass : _sortedPasses)
		pass->FrameEnd(state);
}

// 清空
void RenderGraph::Clear() {
	_passes.clear();
	_ownedPasses.clear();
	_sortedPasses.clear();
	_lifecycles.clear();
	_needsCompile = true;
	_compiledVersion++;
}

PassNode* RenderGraph::GetPass(const std::string& name) const {
	for (auto* pass : _passes) {
		if (pass->GetName() == name) {
			return pass;
		}
	}
	return nullptr;
}

// 配置
void RenderGraph::SetPassEnabled(const std::string& name, bool enabled) {
	for (auto* pass : _passes) {
		if (pass->GetName() == name) {
			pass->SetEnable(enabled);
			return;
		}
	}
}

void RenderGraph::SetRenderTargetFBO(GLuint fbo)
{
	_renderTargetFBO = fbo;
}
