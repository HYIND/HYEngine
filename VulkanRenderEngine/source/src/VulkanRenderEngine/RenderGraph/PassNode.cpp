#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderGraph/PassNode.h"
#include "VulkanRenderEngine/VKContext.h"

thread_local std::string tls_PassName;

using namespace RenderGraph;

std::string PassNode::GetCurThreadPassName()
{
	return tls_PassName;
}

PassNode::PassNode(const std::string& name)
	: _name(name), _enable(true) {}

PassNode& PassNode::Input(const ResourceData& data) {
	_inputs.push_back(data);
	_lifeCycleResource.push_back(data.resource);
	return *this;
}

PassNode& PassNode::InputOption(const ResourceData& data) {
	_inputOptions.push_back(data);
	_lifeCycleResource.push_back(data.resource);
	return *this;
}

PassNode& PassNode::Output(const ResourceData& data) {
	_outputs.push_back(data);
	_lifeCycleResource.push_back(data.resource);
	return *this;
}

PassNode& PassNode::Temp(const ResourceData& data) {
	_temps.push_back(data);
	_lifeCycleResource.push_back(data.resource);
	return *this;
}

PassNode& PassNode::Persistent(const ResourceData& data) {
	_persistents.push_back(data);
	return *this;
}

PassNode& PassNode::External(const ExternalResourceData& resource)
{
	_externals.push_back(resource);
	return *this;
}

PassNode& PassNode::After(PassNode* node) {
	if (node != this)
		_afters.insert(node);
	return *this;
}

PassNode& PassNode::Before(PassNode* node)
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
#ifdef Enable_Vulkan_Validation
	tls_PassName = _name;
#endif
	if (!_render) return;
	_render->FrameBegin(registry, state);
#ifdef Enable_Vulkan_Validation
	tls_PassName.clear();
#endif
}

bool PassNode::ShouldExecute(FrameDataRegistry& registry, RenderState& state)
{
	return _enable && _render && _render->ShouldExecute(registry, state);
}

void PassNode::Execute(PassFrameCmdContext& cmdCtx, FrameDataRegistry& registry, const PassFrameContext& ctx, RenderState& state)
{
#ifdef Enable_Vulkan_Validation
	tls_PassName = _name;
#endif
	cmdCtx.Start();
	if (!_render || !_enable) return;
	_render->Execute(cmdCtx, registry, ctx, state);
	cmdCtx.End();
#ifdef Enable_Vulkan_Validation
	tls_PassName.clear();
#endif
}

void PassNode::FrameEnd(FrameDataRegistry& registry, RenderState& state)
{
#ifdef Enable_Vulkan_Validation
	tls_PassName = _name;
#endif
	if (!_render) return;
	_render->FrameEnd(registry, state);
#ifdef Enable_Vulkan_Validation
	tls_PassName.clear();
#endif
}

const std::string& PassNode::GetName() const { return _name; }

const std::vector<PassNode::ResourceData>& PassNode::GetInputs() const { return _inputs; }

const std::vector<PassNode::ResourceData>& PassNode::GetInputOptions() const { return _inputOptions; }

const std::vector<PassNode::ResourceData>& PassNode::GetOutputs() const { return _outputs; }

const std::vector<PassNode::ResourceData>& PassNode::GetTemps() const { return _temps; }

const std::vector<PassNode::ResourceData>& PassNode::GetPersistents() const { return _persistents; }

const std::vector<PassNode::ExternalResourceData>& PassNode::GetExternals() const { return _externals; }

const std::vector<RenderGraphResource>& PassNode::GetLifeCycleResource() const { return _lifeCycleResource; }

const std::unordered_set<PassNode*>& PassNode::GetAfters() const { return _afters; }

const std::unordered_set<PassNode*>& PassNode::GetBefores() const { return _befores; }

int PassNode::GetIndex() const { return _index; }

int PassNode::GetBatch() const { return _batch; }

bool PassNode::GetEnable() const { return _enable; }

void PassNode::SetIndex(int index) { _index = index; }

void PassNode::SetBatch(int batch) { _batch = batch; }

void PassNode::SetEnable(bool enabled) { _enable = enabled; }
