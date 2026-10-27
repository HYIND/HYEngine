#include "UI/UiDocumentParser.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace UI
{
	// 节点名 -> 节点 id（重名取第一个），宿主按名字塞列表/文本行时用。
	static void CollectNodeIds(const UiNode &node, std::unordered_map<std::string, int> &outIds)
	{
		if (!node.name.empty() && outIds.find(node.name) == outIds.end())
			outIds.emplace(node.name, node.id);

		for (const auto &child : node.children)
			CollectNodeIds(child, outIds);
	}

	namespace
	{
		using Json = nlohmann::json;

		// ---------- 路径工具 ----------

		std::string ToForwardSlashes(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}

		// 处理 "." 与 ".."，产出不带 "./" 前缀的规范路径。
		std::string NormalizePath(const std::string &path)
		{
			std::string forward = ToForwardSlashes(path);
			const bool absolute = !forward.empty() && forward.front() == '/';
			std::vector<std::string> parts;
			std::stringstream stream(forward);
			std::string segment;
			while (std::getline(stream, segment, '/'))
			{
				if (segment.empty() || segment == ".")
					continue;
				if (segment == "..")
				{
					if (!parts.empty() && parts.back() != "..")
						parts.pop_back();
					else if (!absolute)
						parts.push_back("..");
					continue;
				}
				parts.push_back(segment);
			}

			std::string result;
			if (absolute)
				result = "/";
			for (size_t i = 0; i < parts.size(); ++i)
			{
				if (i)
					result += '/';
				result += parts[i];
			}
			return result;
		}

		std::string DirName(const std::string &path)
		{
			const std::string normalized = NormalizePath(path);
			const size_t slash = normalized.find_last_of('/');
			if (slash == std::string::npos)
				return std::string();
			return normalized.substr(0, slash);
		}

		std::string JoinPath(const std::string &base, const std::string &relative)
		{
			if (base.empty())
				return NormalizePath(relative);
			return NormalizePath(base + "/" + relative);
		}

		// ---------- 读取 ----------

		bool ReadJsonFile(const std::string &path, Json &out, std::string &error)
		{
			std::ifstream ifs(ToForwardSlashes(path), std::ios::binary);
			if (!ifs)
			{
				error = "打不开文件：" + path;
				return false;
			}

			try
			{
				out = Json::parse(ifs, nullptr, true, true);
			}
			catch (const std::exception &ex)
			{
				error = std::string("JSON 解析失败：") + path + "：" + ex.what();
				return false;
			}
			return true;
		}

		// ---------- 字段读取 ----------

		float ReadFloat(const Json &object, const char *key, float fallback)
		{
			if (!object.is_object() || !object.contains(key) || !object[key].is_number())
				return fallback;
			return object[key].get<float>();
		}

		bool ReadVec4(const Json &value, Vec4 &out)
		{
			if (!value.is_array() || value.size() < 3)
				return false;
			out.r = value[0].get<float>();
			out.g = value[1].get<float>();
			out.b = value[2].get<float>();
			out.a = value.size() > 3 ? value[3].get<float>() : 1.0f;
			return true;
		}

		bool ReadPadding(const Json &value, Padding &out)
		{
			if (!value.is_object())
				return false;
			out.left = ReadFloat(value, "left", 0.0f);
			out.right = ReadFloat(value, "right", 0.0f);
			out.top = ReadFloat(value, "top", 0.0f);
			out.bottom = ReadFloat(value, "bottom", 0.0f);
			return true;
		}

		bool ReadSlice(const Json &value, Slice &out)
		{
			if (!value.is_object())
				return false;
			out.left = ReadFloat(value, "left", 0.0f);
			out.right = ReadFloat(value, "right", 0.0f);
			out.top = ReadFloat(value, "top", 0.0f);
			out.bottom = ReadFloat(value, "bottom", 0.0f);
			return true;
		}

		// 从一段样式的 "text" 子对象里读文本字段。
		void ReadTextFields(const Json &text, StyleFields &fields)
		{
			if (!text.is_object())
				return;
			if (text.contains("fontFamily") && text["fontFamily"].is_string())
				fields.fontFamily = text["fontFamily"].get<std::string>();
			if (text.contains("fontSize") && text["fontSize"].is_number())
				fields.fontSize = text["fontSize"].get<float>();
			if (text.contains("bold") && text["bold"].is_boolean())
				fields.bold = text["bold"].get<bool>();
			if (text.contains("color"))
			{
				Vec4 color;
				if (ReadVec4(text["color"], color))
					fields.color = color;
			}
			if (text.contains("align") && text["align"].is_string())
				fields.align = text["align"].get<std::string>();
			if (text.contains("valign") && text["valign"].is_string())
				fields.valign = text["valign"].get<std::string>();
			if (text.contains("padding"))
			{
				Padding padding;
				if (ReadPadding(text["padding"], padding))
					fields.padding = padding;
			}
		}

		// 从一段样式的 "background" 子对象里读背景字段。
		void ReadBackgroundFields(const Json &background, StyleFields &fields)
		{
			if (!background.is_object())
				return;
			if (background.contains("texture") && background["texture"].is_string())
				fields.texture = background["texture"].get<std::string>();
			if (background.contains("tint"))
			{
				Vec4 tint;
				if (ReadVec4(background["tint"], tint))
					fields.tint = tint;
			}
			if (background.contains("slice"))
			{
				Slice slice;
				if (ReadSlice(background["slice"], slice))
					fields.slice = slice;
			}
			if (background.contains("cornerRadius") && background["cornerRadius"].is_number())
				fields.cornerRadius = background["cornerRadius"].get<float>();
		}

		StyleFields ReadStyleFields(const Json &object)
		{
			StyleFields fields;
			if (!object.is_object())
				return fields;

			if (object.contains("background"))
				ReadBackgroundFields(object["background"], fields);
			if (object.contains("text"))
				ReadTextFields(object["text"], fields);
			if (object.contains("opacity") && object["opacity"].is_number())
				fields.opacity = object["opacity"].get<float>();
			if (object.contains("fit") && object["fit"].is_string())
				fields.fit = object["fit"].get<std::string>();

			return fields;
		}

		// ---------- 文档 ----------

		UiEvent ReadEvent(const Json &object, const char *key)
		{
			UiEvent event;
			if (!object.is_object() || !object.contains(key) || !object[key].is_object())
				return event;

			const Json &value = object[key];
			if (value.contains("command") && value["command"].is_string())
				event.command = value["command"].get<std::string>();
			if (value.contains("legacyId") && value["legacyId"].is_number_integer())
				event.legacyId = value["legacyId"].get<int>();
			return event;
		}

		// 节点自带的字段（texture / fit / color / textStyle）当作内联样式，优先级最高。
		std::optional<StyleFields> ReadNodeInlineStyle(const Json &node)
		{
			StyleFields fields;
			bool any = false;

			if (node.contains("texture") && node["texture"].is_string())
			{
				fields.texture = node["texture"].get<std::string>();
				any = true;
			}
			if (node.contains("fit") && node["fit"].is_string())
			{
				fields.fit = node["fit"].get<std::string>();
				any = true;
			}
			if (node.contains("color"))
			{
				Vec4 color;
				if (ReadVec4(node["color"], color))
				{
					fields.color = color;
					any = true;
				}
			}
			// 节点自带的 background 块（tint / cornerRadius / texture / slice）同样算内联样式，
			// 这样单文件 demo 不用为每种颜色单独建样式资产。
			if (node.contains("background") && node["background"].is_object())
			{
				ReadBackgroundFields(node["background"], fields);
				any = true;
			}
			if (node.contains("background") && node["background"].is_object())
			{
				ReadBackgroundFields(node["background"], fields);
				any = true;
			}
			if (node.contains("textStyle"))
			{
				StyleFields textFields;
				ReadTextFields(node["textStyle"], textFields);
				MergeFields(fields, textFields);
				any = true;
			}

			if (!any)
				return std::nullopt;
			return fields;
		}

		// 节点自带的 states 块：让它不依赖样式资产也能表达 hover/pressed。
		std::map<std::string, StyleFields> ReadInlineStates(const Json &node)
		{
			std::map<std::string, StyleFields> states;
			if (!node.contains("states") || !node["states"].is_object())
				return states;

			for (auto it = node["states"].begin(); it != node["states"].end(); ++it)
			{
				if (!it.value().is_object())
					continue;
				states[it.key()] = ReadStyleFields(it.value());
			}
			return states;
		}

		UiNode ParseNode(const Json &node)
		{
			UiNode result;
			if (node.contains("type") && node["type"].is_string())
				result.type = node["type"].get<std::string>();
			if (node.contains("name") && node["name"].is_string())
				result.name = node["name"].get<std::string>();
			if (node.contains("style") && node["style"].is_string())
				result.styleRef = node["style"].get<std::string>();
			if (node.contains("text") && node["text"].is_string())
				result.text = node["text"].get<std::string>();
			if (node.contains("texture") && node["texture"].is_string())
				result.texture = node["texture"].get<std::string>();

			if (node.contains("rect") && node["rect"].is_object())
			{
				const Json &rect = node["rect"];
				result.rect.x = ReadFloat(rect, "x", 0.0f);
				result.rect.y = ReadFloat(rect, "y", 0.0f);
				result.rect.w = ReadFloat(rect, "w", 0.0f);
				result.rect.h = ReadFloat(rect, "h", 0.0f);
			}

			// 数值与文本输入相关字段：控件不同但字段名统一，取值都做存在性判断。
			if (node.contains("min") && node["min"].is_number())
				result.minValue = node["min"].get<float>();
			if (node.contains("max") && node["max"].is_number())
				result.maxValue = node["max"].get<float>();
			if (node.contains("value") && node["value"].is_number())
				result.value = node["value"].get<float>();
			if (node.contains("step") && node["step"].is_number())
				result.step = node["step"].get<float>();
			if (node.contains("placeholder") && node["placeholder"].is_string())
				result.placeholder = node["placeholder"].get<std::string>();
			if (node.contains("maxLength") && node["maxLength"].is_number_integer())
				result.maxLength = node["maxLength"].get<int>();
			if (node.contains("showPercent") && node["showPercent"].is_boolean())
				result.showPercent = node["showPercent"].get<bool>();

	if (node.contains("rowHeight") && node["rowHeight"].is_number())
		result.rowHeight = node["rowHeight"].get<float>();

			result.inlineStyle = ReadNodeInlineStyle(node);
			result.inlineStates = ReadInlineStates(node);
			result.onClick = ReadEvent(node, "onClick");
			if (node.contains("options") && node["options"].is_array())
			{
				for (const auto &option : node["options"])
				{
					if (!option.is_object())
						continue;

					UiOption item;
					if (option.contains("text") && option["text"].is_string())
						item.text = option["text"].get<std::string>();
					if (option.contains("value") && option["value"].is_number())
						item.value = option["value"].get<int>();
					result.options.push_back(std::move(item));
				}
			}

			result.onChange = ReadEvent(node, "onChange");
			if (node.contains("bind") && node["bind"].is_object())
			{
				UiNodeBinding binding;
				if (node["bind"].contains("node") && node["bind"]["node"].is_string())
					binding.node = node["bind"]["node"].get<std::string>();
				if (node["bind"].contains("format") && node["bind"]["format"].is_string())
					binding.format = node["bind"]["format"].get<std::string>();
				if (!binding.node.empty())
					result.bind = std::move(binding);
			}

			if (node.contains("children") && node["children"].is_array())
			{
				for (const Json &child : node["children"])
					result.children.push_back(ParseNode(child));
			}

			return result;
		}

		UiDocument ParseDocument(const Json &root, const std::string &sourcePath)
		{
			UiDocument document;
			document.sourcePath = sourcePath;

			if (root.contains("name") && root["name"].is_string())
				document.name = root["name"].get<std::string>();
			if (root.contains("space") && root["space"].is_number())
				document.space = root["space"].get<double>();
			if (root.contains("aspectMode") && root["aspectMode"].is_string())
				document.aspectMode = root["aspectMode"].get<std::string>();
			if (root.contains("layer") && root["layer"].is_number_integer())
				document.layer = root["layer"].get<int>();

			if (root.contains("styles") && root["styles"].is_object())
			{
				for (auto it = root["styles"].begin(); it != root["styles"].end(); ++it)
				{
					if (it.value().is_string())
						document.styleMap[it.key()] = it.value().get<std::string>();
				}
			}

			if (root.contains("root") && root["root"].is_object())
				document.root = ParseNode(root["root"]);

			return document;
		}

		// ---------- 样式 ----------

		UiStyle ParseStyle(const Json &root, const std::string &sourcePath)
		{
			UiStyle style;
			style.sourcePath = sourcePath;

			if (root.contains("name") && root["name"].is_string())
				style.name = root["name"].get<std::string>();
			if (root.contains("extends") && root["extends"].is_string())
				style.extendsRef = root["extends"].get<std::string>();

			style.base = ReadStyleFields(root);

			if (root.contains("states") && root["states"].is_object())
			{
				for (auto it = root["states"].begin(); it != root["states"].end(); ++it)
					style.states[it.key()] = ReadStyleFields(it.value());
			}

			return style;
		}

		// 引用解析：先按文档目录，再按工程基路径。
		bool ResolveStylePath(const std::string &reference,
			const std::string &documentPath,
			const ResolveContext &context,
			std::string &resolvedPath,
			std::string &error)
		{
			const std::string documentDirectory = DirName(documentPath);
			const std::string candidates[2] = {
				JoinPath(documentDirectory, reference),
				context.projectRoot.empty() ? std::string() : JoinPath(context.projectRoot, reference)
			};

			for (const std::string &candidate : candidates)
			{
				if (candidate.empty() || !context.exists)
					continue;
				if (context.exists(candidate))
				{
					resolvedPath = candidate;
					return true;
				}
			}

			std::ostringstream stream;
			stream << "样式引用找不到：" << reference << "（文档 " << documentPath << "）；尝试过：";
			for (const std::string &candidate : candidates)
				stream << (candidate.empty() ? std::string("<跳过>") : candidate) << "  ";
			error = stream.str();
			return false;
		}

		// 递归加载样式并展开 extends：父在前，子覆盖。
		std::shared_ptr<UiStyle> LoadStyle(const std::string &reference,
			const std::string &documentPath,
			const ResolveContext &context,
			std::set<std::string> &visiting,
			std::string &error)
		{
			std::string resolvedPath;
			if (!ResolveStylePath(reference, documentPath, context, resolvedPath, error))
				return nullptr;

			if (visiting.count(resolvedPath))
			{
				error = "样式的 extends 形成环：" + resolvedPath;
				return nullptr;
			}

			Json json;
			if (!ReadJsonFile(resolvedPath, json, error))
				return nullptr;

			auto style = std::make_shared<UiStyle>(ParseStyle(json, resolvedPath));

			if (!style->extendsRef.empty())
			{
				visiting.insert(resolvedPath);
				auto parent = LoadStyle(style->extendsRef, resolvedPath, context, visiting, error);
				visiting.erase(resolvedPath);
				if (!parent)
					return nullptr;

				// 父在前、子覆盖
				StyleFields merged = parent->base;
				MergeFields(merged, style->base);
				style->base = merged;

				for (const auto &[stateName, fields] : parent->states)
				{
					auto existing = style->states.find(stateName);
					if (existing == style->states.end())
						style->states[stateName] = fields;
					else
					{
						StyleFields stateMerged = fields;
						MergeFields(stateMerged, existing->second);
						existing->second = stateMerged;
					}
				}
			}

			return style;
		}

		void ResolveNode(const UiNode &node,
			const ParseResult &result,
			ResolvedNodeStyle &out)
		{
			std::ostringstream description;

			StyleFields effective;

			// 1) 节点 style 名 -> 文档 styles 表 -> 已展开的样式
			if (!node.styleRef.empty())
			{
				auto it = result.styles.find(node.styleRef);
				if (it != result.styles.end() && it->second)
				{
					MergeFields(effective, it->second->base);
					out.states = it->second->states;
					description << "style:" << node.styleRef << "(" << it->second->sourcePath << ")";
				}
				else
				{
					description << "style:" << node.styleRef << "(未找到)";
				}
			}

			// 2) 节点内联字段覆盖样式
			if (node.inlineStyle)
			{
				MergeFields(effective, *node.inlineStyle);
				description << (description.tellp() > 0 ? " > " : "") << "inline";
			}

			// 3) 节点内联状态覆盖样式里的同名状态
			for (const auto &[stateName, stateFields] : node.inlineStates)
			{
				MergeFields(out.states[stateName], stateFields);
				description << (description.tellp() > 0 ? " > " : "") << "inline:" << stateName;
			}

			if (description.tellp() <= 0)
				description << "（无样式）";

			out.effective = effective;
			out.sourceDescription = description.str();
		}

		// 把 bind.node（节点名）解析成节点 id：运行时按 id 取该节点的值。
		static void ResolveBindings(UiNode &node, const std::unordered_map<std::string, int> &ids, std::string &error)
		{
			if (node.bind && !node.bind->node.empty())
			{
				auto it = ids.find(node.bind->node);
				if (it == ids.end())
					error += (error.empty() ? "" : "\n") + std::string("节点 ") + node.name + " 的 bind 指向不存在的节点：" + node.bind->node;
				else
					node.bind->targetId = it->second;
			}

			for (auto &child : node.children)
				ResolveBindings(child, ids, error);
		}

		void AssignNodeIds(UiNode &node, int &nextId)
		{
			node.id = nextId++;
			for (UiNode &child : node.children)
				AssignNodeIds(child, nextId);
		}

		void ResolveNodeTree(const UiNode &node, ParseResult &result)
		{
			ResolvedNodeStyle resolvedStyle;
			ResolveNode(node, result, resolvedStyle);
			result.resolved[&node] = resolvedStyle;

			for (const UiNode &child : node.children)
				ResolveNodeTree(child, result);
		}

		std::string FormatVec4(const Vec4 &value)
		{
			char buffer[96];
			std::snprintf(buffer, sizeof(buffer), "[%.2f,%.2f,%.2f,%.2f]", value.r, value.g, value.b, value.a);
			return buffer;
		}

		std::string FormatFloat(float value)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%.1f", value);
			return buffer;
		}

		void DumpNode(const UiNode &node,
			const ParseResult &result,
			int depth,
			std::ostringstream &out)
		{
			std::string indent(static_cast<size_t>(depth) * 2, ' ');

			out << indent << "- " << (node.name.empty() ? "<未命名>" : node.name)
				<< " [" << node.type << "]"
				<< " rect(" << FormatFloat(node.rect.x) << "," << FormatFloat(node.rect.y) << ","
				<< FormatFloat(node.rect.w) << "x" << FormatFloat(node.rect.h) << ")";

			auto it = result.resolved.find(&node);
			if (it != result.resolved.end())
			{
				const ResolvedNodeStyle &resolvedStyle = it->second;
				out << "  <- " << resolvedStyle.sourceDescription;

				const StyleFields &fields = resolvedStyle.effective;
				if (fields.fontSize)
					out << "  fontSize=" << FormatFloat(*fields.fontSize);
				if (fields.color)
					out << "  color=" << FormatVec4(*fields.color);
				if (fields.tint)
					out << "  tint=" << FormatVec4(*fields.tint);
				if (fields.texture)
					out << "  texture=" << *fields.texture;
				if (fields.fit)
					out << "  fit=" << *fields.fit;
				if (fields.opacity)
					out << "  opacity=" << FormatFloat(*fields.opacity);
				if (!resolvedStyle.states.empty())
				{
					out << "  states[";
					bool first = true;
					for (const auto &[stateName, _] : resolvedStyle.states)
					{
						if (!first)
							out << ",";
						out << stateName;
						first = false;
					}
					out << "]";
				}
			}

			if (!node.text.empty())
				out << "  text=\"" << node.text << "\"";
			if (!node.onClick.command.empty() || node.onClick.legacyId != 0)
				out << "  onClick{" << node.onClick.command << "," << node.onClick.legacyId << "}";

			out << "\n";

			for (const UiNode &child : node.children)
				DumpNode(child, result, depth + 1, out);
		}
	}

	ParseResult ParseDocumentFile(const std::string &filePath, const ResolveContext &context)
	{
		ParseResult result;
		const std::string documentPath = NormalizePath(filePath);

		Json json;
		if (!ReadJsonFile(documentPath, json, result.error))
			return result;

		result.document = ParseDocument(json, documentPath);

		// 先解析所有样式别名（文档 styles 表）
		for (const auto &[alias, reference] : result.document.styleMap)
		{
			std::set<std::string> visiting;
			std::string error;
			auto style = LoadStyle(reference, documentPath, context, visiting, error);
			if (!style)
			{
				result.error += (result.error.empty() ? "" : "\n") + std::string("别名 ") + alias + "：" + error;
				continue;
			}
			result.styles[alias] = style;
		}

		// 再算每个节点的有效样式
		int nodeId = 1;
		AssignNodeIds(result.document.root, nodeId);
		ResolveNodeTree(result.document.root, result);
		CollectNodeIds(result.document.root, result.nodeIds);
		ResolveBindings(result.document.root, result.nodeIds, result.error);

		result.ok = result.error.empty();
		return result;
	}

	bool ParseStyleFile(const std::string &filePath, const ResolveContext &context,
		UiStyle &outStyle, std::string &error)
	{
		std::set<std::string> visiting;
		auto style = LoadStyle(filePath, std::string(), context, visiting, error);
		if (!style)
			return false;

		outStyle = *style;
		return true;
	}

	std::string DumpResolved(const ParseResult &result)
	{
		std::ostringstream out;

		out << "=== " << (result.document.name.empty() ? "<未命名文档>" : result.document.name)
			<< "   space=" << result.document.space
			<< "  aspectMode=" << result.document.aspectMode
			<< "  layer=" << result.document.layer
			<< "  source=" << result.document.sourcePath << "\n";

		if (!result.document.styleMap.empty())
		{
			out << "styles:\n";
			for (const auto &[alias, reference] : result.document.styleMap)
			{
				out << "  " << alias << " -> " << reference;
				auto it = result.styles.find(alias);
				if (it != result.styles.end() && it->second)
					out << "   已解析: " << it->second->sourcePath
					<< "  (name=" << it->second->name << ")";
				else
					out << "   未解析";
				out << "\n";
			}
		}

		out << "nodes:\n";
		DumpNode(result.document.root, result, 0, out);

		if (!result.error.empty())
			out << "errors:\n" << result.error << "\n";

		return out.str();
	}
}


namespace UI
{
	std::vector<std::string> CollectStyleReferences(const std::string &filePath)
	{
	std::vector<std::string> references;

	nlohmann::json root;
	std::string error;
	if (!ReadJsonFile(filePath, root, error))
		return references;

	auto styles = root.find("styles");
	if (styles == root.end() || !styles->is_object())
		return references;

	for (const auto &entry : styles->items())
	{
		if (!entry.value().is_string())
			continue;
		std::string reference = entry.value().get<std::string>();
		if (reference.empty())
			continue;
		for (char &c : reference)
		{
			if (c == '\\')
				c = '/';
		}
		references.push_back(std::move(reference));
	}

	return references;
}
}
