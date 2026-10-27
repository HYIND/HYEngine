#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "CommonData/AssetGUIDDef.h"
#include "ECSCore/Entity.h"
#include "UI/UiTypes.h"

class World;

// UI 的"DOM"层：模块 / 宿主用它反向修改界面。
//
// 定位方式刻意简单——Doc 只记 {World*, Entity}，Element 只再记一个节点 id，
// 每次调用现查 UIViewRuntime::instance。因此：
//   - needReload 重建 instance 之后，旧的 Doc/Element 不会悬空，只是写的状态丢了；
//   - 但**不要跨帧保存**，每帧现取（FindDoc 的代价是一次实体遍历）。
//
// 线程约定：
//   - 渲染线程在画这层 UI 的整段时间里持有 UiInstanceState::mutex；
//   - 场景线程（模块 handler）每次读写只短暂持有同一把锁。
//   这把锁是 CriticalSectionLock（Windows 上是 CRITICAL_SECTION），同一线程可以重复
//   进入，所以每个方法都自己加锁、互相调用也不会自锁——用的时候不用关心"外面有没有
//   已经锁过"，随便调。
//
//   唯一还要留意的是"跨出临界区的引用"：Items()/Options()/Lines() 返回的指针指向实例
//   里的容器，交到你手上时锁已经放了，下一次 SetItems/SetOptions/AppendLine 就失效。
//   当次用，别跨帧留着。
//
// 语义约定：这里的所有写入都**不会**产生 UI 事件。事件只代表用户操作。
namespace UI
{
	class Element;

	// 一个界面实例的句柄（≈ document）。
	class Doc
	{
	public:
		Doc() = default;
		Doc(World* world, const Entity& entity) : m_world(world), m_entity(entity) {}

		bool Valid() const;
		explicit operator bool() const { return Valid(); }

		// 按节点名拿元素（约等于 getElementById）；名字不存在返回无效 Element。
		Element Get(const std::string& name);
		Element GetById(int nodeId);

		// 文档名（.ui 根节点的 name），拿不到返回空串。
		std::string Name() const;

		// 这个实例的运行期状态；文档没加载成功时是 nullptr。
		UiInstanceState* State() const;

		World* WorldPtr() const { return m_world; }
		const Entity& EntityRef() const { return m_entity; }

	private:
		World* m_world = nullptr;
		Entity m_entity{};
	};

	// 一个节点的句柄（≈ Element）。所有 setter 都是"写进实例状态"，不碰资产。
	class Element
	{
	public:
		Element() = default;
		Element(const Doc& doc, int nodeId) : m_doc(doc), m_nodeId(nodeId) {}

		bool Valid() const;
		int Id() const { return m_nodeId; }

		// ---- 读 ----
		bool Enabled() const;
		bool Visible() const;
		bool Toggled() const;
		float Value() const;
		bool Selected() const;
		std::string Text() const;                          // 只反映外部设置过的 label
		// textinput 的真实内容是编辑缓冲，不是 label：Text() 对它永远是空的。
		// 想知道用户此刻输入了什么（包括还没失焦的），读这个。
		std::string InputText() const;
		const std::vector<UiListItem>* Items() const;      // scrolllist
		const std::vector<UiOption>* Options() const;      // combo（只反映被覆盖过的）
		const std::deque<std::string>* Lines() const;      // textblock

		// ---- 写 ----
		void SetEnabled(bool value);
		void SetVisible(bool value);
		void SetToggled(bool value);
		void SetValue(float value);
		void SetSelected(bool value);                      // scrolllist 是否可点选
		void SetText(std::string value);                   // 覆盖文字（对 textinput 顺带清空/填入编辑缓冲）
		void SetInputText(std::string value);              // 只写 textinput 的编辑缓冲，超长截断
		void SetItems(std::vector<UiListItem> items);      // scrolllist 内容
		void SetOptions(std::vector<UiOption> options);    // combo 选项
		void AppendLine(std::string line, std::size_t maxLines = 500); // textblock 追加
		void ClearLines();                                  // textblock 清空

	private:
		// 两个 helper 自己也会加锁（递归锁），调它们不需要先持有 mutex。
		UiInstanceState* Instance() const;
		const UiNodeState* Find() const;     // 读、不插入；加锁

		Doc m_doc;
		int m_nodeId = 0;
	};

	// 按视图实例名（UIView::viewName）找界面。重名 / 找不到都返回无效 Doc 并填 error。
	Doc FindDoc(World& world, std::string_view viewName, std::string* error = nullptr);

	// 引用同一份文档的所有界面实例（批量操作用，不保证顺序）。
	std::vector<Doc> FindDocsByDocument(World& world, std::string_view documentName);

	// 运行期创建一个界面实例：建实体 + 挂 UIView，下一帧 UIViewSystem 会把文档载进来。
	Doc CreateView(World& world, const AssetGUID& documentGuid, std::string_view viewName, int layer = 0);
}
