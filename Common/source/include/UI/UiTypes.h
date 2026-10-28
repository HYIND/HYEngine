#pragma once

// UI 资产的纯数据定义：文档（结构）与样式（外观）两类，互不依赖渲染，也不依赖资产层。
// 放在 Common 里是因为 UIView / UIViewRuntime 组件要用；解析器（读 JSON）在 AssestRuntime。

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CriticalSectionLock.h"

namespace UI
{
	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	struct Vec4
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;
	};

	struct Padding
	{
		float left = 0.0f;
		float right = 0.0f;
		float top = 0.0f;
		float bottom = 0.0f;
	};

	// 九宫格拉伸，单位与文档虚拟坐标一致。
	struct Slice
	{
		float left = 0.0f;
		float right = 0.0f;
		float top = 0.0f;
		float bottom = 0.0f;
	};

	// 样式里能被"覆盖"的一组外观字段。
	// 用 optional 表示"这一层没有声明该字段"，合并时才能正确回退到上一层。
	struct StyleFields
	{
		// 背景
		std::optional<std::string> texture;
		std::optional<Vec4> tint;
		std::optional<Slice> slice;
		std::optional<float> cornerRadius;
		std::optional<std::string> fit;

		// 文本
		std::optional<std::string> fontFamily;
		std::optional<float> fontSize;
		std::optional<bool> bold;
		std::optional<Vec4> color;
		std::optional<std::string> align;
		std::optional<std::string> valign;
		std::optional<Padding> padding;

		// 通用
		std::optional<float> opacity;
	};

	struct UiStyle
	{
		std::string name;        // 样式文件里声明的名字，只用于报错/调试
		std::string extendsRef;  // 原始 extends 引用（未解析）
		std::string sourcePath;  // 解析后落到的路径
		StyleFields base;
		std::map<std::string, StyleFields> states;
	};

	struct UiRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;
	};

	// UI 上发生的一次交互。渲染层产出，进总线时转成 UI::Message 再送到订阅者。
	// command 是路由键（.ui 里 onClick/onChange 的 command 字段），其余都是这次交互的载荷。
	struct UiEvent
	{
		std::string command;
		int legacyId = 0;
		// 产生这条事件的节点名（诊断用：日志和"没人认领"的报错都靠它定位）。
		std::string node;
		float value = 0.0f;   // 滑块/开关这类带数值的控件回传值
		std::string itemId;   // 列表行被点中时回传该行的业务标识
		// textinput 提交时的文本内容，其余控件为空。
		std::string text;
	};

	// 文本节点绑定到另一个节点的运行时值（例如滑动条右边的百分比）。
	struct UiNodeBinding
	{
		std::string node;    // 目标节点名
		std::string format;  // printf 风格格式串，缺省 "%.0f"
		int targetId = 0;    // 解析期回填的目标节点 id
	};

	// combo 的一个选项。
	struct UiOption
	{
		std::string text;
		int value = 0;
	};

	struct UiNode
	{
		int id = 0;            // 解析时按遍历顺序分配，同一份文档内稳定，供运行时按节点存状态
		std::string type;      // panel / image / text / button / slider / textinput / toggle ...
		std::string name;
		std::string styleRef;  // 节点上写的 style 名（文档 styles 表里的键）
		UiRect rect;

		std::string text;
		std::string texture;

		// 带数值的控件（slider / toggle / progress / scrolllist）
		float minValue = 0.0f;
		float maxValue = 100.0f;
		float value = 0.0f;
		float step = 1.0f;

		// 文本输入
		std::string placeholder;
		int maxLength = 0;

		// progress 是否在中间显示百分比
		bool showPercent = false;

		// scrolllist 的行高（虚拟坐标；<=0 用默认值）
		float rowHeight = 0.0f;

		// scrolllist 的行能不能点：资产里不写就默认可点（单击选中/双击进入），
		// 纯展示的列表写 "selectable": false，行就只画不高亮、不发事件。
		bool selectable = true;

		// combo 的选项（选中值存 UiNodeState::value，即选项下标）
		std::vector<UiOption> options;

		std::optional<StyleFields> inlineStyle;  // 节点内联字段，优先级最高
		std::map<std::string, StyleFields> inlineStates;  // 节点内联的 hover/pressed 等状态覆盖
		UiEvent onClick;
		UiEvent onChange;

		// 该节点的文本/数值来自另一个节点（可选）
		std::optional<UiNodeBinding> bind;

		std::vector<UiNode> children;
	};

	struct UiDocument
	{
		std::string name;
		double space = 1000.0;              // 虚拟坐标范围：两轴都是 [0, space]
		std::string aspectMode = "stretch"; // stretch / fit
		int layer = 0;

		// 别名 -> 引用字符串（相对路径或工程内路径），真正解析在 Resolve 阶段。
		std::map<std::string, std::string> styleMap;

		UiNode root;
		std::string sourcePath;
	};

	struct UiListItem
	{
		std::string text;
		std::string id;
		bool disabled = false;
	};

	struct UiNodeState
	{
		float value = 0.0f;
		bool toggled = false;

		// textinput 的编辑缓冲（UTF-8），每个实例各一份。
		std::array<char, 256> text{};

		// ---- 外部（模块 / 宿主）可写的展示状态 ----
		// 只有这些字段被改过才生效，没改过就是资产里声明的样子。

		bool enabled = true;   // false = 不参与命中，画成半透明
		bool visible = true;   // false = 整棵子树都不画

		bool hasLabel = false; // 只有真设置过才覆盖资产文本，否则写个空串会把文字抹掉
		std::string label;     // 覆盖 text / button / toggle 的文字

		bool hasOptions = false;
		std::vector<UiOption> options; // 覆盖 combo 的选项表

		bool selected = true;
	};

	struct UiInstanceState
	{
		std::unordered_map<int, UiNodeState> nodes;

		// 运行时塞进来的行数据：键是节点 id。
		std::unordered_map<int, std::vector<UiListItem>> lists;   // scrolllist
		std::unordered_map<int, std::deque<std::string>> texts;   // textblock

		mutable CriticalSectionLock mutex;

		UiNodeState& Get(int nodeId) { return nodes[nodeId]; }
	};

	void SeedInstanceState(const UiDocument& document, UiInstanceState& instance);

	// 解析一个节点后算出来的最终外观：基础字段 + 各状态覆盖。
	struct ResolvedNodeStyle
	{
		StyleFields effective;                          // 基础态
		std::map<std::string, StyleFields> states;      // 状态覆盖（未与基础态合并）
		std::string sourceDescription;                  // 例如 "inline > button -> button_primary.uistyle"
	};

	inline void MergeFields(StyleFields &target, const StyleFields &overlay)
	{
		if (overlay.texture) target.texture = overlay.texture;
		if (overlay.tint) target.tint = overlay.tint;
		if (overlay.slice) target.slice = overlay.slice;
		if (overlay.cornerRadius) target.cornerRadius = overlay.cornerRadius;
		if (overlay.fit) target.fit = overlay.fit;
		if (overlay.fontFamily) target.fontFamily = overlay.fontFamily;
		if (overlay.fontSize) target.fontSize = overlay.fontSize;
		if (overlay.bold) target.bold = overlay.bold;
		if (overlay.color) target.color = overlay.color;
		if (overlay.align) target.align = overlay.align;
		if (overlay.valign) target.valign = overlay.valign;
		if (overlay.padding) target.padding = overlay.padding;
		if (overlay.opacity) target.opacity = overlay.opacity;
	}

	// 一次解析的完整结果：文档 + 样式表 + 每节点有效样式。
	struct ParseResult
	{
		bool ok = false;
		std::string error;

		UiDocument document;

		// 文档 styles 表解析后的结果：键 = 文档里用的别名，值 = 展开完 extends 的样式。
		std::map<std::string, std::shared_ptr<UiStyle>> styles;

		// 每个节点最终算出来的外观（供调试打印）。
		std::map<const UiNode*, ResolvedNodeStyle> resolved;

		// 节点名 -> 节点 id（重名时取第一个），宿主按名字塞列表/文本行时用。
		std::unordered_map<std::string, int> nodeIds;

		int FindNodeId(const std::string& name) const
		{
			auto it = nodeIds.find(name);
			return it != nodeIds.end() ? it->second : 0;
		}
	};
}
