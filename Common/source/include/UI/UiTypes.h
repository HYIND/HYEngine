#pragma once

// UI 资产的纯数据定义：文档（结构）与样式（外观）两类，互不依赖渲染，也不依赖资产层。
// 放在 Common 里是因为 UIView / UIViewRuntime 组件要用；解析器（读 JSON）在 AssestRuntime。

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

	struct UiEvent
	{
		std::string command;
		int legacyId = 0;
	};

	struct UiNode
	{
		std::string type;      // panel / image / text / button / slider / textinput / toggle ...
		std::string name;
		std::string styleRef;  // 节点上写的 style 名（文档 styles 表里的键）
		UiRect rect;

		std::string text;
		std::string texture;

		std::optional<StyleFields> inlineStyle;  // 节点内联字段，优先级最高
		UiEvent onClick;
		UiEvent onChange;

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
	};
}
