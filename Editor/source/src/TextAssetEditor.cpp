#include "stdafx.h"
#include "TextAssetEditor.h"

#include "ImGuiColorTextEdit/TextEditor.h"
#include "Project/ProjectManager.h"
#include "UI/UiDocumentParser.h"
#include "nlohmann/json.hpp"
#include "lua.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	constexpr size_t kMaxEditableBytes = 8u * 1024u * 1024u;	// 再大就只读打开
	constexpr double kCheckDelay = 0.4;							// 停止输入多久后做语法检查（秒）
	constexpr double kDiskPollInterval = 1.0;					// 多久看一次磁盘上的修改时间（秒）

	const ImVec4 kColorOk(0.45f, 0.80f, 0.45f, 1.0f);
	const ImVec4 kColorError(0.95f, 0.40f, 0.40f, 1.0f);
	const ImVec4 kColorWarn(0.95f, 0.75f, 0.30f, 1.0f);
	const ImU32 kMarkerLineNumber = IM_COL32(200, 60, 60, 200);
	const ImU32 kMarkerText = IM_COL32(220, 70, 70, 60);
	const ImU32 kSquiggle = IM_COL32(255, 90, 90, 255);
	constexpr size_t kSquiggleType = 1;

	enum class TextLang { Json, Lua };	// .ui / .uistyle 都是 JSON

	struct TextDoc
	{
		fs::path path;
		std::wstring key;				// 规范化后的路径（小写），判断是否已打开
		std::string title;				// UTF-8 文件名
		std::string windowId;			// 窗口 ID 后缀，标题变了窗口不变
		TextLang lang = TextLang::Json;
		bool isStyle = false;			// .uistyle

		TextEditor editor;

		// 编辑器里换行统一是 \n，保存时按原文件还原
		std::string savedText;			// 上次保存/加载时编辑器里的内容
		bool hadBom = false;
		bool crlf = false;
		bool readOnly = false;
		std::string readOnlyReason;

		uint64_t version = 1;			// 内容每变一次加一
		uint64_t dirtyVersion = 0;
		bool dirty = false;

		// 磁盘
		fs::file_time_type diskTime{};
		fs::file_time_type pendingDiskTime{};
		double nextDiskPoll = 0.0;
		bool externalChanged = false;	// 本地有改动时外部也改了，等用户选
		bool missingOnDisk = false;

		// 窗口
		bool open = true;
		bool focusRequest = true;
		bool confirmClose = false;
		bool confirmSaveWithError = false;

		// 检查
		uint64_t checkedVersion = 0;
		double lastEditTime = 0.0;
		std::string syntaxError;		// 空 = 通过
		int errorLine = -1;				// 0 基
		int errorColumn = -1;			// 0 基，按字符（码点）算；-1 = 只知道行
		bool needUiCheck = true;
		bool uiCheckOk = true;
		std::string uiCheckMessage;
		std::string status;
	};

	std::vector<std::unique_ptr<TextDoc>> s_docs;

	// ---------------- 字号 ----------------
	// 所有编辑窗口共用一个缩放比例（相对编辑器界面字号）。
	// 存在 imgui.ini 的 [TextAssetEditor][Settings] 里，跟窗口布局一起由 ImGui 自动保存

	constexpr float kMinFontScale = 0.5f;
	constexpr float kMaxFontScale = 3.0f;
	constexpr float kFontScaleStep = 0.1f;
	float s_fontScale = 1.0f;
	constexpr const char* kSettingsType = "TextAssetEditor";

	float EditorFontSize()
	{
		return ImGui::GetStyle().FontSizeBase * s_fontScale;
	}

	float ClampFontScale(float scale)
	{
		return std::clamp(std::round(scale * 100.0f) / 100.0f, kMinFontScale, kMaxFontScale);
	}

	void SetFontScale(float scale)
	{
		scale = ClampFontScale(scale);
		if (scale == s_fontScale)
			return;
		s_fontScale = scale;
		ImGui::MarkIniSettingsDirty();	// ImGui 按 io.IniSavingRate 写回 imgui.ini

		// 字号变了以后行高跟着变，按"第一行可见行"重新对齐，免得视图跳走
		for (auto& doc : s_docs)
		{
			const size_t firstRow = doc->editor.GetFirstVisibleRow();
			doc->editor.ScrollToLine(firstRow, TextEditor::Scroll::alignTop);
		}
	}

	void ReadSettingsLine(const char* line)
	{
		float value = 0.0f;
		if (std::sscanf(line, "FontScale=%f", &value) == 1)
			s_fontScale = ClampFontScale(value);
	}

	// imgui.ini 在第一帧 NewFrame 时就读过了，那时这个处理器还没注册，ImGui 会跳过我们的段落。
	// 所以注册之后自己把文件里这一段读一遍；之后的保存由 ImGui 调 WriteAllFn
	void ReadSettingsFromIniFile()
	{
		const char* iniFile = ImGui::GetIO().IniFilename;
		if (!iniFile || !iniFile[0])
			return;

		std::ifstream ifs(iniFile, std::ios::binary);
		if (!ifs)
			return;

		const std::string header = std::string("[") + kSettingsType + "][Settings]";
		bool inSection = false;
		std::string line;
		while (std::getline(ifs, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (!line.empty() && line.front() == '[')
			{
				inSection = (line == header);
				continue;
			}
			if (inSection)
				ReadSettingsLine(line.c_str());
		}
	}

	// 第一次画编辑窗口前注册；ImGui 上下文重建过（比如重新初始化界面）就再注册一次
	void EnsureSettingsHandler()
	{
		static ImGuiContext* registeredFor = nullptr;
		ImGuiContext* context = ImGui::GetCurrentContext();
		if (!context || context == registeredFor)
			return;
		registeredFor = context;
		if (ImGui::FindSettingsHandler(kSettingsType))
			return;

		ImGuiSettingsHandler handler;
		handler.TypeName = kSettingsType;
		handler.TypeHash = ImHashStr(kSettingsType);
		handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void*
			{
				// 返回非空才会收到这一段的每一行
				return std::strcmp(name, "Settings") == 0 ? reinterpret_cast<void*>(1) : nullptr;
			};
		handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
			{
				ReadSettingsLine(line);
			};
		handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* out)
			{
				out->appendf("[%s][Settings]\n", self->TypeName);
				out->appendf("FontScale=%.2f\n", s_fontScale);
				out->append("\n");
			};
		ImGui::AddSettingsHandler(&handler);

		if (context->SettingsLoaded)
			ReadSettingsFromIniFile();
	}

	// ---------------- 小工具 ----------------

	std::string ToUtf8(const fs::path& path)
	{
		const std::u8string u8 = path.u8string();
		return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
	}

	std::string ToLowerAscii(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	std::wstring MakeKey(const fs::path& path)
	{
		std::error_code ec;
		fs::path absolutePath = fs::absolute(path, ec);
		if (ec)
			absolutePath = path;
		std::wstring key = absolutePath.lexically_normal().wstring();
		std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return key;
	}

	bool IsValidUtf8(const std::string& s)
	{
		const auto* p = reinterpret_cast<const unsigned char*>(s.data());
		const size_t n = s.size();
		size_t i = 0;
		while (i < n)
		{
			const unsigned char c = p[i];
			if (c < 0x80) { ++i; continue; }

			int length = 0;
			uint32_t cp = 0;
			if ((c & 0xE0) == 0xC0) { length = 2; cp = c & 0x1F; }
			else if ((c & 0xF0) == 0xE0) { length = 3; cp = c & 0x0F; }
			else if ((c & 0xF8) == 0xF0) { length = 4; cp = c & 0x07; }
			else return false;

			if (i + length > n)
				return false;
			for (int k = 1; k < length; ++k)
			{
				if ((p[i + k] & 0xC0) != 0x80)
					return false;
				cp = (cp << 6) | (p[i + k] & 0x3F);
			}
			if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) || (length == 4 && (cp < 0x10000 || cp > 0x10FFFF)))
				return false;	// 过长编码
			if (cp >= 0xD800 && cp <= 0xDFFF)
				return false;	// 代理区
			i += length;
		}
		return true;
	}

	// UTF-8 字符数（列号用）
	int CountCodepoints(const char* begin, const char* end)
	{
		int count = 0;
		for (const char* p = begin; p < end; ++p)
		{
			if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80)
				++count;
		}
		return count;
	}

	bool IsDirty(TextDoc& doc)
	{
		if (doc.dirtyVersion != doc.version)
		{
			doc.dirty = !doc.readOnly && (doc.editor.GetText() != doc.savedText);
			doc.dirtyVersion = doc.version;
		}
		return doc.dirty;
	}

	// 等宽字体：Consolas，中文并进微软雅黑。第一次打开编辑器时才加进字体图集（后端支持动态字体图集）
	ImFont* EditorFont()
	{
		static bool tried = false;
		static ImFont* font = nullptr;
		if (tried)
			return font;
		tried = true;

		ImGuiIO& io = ImGui::GetIO();
		if (!(io.BackendFlags & ImGuiBackendFlags_RendererHasTextures))
			return nullptr;	// 字体图集是锁死的，不能中途加字体：就用默认字体

		const char* monoPath = "C:/Windows/Fonts/consola.ttf";
		const char* cjkPath = "C:/Windows/Fonts/msyh.ttc";
		std::error_code ec;
		if (!fs::exists(monoPath, ec))
			return nullptr;

		const float size = (io.FontDefault && io.FontDefault->LegacySize > 0.0f) ? io.FontDefault->LegacySize : 18.0f;

		ImFontConfig monoConfig;
		monoConfig.OversampleH = 2;
		monoConfig.OversampleV = 2;
		monoConfig.PixelSnapH = true;
		font = io.Fonts->AddFontFromFileTTF(monoPath, size, &monoConfig);
		if (font && fs::exists(cjkPath, ec))
		{
			ImFontConfig cjkConfig;
			cjkConfig.MergeMode = true;
			cjkConfig.OversampleH = 2;
			cjkConfig.OversampleV = 2;
			cjkConfig.PixelSnapH = true;
			io.Fonts->AddFontFromFileTTF(cjkPath, size, &cjkConfig);
		}
		return font;
	}

	// .ui / .uistyle 的解析器允许注释，高亮也按带注释的 JSON 来
	const TextEditor::Language* JsonWithComments()
	{
		static const TextEditor::Language language = []
			{
				TextEditor::Language result = *TextEditor::Language::Json();
				result.name = "JSON";
				result.singleLineComment = "//";
				result.commentStart = "/*";
				result.commentEnd = "*/";
				return result;
			}();
		return &language;
	}

	// ---------------- 读写 ----------------

	bool ReadFileBytes(const fs::path& path, std::string& out)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs)
			return false;
		out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
		return !ifs.bad();
	}

	// 去 BOM、CRLF -> LF，判断能不能编辑
	std::string Decode(TextDoc& doc, std::string raw)
	{
		doc.hadBom = raw.size() >= 3
			&& static_cast<unsigned char>(raw[0]) == 0xEF
			&& static_cast<unsigned char>(raw[1]) == 0xBB
			&& static_cast<unsigned char>(raw[2]) == 0xBF;
		if (doc.hadBom)
			raw.erase(0, 3);

		doc.crlf = raw.find("\r\n") != std::string::npos;

		std::string text;
		text.reserve(raw.size());
		for (size_t i = 0; i < raw.size(); ++i)
		{
			if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
				continue;	// CRLF -> LF
			text.push_back(raw[i]);
		}

		doc.readOnly = false;
		doc.readOnlyReason.clear();
		if (text.find('\0') != std::string::npos)
		{
			doc.readOnly = true;
			doc.readOnlyReason = "文件里有 \\0 字符（可能不是文本文件），只读打开。";
		}
		else if (!IsValidUtf8(text))
		{
			doc.readOnly = true;
			doc.readOnlyReason = "文件不是 UTF-8 编码（可能是 GBK），为了不弄坏内容，只读打开。可以先用 VS Code 转成 UTF-8 再编辑。";
		}
		else if (text.size() > kMaxEditableBytes)
		{
			doc.readOnly = true;
			doc.readOnlyReason = "文件超过 8 MB，内置编辑器只读打开。";
		}
		return text;
	}

	std::string EncodeForDisk(const TextDoc& doc, const std::string& text)
	{
		std::string out;
		if (doc.hadBom)
			out = "\xEF\xBB\xBF";
		if (!doc.crlf)
		{
			out += text;
			return out;
		}
		out.reserve(out.size() + text.size() + text.size() / 16 + 1);
		for (char c : text)
		{
			if (c == '\n')
				out += "\r\n";
			else
				out.push_back(c);
		}
		return out;
	}

	bool WriteFileBytes(const fs::path& path, const std::string& bytes, std::string& error)
	{
		std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
		if (!ofs)
		{
			error = "文件打不开（可能被占用或只读）";
			return false;
		}
		ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		ofs.close();
		if (!ofs)
		{
			error = "写入失败";
			return false;
		}
		return true;
	}

	// ---------------- 检查 ----------------

	void CheckJson(TextDoc& doc, const std::string& text)
	{
		try
		{
			(void)nlohmann::json::parse(text, nullptr, true, true);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			// e.byte：出错字符的位置（从 1 开始）；读到末尾才出错时会比长度大
			const size_t byte = std::min(e.byte > 0 ? e.byte - 1 : 0, text.size());
			const size_t lineStart = (byte == 0) ? 0 : [&] { const size_t p = text.rfind('\n', byte - 1); return p == std::string::npos ? 0 : p + 1; }();
			doc.errorLine = static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(lineStart), '\n'));
			doc.errorColumn = CountCodepoints(text.data() + lineStart, text.data() + byte);

			std::string message = e.what();
			if (message.starts_with("[json.exception"))
			{
				if (const size_t p = message.find("] "); p != std::string::npos)
					message = message.substr(p + 2);
			}
			doc.syntaxError = "第 " + std::to_string(doc.errorLine + 1) + " 行：" + message;
		}
		catch (const std::exception& e)
		{
			doc.syntaxError = std::string("JSON 检查出错：") + e.what();
		}
	}

	void CheckLua(TextDoc& doc, const std::string& text)
	{
		lua_State* L = luaL_newstate();
		if (!L)
		{
			doc.syntaxError = "Lua 状态机创建失败，没法检查";
			return;
		}

		// 只编译、不运行；"t" = 只接受源码，不接受字节码
		constexpr const char* kChunkName = "=lua";
		const int result = luaL_loadbufferx(L, text.data(), text.size(), kChunkName, "t");
		if (result != LUA_OK)
		{
			const char* raw = lua_tostring(L, -1);
			std::string message = raw ? raw : "未知错误";

			// 格式："lua:<行>: <信息>"
			const std::string prefix = "lua:";
			if (message.starts_with(prefix))
			{
				size_t q = prefix.size();
				int line = 0;
				while (q < message.size() && std::isdigit(static_cast<unsigned char>(message[q])))
					line = line * 10 + (message[q++] - '0');
				if (q > prefix.size() && q < message.size() && message[q] == ':')
				{
					doc.errorLine = std::max(0, line - 1);
					message = message.substr(q + 1);
					if (!message.empty() && message.front() == ' ')
						message.erase(0, 1);
				}
			}
			doc.syntaxError = (doc.errorLine >= 0 ? "第 " + std::to_string(doc.errorLine + 1) + " 行：" : std::string()) + message;
		}
		lua_close(L);
	}

	// 把检查结果画到编辑器里：行号和整行标红，JSON 还能在出错的字符下面画波浪线
	void ApplyErrorMarkers(TextDoc& doc)
	{
		doc.editor.ClearMarkers();
		doc.editor.ClearSquiggles();
		if (doc.syntaxError.empty() || doc.errorLine < 0)
			return;

		const size_t line = static_cast<size_t>(doc.errorLine);
		doc.editor.AddMarker(line, kMarkerLineNumber, kMarkerText, doc.syntaxError, doc.syntaxError);
		if (doc.errorColumn >= 0)
		{
			const size_t column = static_cast<size_t>(doc.errorColumn);
			doc.editor.AddSquiggle(TextEditor::DocPos(line, column), TextEditor::DocPos(line, column + 1), kSquiggleType, kSquiggle, doc.syntaxError);
		}
	}

	void RunSyntaxCheck(TextDoc& doc)
	{
		doc.syntaxError.clear();
		doc.errorLine = -1;
		doc.errorColumn = -1;
		const std::string text = doc.editor.GetText();
		if (doc.lang == TextLang::Json)
			CheckJson(doc, text);
		else
			CheckLua(doc, text);
		doc.checkedVersion = doc.version;
		ApplyErrorMarkers(doc);
	}

	// 读磁盘上的文件走一遍真正的 UI 解析（样式引用、继承、绑定），和热重载用的是同一套代码
	void RunUiCheck(TextDoc& doc, ProjectManager* projectManager)
	{
		doc.needUiCheck = false;
		doc.uiCheckOk = true;
		doc.uiCheckMessage.clear();
		if (doc.lang != TextLang::Json || !projectManager || !projectManager->IsProjectOpen())
			return;

		try
		{
			UI::ResolveContext context;
			context.projectRoot = projectManager->GetProjectAssetFullPath(AssetPath());
			context.exists = [](const std::string& p) { std::error_code ec; return fs::exists(fs::path(p), ec); };

			// 引擎里的路径都是窄字符串，这里保持一致
			const std::string fullPath = doc.path.string();
			if (doc.isStyle)
			{
				UI::UiStyle style;
				std::string error;
				doc.uiCheckOk = UI::ParseStyleFile(fullPath, context, style, error);
				if (!doc.uiCheckOk)
					doc.uiCheckMessage = "UI 解析：" + error;
			}
			else
			{
				const UI::ParseResult result = UI::ParseDocumentFile(fullPath, context);
				doc.uiCheckOk = result.ok;
				if (!result.ok)
					doc.uiCheckMessage = "UI 解析：" + result.error;
			}
		}
		catch (const std::exception& e)
		{
			doc.uiCheckOk = false;
			doc.uiCheckMessage = std::string("UI 解析出错：") + e.what();
		}
	}

	// ---------------- 文件操作 ----------------

	void SetupEditor(TextDoc& doc)
	{
		TextEditor& editor = doc.editor;
		editor.SetLanguage(doc.lang == TextLang::Lua ? TextEditor::Language::Lua() : JsonWithComments());
		editor.SetTabSize(4);
		editor.SetShowLineNumbersEnabled(true);
		editor.SetShowCurrentLineHighlightEnabled(true);
		editor.SetShowMatchingBrackets(true);
		editor.SetAutoIndentEnabled(true);
		editor.SetFindReplaceEnabled(true);
		editor.SetFindButtonLabel("查找");
		editor.SetFindAllButtonLabel("全部");
		editor.SetReplaceButtonLabel("替换");
		editor.SetReplaceAllButtonLabel("全部替换");

		TextDoc* target = &doc;
		editor.SetChangeCallback([target]
			{
				++target->version;
				target->lastEditTime = ImGui::GetTime();
				target->status.clear();
			});
	}

	// keepView：重新加载时尽量留在原来的位置
	bool LoadDoc(TextDoc& doc, std::string& error, bool keepView)
	{
		std::string raw;
		if (!ReadFileBytes(doc.path, raw))
		{
			error = "读不了文件";
			return false;
		}

		const TextEditor::DocPos cursor = doc.editor.GetMainCursorPosition();
		const size_t firstLine = doc.editor.GetFirstVisibleRow();

		const std::string text = Decode(doc, std::move(raw));
		doc.editor.SetText(text);
		doc.editor.SetReadOnlyEnabled(doc.readOnly);
		if (keepView)
		{
			doc.editor.SetCursor(cursor);
			doc.editor.ScrollToLine(firstLine, TextEditor::Scroll::alignTop);
		}

		// 以编辑器读回来的内容为准，免得刚打开就显示有修改
		doc.savedText = doc.editor.GetText();
		++doc.version;
		doc.dirtyVersion = 0;

		std::error_code ec;
		doc.diskTime = fs::last_write_time(doc.path, ec);
		doc.externalChanged = false;
		doc.missingOnDisk = false;
		doc.needUiCheck = true;
		RunSyntaxCheck(doc);
		return true;
	}

	bool SaveDoc(TextDoc& doc, ProjectManager* projectManager)
	{
		if (doc.readOnly)
			return false;

		const std::string text = doc.editor.GetText();
		std::string error;
		if (!WriteFileBytes(doc.path, EncodeForDisk(doc, text), error))
		{
			doc.status = "保存失败：" + error;
			return false;
		}

		doc.savedText = text;
		doc.dirtyVersion = 0;
		std::error_code ec;
		doc.diskTime = fs::last_write_time(doc.path, ec);
		doc.externalChanged = false;
		doc.missingOnDisk = false;
		doc.status = "已保存";
		RunUiCheck(doc, projectManager);
		return true;
	}

	void RequestSave(TextDoc& doc, ProjectManager* projectManager)
	{
		if (doc.readOnly)
			return;
		if (doc.checkedVersion != doc.version)
			RunSyntaxCheck(doc);
		if (!doc.syntaxError.empty())
		{
			doc.confirmSaveWithError = true;
			return;
		}
		SaveDoc(doc, projectManager);
	}

	void GotoError(TextDoc& doc)
	{
		if (doc.errorLine < 0)
			return;
		const size_t column = doc.errorColumn >= 0 ? static_cast<size_t>(doc.errorColumn) : 0;
		doc.editor.SetCursor(TextEditor::DocPos(static_cast<size_t>(doc.errorLine), column));
		doc.editor.SetFocus();
	}

	void PollDisk(TextDoc& doc)
	{
		const double now = ImGui::GetTime();
		if (now < doc.nextDiskPoll)
			return;
		doc.nextDiskPoll = now + kDiskPollInterval;

		std::error_code ec;
		const auto writeTime = fs::last_write_time(doc.path, ec);
		if (ec)
		{
			doc.missingOnDisk = true;	// 被删了或被占用：保留内容，保存时会重新创建
			return;
		}
		doc.missingOnDisk = false;

		if (writeTime == doc.diskTime || doc.externalChanged)
			return;

		if (!IsDirty(doc))
		{
			std::string error;
			if (LoadDoc(doc, error, true))
				doc.status = "文件在外部被修改，已重新加载";
			else
				doc.status = "文件在外部被修改，重新加载失败：" + error;
		}
		else
		{
			doc.externalChanged = true;
			doc.pendingDiskTime = writeTime;
		}
	}

	// ---------------- 界面 ----------------

	void DrawFooter(TextDoc& doc)
	{
		// 语法
		if (doc.checkedVersion != doc.version)
		{
			ImGui::TextDisabled("检查中…");
		}
		else if (doc.syntaxError.empty())
		{
			ImGui::TextColored(kColorOk, "%s", doc.lang == TextLang::Lua ? "Lua 语法正确" : "JSON 语法正确");
		}
		else
		{
			ImGui::TextColored(kColorError, "%s", doc.syntaxError.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s\n（点击跳到出错的地方）", doc.syntaxError.c_str());
			if (ImGui::IsItemClicked())
				GotoError(doc);
		}

		// UI 解析（打开时和保存后）
		if (!doc.uiCheckMessage.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
			ImGui::TextColored(doc.uiCheckOk ? kColorOk : kColorWarn, "%s", doc.uiCheckMessage.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", doc.uiCheckMessage.c_str());
		}

		if (!doc.status.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("| %s", doc.status.c_str());
		}

		// 右侧：行列、换行、编码
		const TextEditor::DocPos cursor = doc.editor.GetMainCursorPosition();
		char info[96];
		std::snprintf(info, sizeof(info), "行 %zu，列 %zu   %s   %s", cursor.line + 1, cursor.index + 1, doc.crlf ? "CRLF" : "LF", doc.hadBom ? "UTF-8 BOM" : "UTF-8");
		const float width = ImGui::CalcTextSize(info).x;
		ImGui::SameLine();
		const float room = ImGui::GetContentRegionAvail().x;
		if (room > width)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - width);
		ImGui::TextDisabled("%s", info);
	}

	void DrawPopups(TextDoc& doc, ProjectManager* projectManager)
	{
		if (doc.confirmClose)
		{
			ImGui::OpenPopup("关闭前保存？");
			doc.confirmClose = false;
		}
		if (ImGui::BeginPopupModal("关闭前保存？", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("%s 有未保存的修改。", doc.title.c_str());
			if (ImGui::Button("保存"))
			{
				if (SaveDoc(doc, projectManager))
					doc.open = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("不保存"))
			{
				doc.open = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("取消"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (doc.confirmSaveWithError)
		{
			ImGui::OpenPopup("有语法错误");
			doc.confirmSaveWithError = false;
		}
		if (ImGui::BeginPopupModal("有语法错误", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("当前内容有语法错误，保存后热重载会失败（引擎会继续用旧内容）：");
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
			ImGui::TextColored(kColorError, "%s", doc.syntaxError.c_str());
			ImGui::PopTextWrapPos();
			if (ImGui::Button("仍然保存"))
			{
				SaveDoc(doc, projectManager);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("取消"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	void DrawDoc(TextDoc& doc, ProjectManager* projectManager)
	{
		// 停止输入一会儿再检查
		if (doc.checkedVersion != doc.version && ImGui::GetTime() - doc.lastEditTime >= kCheckDelay)
			RunSyntaxCheck(doc);
		if (doc.needUiCheck && projectManager)
			RunUiCheck(doc, projectManager);

		const bool dirty = IsDirty(doc);
		const std::string title = doc.title + (dirty ? " *" : "") + (doc.readOnly ? "（只读）" : "") + "###TextAsset_" + doc.windowId;

		ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (doc.focusRequest)
		{
			ImGui::SetNextWindowFocus();
			doc.editor.SetFocus();
			doc.focusRequest = false;
		}

		bool open = true;
		const bool visible = ImGui::Begin(title.c_str(), &open);
		if (!open)
		{
			if (dirty)
				doc.confirmClose = true;
			else
				doc.open = false;
		}

		if (visible)
		{
			ImGuiIO& io = ImGui::GetIO();
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
					RequestSave(doc, projectManager);
				if (ImGui::IsKeyPressed(ImGuiKey_F3, true))
				{
					if (doc.editor.HasFindString())
						doc.editor.FindNext();
					else
						doc.editor.OpenFindReplaceWindow();
				}

				// 字号：Ctrl+= / Ctrl+- 放大缩小，Ctrl+0 还原
				if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, true)))
					SetFontScale(s_fontScale + kFontScaleStep);
				if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, true)))
					SetFontScale(s_fontScale - kFontScaleStep);
				if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)))
					SetFontScale(1.0f);
			}

			// 字号：鼠标在窗口上时 Ctrl+滚轮缩放。
			// 按住 Ctrl 时先占住滚轮，下一帧 ImGui 就不会再拿它去滚动正文
			if (io.KeyCtrl && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
			{
				const ImGuiID wheelOwner = ImGui::GetID("##font_zoom");
				if (io.MouseWheel != 0.0f && ImGui::TestKeyOwner(ImGuiKey_MouseWheelY, wheelOwner))
					SetFontScale(s_fontScale + io.MouseWheel * kFontScaleStep);
				ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, wheelOwner);
			}

			// 工具栏
			ImGui::BeginDisabled(doc.readOnly || !dirty);
			if (ImGui::Button("保存"))
				RequestSave(doc, projectManager);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("查找"))
				doc.editor.OpenFindReplaceWindow();
			ImGui::SameLine();
			ImGui::BeginDisabled(!doc.editor.CanUndo());
			if (ImGui::Button("撤销"))
				doc.editor.Undo();
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!doc.editor.CanRedo());
			if (ImGui::Button("重做"))
				doc.editor.Redo();
			ImGui::EndDisabled();
			ImGui::SameLine();

			// 字号
			if (ImGui::Button("A-"))
				SetFontScale(s_fontScale - kFontScaleStep);
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%d%%", static_cast<int>(std::round(s_fontScale * 100.0f)));
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("字号（点击还原）\nCtrl+滚轮、Ctrl+= / Ctrl+- 缩放，Ctrl+0 还原");
			if (ImGui::IsItemClicked())
				SetFontScale(1.0f);
			ImGui::SameLine();
			if (ImGui::Button("A+"))
				SetFontScale(s_fontScale + kFontScaleStep);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", ToUtf8(doc.path).c_str());

			if (doc.readOnly)
				ImGui::TextColored(kColorWarn, "%s", doc.readOnlyReason.c_str());

			if (doc.missingOnDisk)
				ImGui::TextColored(kColorWarn, "磁盘上找不到这个文件了（被删除或被占用）。保存会重新创建它。");

			if (doc.externalChanged)
			{
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(kColorWarn, "文件在外部被修改了。");
				ImGui::SameLine();
				if (ImGui::SmallButton("重新加载（放弃我的修改）"))
				{
					std::string error;
					if (LoadDoc(doc, error, true))
						doc.status = "已重新加载";
					else
						doc.status = "重新加载失败：" + error;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("保留我的修改"))
				{
					doc.externalChanged = false;
					doc.diskTime = doc.pendingDiskTime;	// 之后再变才提示
				}
			}

			// 正文
			// 没有 Consolas 时传 nullptr：沿用当前字体，只改字号
			ImGui::PushFont(EditorFont(), EditorFontSize());
			doc.editor.Render("##text", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
			ImGui::PopFont();

			DrawFooter(doc);
		}

		DrawPopups(doc, projectManager);
		ImGui::End();
	}
}

namespace TextAssetEditor
{
	bool CanOpen(const std::filesystem::path& fullPath)
	{
		const std::string extension = ToLowerAscii(ToUtf8(fullPath.extension()));
		return extension == ".ui" || extension == ".uistyle" || extension == ".lua";
	}

	void Open(const std::filesystem::path& fullPath)
	{
		const std::wstring key = MakeKey(fullPath);
		for (auto& doc : s_docs)
		{
			if (doc->key == key)
			{
				doc->focusRequest = true;
				return;
			}
		}

		auto doc = std::make_unique<TextDoc>();
		doc->path = fullPath;
		doc->key = key;
		doc->title = ToUtf8(fullPath.filename());
		doc->windowId = std::to_string(std::hash<std::wstring>{}(key));

		const std::string extension = ToLowerAscii(ToUtf8(fullPath.extension()));
		doc->lang = (extension == ".lua") ? TextLang::Lua : TextLang::Json;
		doc->isStyle = (extension == ".uistyle");
		SetupEditor(*doc);

		std::string error;
		if (!LoadDoc(*doc, error, false))
		{
			doc->readOnly = true;
			doc->readOnlyReason = "打不开文件：" + error;
			doc->editor.SetReadOnlyEnabled(true);
		}
		s_docs.push_back(std::move(doc));
	}

	void DrawWindows(ProjectManager* projectManager)
	{
		// 没开窗口也要注册：否则 ImGui 存 imgui.ini 时会把我们的段落丢掉
		EnsureSettingsHandler();

		for (auto& doc : s_docs)
		{
			PollDisk(*doc);
			DrawDoc(*doc, projectManager);
		}
		std::erase_if(s_docs, [](const std::unique_ptr<TextDoc>& doc) { return !doc->open; });
	}

	bool HasUnsavedChanges()
	{
		for (auto& doc : s_docs)
		{
			if (IsDirty(*doc))
				return true;
		}
		return false;
	}
}
