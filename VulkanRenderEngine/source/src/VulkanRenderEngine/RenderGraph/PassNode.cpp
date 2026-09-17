#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/PassNode.h"

using namespace RenderGraph;

PassNode::PassNode(const std::string& name)
	: _name(name), _enable(true) {}

PassNode& PassNode::Input(const RenderGraphResource& resource) {
	_inputs.push_back(resource);
	_lifeCycleResource.push_back(resource);
	return *this;
}

PassNode& PassNode::InputOption(const RenderGraphResource& resource) {
	_inputOptions.push_back(resource);
	_lifeCycleResource.push_back(resource);
	return *this;
}

PassNode& PassNode::Output(const RenderGraphResource& resource) {
	_outputs.push_back(resource);
	_lifeCycleResource.push_back(resource);
	return *this;
}

PassNode& PassNode::Temp(const RenderGraphResource& resource) {
	_temps.push_back(resource);
	_lifeCycleResource.push_back(resource);
	return *this;
}

PassNode& PassNode::Persistent(const RenderGraphResource& resource) {
	_persistents.push_back(resource);
	return *this;
}

PassNode& RenderGraph::PassNode::External(const ExternalResource& resource)
{
	_externals.push_back(resource);
	return *this;
}

PassNode& PassNode::After(PassNode* node) {
	if (node != this)
		_afters.insert(node);
	return *this;
}

PassNode& RenderGraph::PassNode::Before(PassNode* node)
{
	if (node != this)
		_befores.insert(node);
	return *this;
}

PassNode& PassNode::SetRenderPass(std::unique_ptr<RenderPassBase>&& pass)
{
	_render = std::move(pass);
	return *this;
}
void PassNode::FrameBegin(FrameDataRegistry& registry, RenderState& state)
{
	if (!_render) return;
	_render->FrameBegin(registry, state);
}

bool PassNode::ShouldExecute(FrameDataRegistry& registry, RenderState& state) 
{
	return _enable && _render && _render->ShouldExecute(registry, state);
}

void PassNode::Execute(FrameDataRegistry& registry, const PassFrameContext& ctx, RenderState& state)
{
	if (!_render || !_enable) return;
	_render->Execute(registry, ctx, state);
}

void PassNode::FrameEnd(FrameDataRegistry& registry, RenderState& state)
{
	if (!_render) return;
	_render->FrameEnd(registry, state);
}

const std::string& PassNode::GetName() const { return _name; }

const std::vector<RenderGraphResource>& PassNode::GetInputs() const { return _inputs; }

const std::vector<RenderGraphResource>& PassNode::GetInputOptions() const { return _inputOptions; }

const std::vector<RenderGraphResource>& PassNode::GetOutputs() const { return _outputs; }

const std::vector<RenderGraphResource>& PassNode::GetTemps() const { return _temps; }

const std::vector<RenderGraphResource>& PassNode::GetPersistents() const { return _persistents; }

const std::vector<ExternalResource>& RenderGraph::PassNode::GetExternals() const { return _externals; }

const std::vector<RenderGraphResource>& RenderGraph::PassNode::GetLifeCycleResource() const { return _lifeCycleResource; }

const std::unordered_set<PassNode*>& PassNode::GetAfters() const { return _afters; }

const std::unordered_set<PassNode*>& PassNode::GetBefores() const { return _befores; }

int PassNode::GetIndex() const { return _index; }

int PassNode::GetBatch() const { return _batch; }

bool PassNode::GetEnable() const { return _enable; }

void PassNode::SetIndex(int index) { _index = index; }

void PassNode::SetBatch(int batch) { _batch = batch; }

void PassNode::SetEnable(bool enabled) { _enable = enabled; }
