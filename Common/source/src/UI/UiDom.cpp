#include "UI/UiDom.h"

#include "Components/UIView.h"
#include "ECSCore/World.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace
{
	std::string FileNameOf(const std::string& path)
	{
		const size_t slash = path.find_last_of("/\\");
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	// 往编辑缓冲里写：留一个 '\0'，超长的截掉，别让 ImGui 读到没有结尾的串。
	void WriteInputBuffer(UI::UiNodeState& state, const std::string& text)
	{
		const size_t limit = state.text.size() - 1;
		const size_t count = (std::min)(text.size(), limit);
		if (count > 0)
			std::memcpy(state.text.data(), text.data(), count);
		state.text[count] = '\0';
	}
}

namespace UI
{
	// ---------------- Doc ----------------

	UiInstanceState* Doc::State() const
	{
		if (!m_world)
			return nullptr;

		UIViewRuntime* runtime = m_world->tryGetComponent<UIViewRuntime>(m_entity);
		return (runtime && runtime->instance) ? runtime->instance.get() : nullptr;
	}

	bool Doc::Valid() const
	{
		return m_world != nullptr && State() != nullptr;
	}

	std::string Doc::Name() const
	{
		if (!m_world)
			return {};

		UIViewRuntime* runtime = m_world->tryGetComponent<UIViewRuntime>(m_entity);
		return (runtime && runtime->document) ? runtime->document->document.name : std::string();
	}

	bool Doc::Enabled() const
	{
		if (!m_world)
			return false;

		// 没有 UIView 组件的实体不算"启用的视图"，免得调用方以为它在画。
		const UIView* view = m_world->tryGetComponent<UIView>(m_entity);
		return view && view->enabled;
	}

	void Doc::SetEnabled(bool enabled)
	{
		if (!m_world)
			return;

		// 只改 enabled，不动 needReload：切屏不该让文档重新解析、更不该丢实例状态。
		UIView* view = m_world->tryGetComponent<UIView>(m_entity);
		if (view)
			view->enabled = enabled;
	}

	Element Doc::Get(const std::string& name)
	{
		if (!m_world)
			return {};

		UIViewRuntime* runtime = m_world->tryGetComponent<UIViewRuntime>(m_entity);
		if (!runtime || !runtime->document)
			return {};

		const int id = runtime->document->FindNodeId(name);
		return id != 0 ? Element(*this, id) : Element();
	}

	Element Doc::GetById(int nodeId)
	{
		return nodeId != 0 ? Element(*this, nodeId) : Element();
	}

	// ---------------- Element ----------------

	UiInstanceState* Element::Instance() const
	{
		return m_doc.State();
	}

	// helper 自己也加锁：CriticalSectionLock 是递归锁，公开方法外面已经锁过一层，
	// 同一线程再进来不会自锁。所以调它不需要先持有 mutex。
	// 返回的指针指向实例内部，交给调用方时锁已放掉——调用方要么还在自己的临界区里
	// （所有公开方法都是这么用的），要么就只当次看一眼，别存起来。
	const UiNodeState* Element::Find() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return nullptr;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const auto it = instance->nodes.find(m_nodeId);
		return it != instance->nodes.end() ? &it->second : nullptr;
	}

	bool Element::Valid() const
	{
		return m_nodeId != 0 && Instance() != nullptr;
	}

	// ---- 读 ----
	// 每个方法都自己加锁再调 Find()，多锁一层无所谓：递归。

	bool Element::Enabled() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return true;   // 界面还没载进来：按"可用"算，别让调用方因为时序问题误判

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return state ? state->enabled : true;
	}

	bool Element::Visible() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return true;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return state ? state->visible : true;
	}

	bool Element::Toggled() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return false;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return state ? state->toggled : false;
	}

	float Element::Value() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return 0.0f;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return state ? state->value : 0.0f;
	}

	bool Element::Selected() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return false;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return state ? state->selected : false;
	}

	std::string Element::Text() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return {};

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return (state && state->hasLabel) ? state->label : std::string();   // 拷出来，别把引用带出临界区
	}

	std::string Element::InputText() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return {};

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return state ? std::string(state->text.data()) : std::string();
	}

	const std::vector<UiListItem>* Element::Items() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return nullptr;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const auto it = instance->lists.find(m_nodeId);
		return it != instance->lists.end() ? &it->second : nullptr;
	}

	const std::vector<UiOption>* Element::Options() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return nullptr;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const UiNodeState* state = Find();
		return (state && state->hasOptions) ? &state->options : nullptr;
	}

	const std::deque<std::string>* Element::Lines() const
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return nullptr;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		const auto it = instance->texts.find(m_nodeId);
		return it != instance->texts.end() ? &it->second : nullptr;
	}

	// ---- 写 ----
	// Get() 在节点没有状态时会插一个默认值，正合写入口的语义。

	void Element::SetEnabled(bool value)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		instance->Get(m_nodeId).enabled = value;
	}

	void Element::SetVisible(bool value)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		instance->Get(m_nodeId).visible = value;
	}

	void Element::SetToggled(bool value)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		instance->Get(m_nodeId).toggled = value;
	}

	void Element::SetValue(float value)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		instance->Get(m_nodeId).value = value;
	}

	void Element::SetSelected(bool value)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		instance->Get(m_nodeId).selected = value;
	}

	void Element::SetText(std::string value)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		UiNodeState& state = instance->Get(m_nodeId);
		// label 只影响"画出来的文字"，编辑缓冲只影响 textinput——两边都写一遍，
		// 调用方就不用先知道这个节点是什么类型了。
		WriteInputBuffer(state, value);
		state.label = std::move(value);
		state.hasLabel = true;   // 只有真设置过才覆盖资产文字：写空串就是"显示空的"
	}

	void Element::SetInputText(std::string value)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		WriteInputBuffer(instance->Get(m_nodeId), value);
		// 不碰 label：textinput 显示的是缓冲，改 label 反而会让别的读取方以为设过文字。
	}

	void Element::SetItems(std::vector<UiListItem> items)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		instance->lists[m_nodeId] = std::move(items);
	}

	void Element::SetOptions(std::vector<UiOption> options)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		UiNodeState& state = instance->Get(m_nodeId);
		state.options = std::move(options);
		state.hasOptions = true;
	}

	void Element::AppendLine(std::string line, std::size_t maxLines)
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		std::deque<std::string>& lines = instance->texts[m_nodeId];
		lines.push_back(std::move(line));

		// 聊天这类会一直涨，留一个上限，超了从最旧的开始丢。
		if (maxLines > 0)
		{
			while (lines.size() > maxLines)
				lines.pop_front();
		}
	}

	void Element::ClearLines()
	{
		UiInstanceState* instance = Instance();
		if (!instance)
			return;

		std::lock_guard<CriticalSectionLock> guard(instance->mutex);
		instance->texts[m_nodeId].clear();
	}

	// ---------------- 查找 / 创建 ----------------

	Doc FindDoc(World& world, std::string_view viewName, std::string* error)
	{
		if (viewName.empty())
		{
			if (error)
				*error = "viewName 为空";
			return {};
		}

		Doc found;
		int count = 0;
		for (const Entity& entity : world.getEntitiesWith<UIView>())
		{
			UIView* view = world.tryGetComponent<UIView>(entity);
			if (!view || view->viewName != viewName)
				continue;

			++count;
			if (count == 1)
				found = Doc(&world, entity);
		}

		if (count == 0)
		{
			if (error)
				*error = "找不到视图：" + std::string(viewName);
			return {};
		}

		if (count > 1)
		{
			// 重名是配置错误：不猜第一个，让问题暴露出来。
			if (error)
				*error = "视图名重复：" + std::string(viewName) + "（" + std::to_string(count) + " 个实体）";
			return {};
		}

		return found;
	}

	std::vector<Doc> FindDocsByDocument(World& world, std::string_view documentName)
	{
		std::vector<Doc> result;
		if (documentName.empty())
			return result;

		for (const Entity& entity : world.getEntitiesWith<UIView, UIViewRuntime>())
		{
			UIViewRuntime* runtime = world.tryGetComponent<UIViewRuntime>(entity);
			if (!runtime || !runtime->document || runtime->faulted)
				continue;

			const UiDocument& document = runtime->document->document;
			if (document.name == documentName || FileNameOf(document.sourcePath) == documentName ||
				FileNameOf(document.sourcePath) == std::string(documentName) + ".ui")
				result.emplace_back(&world, entity);
		}

		return result;
	}

	Doc CreateView(World& world, const AssetGUID& documentGuid, std::string_view viewName, int layer)
	{
		Entity entity = world.createEntity();

		UIView view;
		view.documentGuid = documentGuid;
		view.viewName = std::string(viewName);
		view.layer = layer;
		view.needReload = true;   // 下一帧 UIViewSystem 把文档载进来

		world.addComponent<UIView>(entity, std::move(view));
		return Doc(&world, entity);
	}
}
