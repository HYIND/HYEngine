#pragma once

// .ui / .uistyle 的加载与解析。

#include "UI/UiTypes.h"

#include <functional>
#include <string>

namespace UI
{
	using PathExistsFn = std::function<bool(const std::string &path)>;

	struct ResolveContext
	{
		std::string projectRoot;  // 工程基路径，可为空（为空时跳过第二次尝试）
		PathExistsFn exists;      // 必填
	};

	// 读文件 + 解析 + 解析样式引用 + 展开继承 + 计算每个节点的有效样式。
	ParseResult ParseDocumentFile(const std::string &filePath, const ResolveContext &context);

	// 单独加载一份 .uistyle（展开它的 extends 链）。
	bool ParseStyleFile(const std::string &filePath, const ResolveContext &context,
		UiStyle &outStyle, std::string &error);

	// 只读文档的 styles 表，返回它引用的样式文件（原始字符串，未解析路径）。
	// 导入时用它把依赖的 .uistyle 一起带进工程。
	std::vector<std::string> CollectStyleReferences(const std::string &filePath);

	// 把解析结果打印成表格文本，便于人工核对（不接渲染）。
	std::string DumpResolved(const ParseResult &result);
}
