#pragma once

#define GLEW_STATIC
#include"GL\glew.h"
#include"glm\glm.hpp"
#include <map>
#include <memory>
#include <string>
#include <concepts>
#include "Texture2D.h"

//SSBO管理
class SSBO
{
public:
	SSBO(size_t size = 0);
	~SSBO();

	void SetSize(size_t size);
	void WriteData(const void* data, size_t size, size_t offset = 0);

	GLuint GetID();
	size_t GetSize();

private:
	void Need();

private:
	GLuint _ssboId;
	size_t _size;
};

template<typename T>
concept StringConvertable = !std::is_same_v<T, std::string>
&& requires(const T& value) {
	{ std::to_string(value) } -> std::convertible_to<std::string>;
};

class Shader
{
public:
	Shader();
	Shader(const std::string& vertexPath, const std::string& fragmentPath);
	Shader(const std::string& vertexPath, const std::string& geometryPath, const std::string& fragmentPath);
	Shader(const std::string& computePath);

	~Shader();

	bool CompileFromFile(const std::string& vertexPath, const std::string& fragmentPath);
	bool CompileFromFile(const std::string& vertexPath, const std::string& geometryPath, const std::string& fragmentPath);
	bool CompileFromFile(const std::string& computePath);

	bool CompileFromString(const std::string& vertexCode, const std::string& fragmentCode);
	bool CompileFromString(const std::string& vertexCode, const std::string& geometryCode, const std::string& fragmentCode);
	bool CompileFromString(const std::string& computeCode);

	void Release();

	void Use();
	void Close();

	void setBool(const std::string& name, bool value);
	void setInt(const std::string& name, int value);
	void setUInt(const std::string& name, uint32_t value);
	void setFloat(const std::string& name, float value);
	void setMat4(const std::string& name, const glm::mat4& value);
	void setVec2(const std::string& name, const glm::vec2& value);
	void setVec3(const std::string& name, const glm::vec3& value);
	void setVec4(const std::string& name, const glm::vec4& value);

	void setIVec2(const std::string& name, const glm::ivec2& value);

	void setTexture(const std::shared_ptr<Texture2D>& tex, const std::string& name, unsigned int slot);
	void setTexture(const std::unique_ptr<Texture2D>& tex, const std::string& name, unsigned int slot);
	void setTexture(const Texture2D& tex, const std::string& name, unsigned int slot);

public:
	template <StringConvertable T>
	void AddDefineMacro(const std::string& name, const T& value) { AddDefineMacro(name, std::to_string(value)); }
	void AddDefineMacro(const std::string& name, const std::string& value);
	void RemoveDefineMarco(const std::string& name);
	std::string GetDefineValue(const std::string& name);

public:
	//SSBO关联
	//以下操作成功的前提都是绑定点或者绑定名有效
	bool bindSSBO(const std::string& binding_name, std::shared_ptr<SSBO> ssbo);	//返回绑定结果(绑定点不存在则失败)
	bool bindSSBO(GLuint binding, std::shared_ptr<SSBO> ssbo);					//返回绑定结果(绑定点不存在则失败)

	std::shared_ptr<SSBO> TryGetSSBO(GLuint binding);							//尝试获取绑定点上的SSBO，不存在SSBO时，如果绑定点有效，则创建一个SSBO，如果无效则返回nullptr
	std::shared_ptr<SSBO> TryGetSSBO(const std::string& binding_name);			//尝试获取绑定点上的SSBO，不存在SSBO时，如果绑定点有效，则创建一个SSBO，如果无效则返回nullptr

	std::shared_ptr<SSBO> FindSSBO(GLuint binding);								//和TryGetSSBO的区别在于不会自动创建SSBO
	std::shared_ptr<SSBO> FindSSBO(const std::string& binding_name);			//和TryGetSSBO的区别在于不会自动创建SSBO

public:
	GLuint GetProgram();

private:
	bool FindBindingFromName(const std::string& binding_name, GLuint& binding);
	GLint GetLocation(const std::string& name);

private:
	GLuint vertex, geometry, fragment, compute;
	GLuint Program;

	std::map<std::string, GLint> _location_Cache;

	std::map<GLuint, std::shared_ptr<SSBO>> SSBO_BindingToData;	//只记录有效绑定，即shader中实际存在的有效绑定点
	std::map<std::string, GLuint> SSBO_NameToBindingMap;		//只记录有效绑定

	std::map<std::string, std::string> _defines;
};
