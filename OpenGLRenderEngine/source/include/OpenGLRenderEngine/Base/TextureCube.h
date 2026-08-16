#pragma once

#include <array>
#include <atomic>
#include <string>
#include <memory>
#include "GL\glew.h"
#include "glm/glm.hpp"

class TextureCube
{
public:
	static std::shared_ptr<TextureCube> GetPlaceholderCubeMap();

public:
	TextureCube(const std::array<std::string, 6>& filepaths, bool gammaCorrection = true);	// 从文件加载纹理

	~TextureCube();

public:
	TextureCube& SetFiltering(unsigned int filter);
	TextureCube& SetFiltering(unsigned int minFilter, unsigned int magFilter);
	TextureCube& SetWrapping(unsigned int wrap);
	TextureCube& SetWrapping(unsigned int wrapS, unsigned int wrapT);
	TextureCube& SetAnisotropy(bool anisotropy);

	bool LoadFromFile(const std::array<std::string, 6>& filepaths, bool gammaCorrection);

public:
	void Bind(unsigned int slot = 0) const;
	void Unbind() const;

	unsigned int GetID() const;
	uint32_t GetWidth() const;
	uint32_t GetHeight() const;
	uint32_t GetMaxLevel() const;
	glm::u32vec2 GetSize() const;
	bool IsEmpty() const;

	unsigned int GetMinFilter() const;
	unsigned int GetMagFilter() const;
	unsigned int GetWrapS() const;
	unsigned int GetWrapT() const;
	unsigned int GetWrapR() const;

private:
	unsigned int m_RendererID;

	uint32_t m_Width;
	uint32_t m_Height;

	unsigned int m_MinFilter;
	unsigned int m_MagFilter;

	unsigned int m_WrapS;
	unsigned int m_WrapT;
	unsigned int m_WrapR;
};
