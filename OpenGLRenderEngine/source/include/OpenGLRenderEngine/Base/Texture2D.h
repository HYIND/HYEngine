#pragma once

#include <atomic>
#include <string>
#include <memory>
#include "GL\glew.h"
#include "glm/glm.hpp"

struct TextureConfig
{
	unsigned int minFilter = GL_NEAREST;
	unsigned int magFilter = GL_NEAREST;
	unsigned int wrapS = GL_CLAMP_TO_EDGE;
	unsigned int wrapT = GL_CLAMP_TO_EDGE;
	bool anisotropy = false;
	bool gammaCorrection = false;

	bool operator==(const TextureConfig& other);
	bool operator!=(const TextureConfig& other);
};

class Texture2D
{
public:
	static void ClearDirtyThreadResidentTexture();

public:
	static bool CopyTexture(const Texture2D& src, const Texture2D& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);
	static bool CopyTexture(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel = 0, uint32_t destLevel = 0);

public:
	Texture2D(const std::string& filepath, bool gammaCorrection);											// 从文件加载纹理
	Texture2D(int width, int height, unsigned int internalFormat = GL_RGBA8, uint32_t maxLevel = 1);		// 创建空纹理

	~Texture2D();

public:
	Texture2D& SetFiltering(unsigned int filter);
	Texture2D& SetFiltering(unsigned int minFilter, unsigned int magFilter);
	Texture2D& SetWrapping(unsigned int wrap);
	Texture2D& SetWrapping(unsigned int wrapS, unsigned int wrapT);
	Texture2D& SetAnisotropy(bool anisotropy);

	void UpdateTextureData(void* data, int format, int type, uint32_t level = 0);
	bool LoadFromFile(const std::string& filepath, bool gammaCorrection);

	void Resize(uint32_t width, uint32_t height);			// 不保留数据！

public:
	void Bind(unsigned int slot = 0) const;										// slot 是纹理单元编号（0~31）
	void Unbind() const;

	unsigned int GetID() const;
	GLuint64 GetBindlessID() const;
	uint32_t GetWidth() const;
	uint32_t GetHeight() const;
	uint32_t GetMaxLevel() const;
	glm::u32vec2 GetSize() const;
	bool IsEmpty() const;

	int GetInternalFormat() const;
	unsigned int GetFormat() const;
	unsigned int GetType() const;
	unsigned int GetMinFilter() const;
	unsigned int GetMagFilter() const;
	unsigned int GetWrapS() const;
	unsigned int GetWrapT() const;

	TextureConfig GetConfig() const;

private:
	void CreateEmpty(int width, int height, unsigned int internalFormat, uint32_t level);

private:
	unsigned int m_RendererID;
	std::atomic<uint32_t> _BindlessVersion{ 0 };

	uint32_t m_Width;
	uint32_t m_Height;

	uint32_t m_MaxLevel;

	unsigned int m_InternalFormat;  // 内部格式，如 GL_RGBA8
	unsigned int m_Format;
	unsigned int m_Type;

	unsigned int m_MinFilter;
	unsigned int m_MagFilter;

	unsigned int m_WrapS;
	unsigned int m_WrapT;

	bool m_Anisotropy;
	bool m_gammaCorrect;
};

