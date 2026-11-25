#pragma once

#include <filesystem>

class ProjectManager;

// 编辑器内置的文本编辑器：.ui / .uistyle / .lua（基于 ImGuiColorTextEdit）
//   - 一个文件一个窗口；Ctrl+S 保存，标题带 * 表示有未保存的修改
//   - 语法高亮、行号、括号匹配、撤销/重做（ImGuiColorTextEdit 自带）
//   - Ctrl+F 查找替换，Ctrl+G / F3 找下一个（ImGuiColorTextEdit 自带）
//   - 语法检查（停止输入 0.4 秒后自动跑），出错的行在行号和正文上标红，悬停显示错误：
//       .ui / .uistyle：JSON 语法（允许注释）；打开和保存后再完整走一遍 UI 解析，报出样式引用等错误
//       .lua：只编译、不运行，报出语法错误
//   - 文件在外部被改：本地没改就自动重新加载，本地改过就提示二选一
//   - 换行和 BOM 按原文件保留；不是 UTF-8 的文件只读打开，免得把内容弄坏
// 这里只负责写盘，热重载交给 ProjectManager 现有的改动检测（CheckUIDocumentChanges / CheckScriptAssetChanges）
namespace TextAssetEditor
{
	// 扩展名是否由内置编辑器处理（.ui / .uistyle / .lua，不区分大小写）
	bool CanOpen(const std::filesystem::path& fullPath);

	// 打开文件；已经打开就把窗口提到前面
	void Open(const std::filesystem::path& fullPath);

	// 每帧调用：画所有打开着的编辑窗口
	void DrawWindows(ProjectManager* projectManager);

	// 是否还有未保存的文件（退出编辑器前可以用来提示）
	bool HasUnsavedChanges();
}
