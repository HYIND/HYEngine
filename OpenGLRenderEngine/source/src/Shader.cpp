#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <regex>
#include <filesystem>

static std::string ReadFromFile(const std::string& path)
{
	std::ifstream file;
	file.exceptions(std::ifstream::badbit);// 异常机制处理：保证ifstream对象可以抛出异常：

	try
	{
		file.open(path);
		if (!file) {
			std::cerr << "文件打开失败" << ",path=" << path << std::endl;
			return std::string();
		}

		std::stringstream stream;
		stream << file.rdbuf();
		file.close();

		return stream.str();
	}
	catch (std::ifstream::failure e) {
		std::cout << "ERROR::FILE_NOT_SUCCESSFULLY_READ" << std::endl;
	}

	return std::string();
}

static std::string RemoveComments(std::string code)
{
	// 1. 删除多行注释
	std::regex blockComment(R"(/\*.*?\*/)");
	code = std::regex_replace(code, blockComment, "");

	// 2. 删除单行注释
	std::regex lineComment(R"(//[^\n]*)");
	code = std::regex_replace(code, lineComment, "");

	//std::cout << code << '\n';
	return code;
}

static std::string preProcessInclude(std::string shaderSource)
{
	struct matchData
	{
		size_t pos;
		size_t length;
		std::filesystem::path filepath;
	};

	while (true)
	{
		bool IsRepleaceAnyStr = false;
		std::vector<matchData> matches;  // 存储位置和文件名

		// 查找 #include
		std::regex includePattern(R"(#include\s*[<\"]([^>\"]*)[>\"])");
		std::sregex_iterator it(shaderSource.begin(), shaderSource.end(), includePattern);
		std::sregex_iterator end;

		// 先收集所有匹配的位置
		for (; it != end; ++it)
		{
			std::smatch match = *it;
			std::string includeStr = match.str(0);

			size_t pos = match.position(0);
			size_t length = includeStr.length();
			std::string filepath = match.str(1);

			matches.push_back({ pos,length,filepath });
		}

		for (auto it = matches.rbegin(); it != matches.rend(); ++it)
		{
			auto& matchdata = *it;
			for (auto& path : { matchdata.filepath.string(), matchdata.filepath.filename().string() })
			{
				std::string code = ReadFromFile(path);
				if (!code.empty())
				{
					shaderSource.replace(matchdata.pos, matchdata.length, code);
					IsRepleaceAnyStr = true;
					break;
				}
			}
		}

		if (!IsRepleaceAnyStr)
			break;
	}

	//std::cout << shaderSource << '\n';
	return shaderSource;
}

static std::string insertDefinesAfterVersionAdvanced(const std::string& shaderSource, const std::map<std::string, std::string>& defines) {
	if (defines.empty()) {
		return shaderSource;
	}

	// 查找 #version，忽略前面的注释和空行
	std::regex versionPattern(R"(#version\s+[0-9]+\s*(?:es|core|compatibility)?)");

	std::string definestr;
	for (auto& [name, value] : defines)
		definestr += std::format("#define {} {}\n", name, value);

	std::smatch match;
	if (std::regex_search(shaderSource, match, versionPattern))
	{
		std::string versionLine = match.str(0);
		size_t versionStart = match.position(0);
		size_t versionEnd = versionStart + versionLine.length();

		std::string result = shaderSource;
		result.insert(versionEnd, "\n" + definestr);// 在 #version 之后插入
		return result;
	}

	// 没找到 #version，在开头插入
	return definestr + "\n" + shaderSource;
}

enum class ShaderType { Vertex = 0, Geometry, Fragment, Compute };
static bool CompileSourceCode(const std::string& code, GLuint& shader, ShaderType type)
{
	const GLchar* ShaderCode = code.c_str();

	int ShaderFlag = 0;

	if (type == ShaderType::Vertex)
		ShaderFlag = GL_VERTEX_SHADER;
	else if (type == ShaderType::Geometry)
		ShaderFlag = GL_GEOMETRY_SHADER;
	else if (type == ShaderType::Fragment)
		ShaderFlag = GL_FRAGMENT_SHADER;
	else if (type == ShaderType::Compute)
		ShaderFlag = GL_COMPUTE_SHADER;

	shader = glCreateShader(ShaderFlag);				// 创建顶点着色器对象
	glShaderSource(shader, 1, &ShaderCode, NULL);			// 将顶点着色器的内容传进来
	glCompileShader(shader);								// 编译顶点着色器

	GLint flag;												// 用于判断编译是否成功
	glGetShaderiv(shader, GL_COMPILE_STATUS, &flag);		// 获取编译状态
	return flag;
}

static void PrintError(GLuint shader, ShaderType type, const std::string& path) {
	GLint flag;
	GLchar infoLog[2048];
	glGetShaderiv(shader, GL_COMPILE_STATUS, &flag); // 获取编译状态
	if (!flag)
	{
		glGetShaderInfoLog(shader, 2048, NULL, infoLog);

		std::string shaderTypeStr;

		if (type == ShaderType::Vertex)
			shaderTypeStr = "VERTEX";
		else if (type == ShaderType::Geometry)
			shaderTypeStr = "GEOMETRY";
		else if (type == ShaderType::Fragment)
			shaderTypeStr = "FRAGMENT";
		else if (type == ShaderType::Compute)
			shaderTypeStr = "COMPUTE";

		std::cout << std::format("ERROR::SHADER::{}::COMPILATION_FAILED, path = {} \n {}", shaderTypeStr, path, infoLog);
	}
};

static void PrintError(GLuint shader, ShaderType type)
{
	GLint flag;
	GLchar infoLog[2048];
	glGetShaderiv(shader, GL_COMPILE_STATUS, &flag); // 获取编译状态
	if (!flag)
	{
		glGetShaderInfoLog(shader, 2048, NULL, infoLog);

		std::string shaderTypeStr;

		if (type == ShaderType::Vertex)
			shaderTypeStr = "VERTEX";
		else if (type == ShaderType::Geometry)
			shaderTypeStr = "GEOMETRY";
		else if (type == ShaderType::Fragment)
			shaderTypeStr = "FRAGMENT";
		else if (type == ShaderType::Compute)
			shaderTypeStr = "COMPUTE";

		std::cout << std::format("ERROR::SHADER::{}::COMPILATION_FAILED \n {}", shaderTypeStr, infoLog);
	}
};


Shader::Shader()
	:vertex(0), geometry(0), fragment(0), compute(0), Program(0)
{
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
	:Shader()
{
	CompileFromFile(vertexPath, fragmentPath);
}

Shader::Shader(const std::string& vertexPath, const std::string& geometryPath, const std::string& fragmentPath)
	:Shader()
{
	CompileFromFile(vertexPath, geometryPath, fragmentPath);
}

Shader::Shader(const std::string& computePath)
	:Shader()
{
	CompileFromFile(computePath);
}

Shader::~Shader()
{
	Release();
}

static auto ProcessFile = [](const std::string& path, GLuint& shader, ShaderType type, std::map<std::string, std::string>& defines)->bool
	{
		std::string code;
		code = ReadFromFile(path);
		code = RemoveComments(code);
		code = preProcessInclude(code);
		code = insertDefinesAfterVersionAdvanced(code, defines);

		if (!CompileSourceCode(code, shader, type))
		{
			PrintError(shader, type, path);
			return false;
		}
		return true;
	};

static auto ProcessCode = [](const std::string& sourcecode, GLuint& shader, ShaderType type, std::map<std::string, std::string>& defines)->bool
	{
		std::string code = sourcecode;
		code = RemoveComments(code);
		code = preProcessInclude(code);
		code = insertDefinesAfterVersionAdvanced(sourcecode, defines);

		if (!CompileSourceCode(code, shader, type))
		{
			PrintError(shader, type);
			return false;
		}
		return true;
	};

static auto CreateProgram = [](GLuint& program, std::vector<GLuint> shaders)->bool
	{
		program = glCreateProgram();
		for (auto shader : shaders)
			glAttachShader(program, shader);
		glLinkProgram(program);

		GLint flag;
		GLchar infoLog[2048];
		glGetProgramiv(program, GL_LINK_STATUS, &flag);
		if (!flag) {
			glGetProgramInfoLog(program, 2048, NULL, infoLog);
			std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
		}
		return flag;
	};

bool Shader::CompileFromFile(const std::string& vertexPath, const std::string& fragmentPath)
{
	Release();

	if (
		!ProcessFile(vertexPath, vertex, ShaderType::Vertex, _defines) ||
		!ProcessFile(fragmentPath, fragment, ShaderType::Fragment, _defines) ||
		!CreateProgram(Program, { vertex,fragment })
		)
	{
		Release();
		return false;
	}
	return true;
}
bool Shader::CompileFromFile(const std::string& vertexPath, const std::string& geometryPath, const std::string& fragmentPath)
{
	Release();

	if (
		!ProcessFile(vertexPath, vertex, ShaderType::Vertex, _defines) ||
		!ProcessFile(geometryPath, geometry, ShaderType::Geometry, _defines) ||
		!ProcessFile(fragmentPath, fragment, ShaderType::Fragment, _defines) ||
		!CreateProgram(Program, { vertex, geometry,fragment })
		)
	{
		Release();
		return false;
	}
	return true;
}
bool Shader::CompileFromFile(const std::string& computePath)
{
	Release();

	if (
		!ProcessFile(computePath, compute, ShaderType::Compute, _defines) ||
		!CreateProgram(Program, { compute })
		)
	{
		Release();
		return false;
	}
	return true;
}

bool Shader::CompileFromString(const std::string& vertexCode, const std::string& fragmentCode)
{
	Release();

	if (
		!ProcessCode(vertexCode, vertex, ShaderType::Vertex, _defines) ||
		!ProcessCode(fragmentCode, fragment, ShaderType::Fragment, _defines) ||
		!CreateProgram(Program, { vertex, fragment })
		)
	{
		Release();
		return false;
	}
	return true;
}

bool Shader::CompileFromString(const std::string& vertexCode, const std::string& geometryCode, const std::string& fragmentCode)
{
	Release();

	if (
		!ProcessCode(vertexCode, vertex, ShaderType::Vertex, _defines) ||
		!ProcessCode(geometryCode, geometry, ShaderType::Geometry, _defines) ||
		!ProcessCode(fragmentCode, fragment, ShaderType::Fragment, _defines) ||
		!CreateProgram(Program, { vertex, geometry, fragment })
		)
	{
		Release();
		return false;
	}
	return true;
}

bool Shader::CompileFromString(const std::string& computeCode)
{
	Release();

	if (
		!ProcessCode(computeCode, compute, ShaderType::Compute, _defines) ||
		!CreateProgram(Program, { compute })
		)
	{
		Release();
		return false;
	}
	return true;
}

void Shader::Release()
{
	for (auto shader : { vertex, geometry, fragment, compute })
	{
		if (shader != 0)
		{
			if (this->Program != 0)
				glDetachShader(this->Program, shader);
			glDeleteShader(shader);
		}
	}

	if (this->Program != 0)
		glDeleteProgram(this->Program);

	vertex = 0;
	geometry = 0;
	fragment = 0;
	compute = 0;
	Program = 0;

	_location_Cache.clear();
}

void Shader::setBool(const std::string& name, bool value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform1i(location, (int)value);
}

void Shader::setInt(const std::string& name, int value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform1i(location, value);
}

void Shader::setUInt(const std::string& name, uint32_t value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform1ui(location, value);
}

void Shader::setFloat(const std::string& name, float value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform1f(location, value);
}

void Shader::setMat4(const std::string& name, const glm::mat4& value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setVec2(const std::string& name, const glm::vec2& value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform2f(location, value.x, value.y);
}

void Shader::setVec3(const std::string& name, const glm::vec3& value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform3f(location, value.x, value.y, value.z);
}

void Shader::setVec4(const std::string& name, const glm::vec4& value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform4f(location, value.x, value.y, value.z, value.w);
}

void Shader::setIVec2(const std::string& name, const glm::ivec2& value)
{
	GLint location = GetLocation(name);
	if (location < 0)
		return;
	glUniform2i(location, value.x, value.y);
}

void Shader::setTexture(const std::shared_ptr<Texture2D>& tex, const std::string& name, unsigned int slot)
{
	tex->Bind(slot);
	setInt(name, slot);
}

void Shader::setTexture(const std::unique_ptr<Texture2D>& tex, const std::string& name, unsigned int slot)
{
	tex->Bind(slot);
	setInt(name, slot);
}

void Shader::setTexture(const Texture2D& tex, const std::string& name, unsigned int slot)
{
	tex.Bind(slot);
	setInt(name, slot);
}

void Shader::setTexture(const std::shared_ptr<TextureCube>& tex, const std::string& name, unsigned int slot)
{
	tex->Bind(slot);
	setInt(name, slot);
}

void Shader::setTexture(const std::unique_ptr<TextureCube>& tex, const std::string& name, unsigned int slot)
{
	tex->Bind(slot);
	setInt(name, slot);
}

void Shader::setTexture(const TextureCube& tex, const std::string& name, unsigned int slot)
{
	tex.Bind(slot);
	setInt(name, slot);
}

void Shader::AddDefineMacro(const std::string& name, const std::string& value)
{
	_defines[name] = value;
}

void Shader::RemoveDefineMarco(const std::string& name)
{
	_defines.erase(name);
}

std::string Shader::GetDefineValue(const std::string& name)
{
	auto it = _defines.find(name);
	if (it != _defines.end())
		return it->second;
	return std::string();
}

bool Shader::bindSSBO(const std::string& binding_name, std::shared_ptr<SSBO> ssbo)
{
	if (!ssbo)
		return false;

	GLuint binding = 0;
	if (!FindBindingFromName(binding_name, binding))
		return false;

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo->GetID());

	SSBO_BindingToData[binding] = ssbo;
	SSBO_NameToBindingMap[binding_name] = binding;

	return true;
}

bool Shader::bindSSBO(GLuint binding, std::shared_ptr<SSBO> ssbo)
{
	if (!ssbo)
		return false;

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo->GetID());

	GLint boundBuffer = 0;
	glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, binding, &boundBuffer);
	if (boundBuffer != ssbo->GetID() || boundBuffer == 0)
		return false;

	SSBO_BindingToData[binding] = ssbo;

	return true;
}

std::shared_ptr<SSBO> Shader::TryGetSSBO(GLuint binding)
{
	auto it = SSBO_BindingToData.find(binding);
	if (it != SSBO_BindingToData.end())
		return it->second;

	auto ssbo = std::make_shared<SSBO>();
	if (bindSSBO(binding, ssbo))
		return ssbo;

	return nullptr;
}

std::shared_ptr<SSBO> Shader::TryGetSSBO(const std::string& binding_name)
{

	GLuint binding = 0;
	auto it = SSBO_NameToBindingMap.find(binding_name);
	if (it != SSBO_NameToBindingMap.end())
	{
		binding = it->second;
		return TryGetSSBO(binding);
	}

	if (FindBindingFromName(binding_name, binding))
	{
		SSBO_NameToBindingMap[binding_name] = binding;
		return TryGetSSBO(binding);
	}

	return nullptr;
}

std::shared_ptr<SSBO> Shader::FindSSBO(GLuint binding)
{
	auto it = SSBO_BindingToData.find(binding);
	if (it != SSBO_BindingToData.end())
		return it->second;
	return nullptr;
}

std::shared_ptr<SSBO> Shader::FindSSBO(const std::string& binding_name)
{
	GLuint binding = 0;
	auto it = SSBO_NameToBindingMap.find(binding_name);
	if (it != SSBO_NameToBindingMap.end())
	{
		binding = it->second;
		return FindSSBO(binding);
	}
	return nullptr;
}

GLuint Shader::GetProgram()
{
	return Program;
}

bool Shader::FindBindingFromName(const std::string& binding_name, GLuint& binding)
{
	GLuint blockIndex = glGetProgramResourceIndex(
		Program,
		GL_SHADER_STORAGE_BLOCK,
		binding_name.c_str()
	);

	if (blockIndex == GL_INVALID_INDEX)
		return false;

	GLenum prop = GL_BUFFER_BINDING;
	GLint temp = -1;
	glGetProgramResourceiv(
		Program,
		GL_SHADER_STORAGE_BLOCK,
		blockIndex,
		1,
		&prop,
		1,
		nullptr,
		&temp
	);
	if (temp == -1)
		return false;

	binding = temp;
	return true;
}

void Shader::Use()
{
	glUseProgram(this->Program);

	for (auto& [binding, ssbo] : SSBO_BindingToData)
	{
		if (ssbo)
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo->GetID());
	}
}

void Shader::Close()
{
	glUseProgram(0);
}

GLint Shader::GetLocation(const std::string& name)
{
	auto it = _location_Cache.find(name);
	if (it != _location_Cache.end())
		return it->second;

	GLint location = glGetUniformLocation(Program, name.c_str());
	_location_Cache[name] = location;
	return location;
}