#include "OpenGLRenderEngine/Base/TextureCube.h"
#include "OpenGLRenderEngine/OpenGLRenderContextManager.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>   
#include <iostream>			

std::shared_ptr<TextureCube> TextureCube::GetPlaceholderCubeMap()
{
	static std::shared_ptr<TextureCube> textureCube;
	if (textureCube->IsEmpty())
	{
		GLuint textureID;
		glGenTextures(1, &textureID);
		glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

		for (GLuint i = 0; i < 6; ++i)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		textureCube->m_RendererID = textureID;
		textureCube->m_Width = 1;
		textureCube->m_Height = 1;
		textureCube->m_MinFilter = GL_NEAREST;
		textureCube->m_MagFilter = GL_NEAREST;
		textureCube->m_WrapS = GL_CLAMP_TO_EDGE;
		textureCube->m_WrapS = GL_CLAMP_TO_EDGE;
		textureCube->m_WrapS = GL_CLAMP_TO_EDGE;
	}
	return textureCube;
}


TextureCube::TextureCube(const std::array<std::string, 6>& filepaths, bool gammaCorrection)
	: m_RendererID(0), m_Width(0), m_Height(0),
	m_MinFilter(GL_LINEAR), m_MagFilter(GL_LINEAR),
	m_WrapS(GL_CLAMP_TO_EDGE), m_WrapT(GL_CLAMP_TO_EDGE), m_WrapR(GL_CLAMP_TO_EDGE)
{
	LoadFromFile(filepaths, gammaCorrection);
}

TextureCube::~TextureCube()
{
	if (IsEmpty())
		return;

	auto guard = THREADCONTEXT->GetBindGuard();
	glDeleteTextures(1, &m_RendererID);
}

void TextureCube::Bind(unsigned int slot) const {
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
}

void TextureCube::Unbind() const {
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

TextureCube& TextureCube::SetFiltering(unsigned int filter)
{
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filter);
	m_MinFilter = filter;
	m_MagFilter = filter;
	return *this;
}

TextureCube& TextureCube::SetFiltering(unsigned int minFilter, unsigned int magFilter) {
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, magFilter);
	m_MinFilter = minFilter;
	m_MagFilter = magFilter;
	return *this;
}

TextureCube& TextureCube::SetWrapping(unsigned int wrap)
{
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	m_WrapS = wrap;
	m_WrapT = wrap;
	return *this;
}

TextureCube& TextureCube::SetWrapping(unsigned int wrapS, unsigned int wrapT) {
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
	m_WrapS = wrapS;
	m_WrapT = wrapT;
	return *this;
}

// ---------- 获取属性 ----------

unsigned int TextureCube::GetMinFilter() const
{
	return m_MinFilter;
}

unsigned int TextureCube::GetMagFilter() const
{
	return m_MagFilter;
}

unsigned int TextureCube::GetWrapS() const
{
	return m_WrapS;
}

unsigned int TextureCube::GetWrapT() const
{
	return m_WrapT;
}

unsigned int TextureCube::GetWrapR() const
{
	return m_WrapR;
}

unsigned int TextureCube::GetID() const
{
	return m_RendererID;
}

uint32_t TextureCube::GetWidth() const
{
	return m_Width;
}

uint32_t TextureCube::GetHeight() const
{
	return m_Height;
}

glm::u32vec2 TextureCube::GetSize() const
{
	return glm::u32vec2(m_Width, m_Height);
}

bool TextureCube::IsEmpty() const
{
	return m_RendererID == 0;
}

bool TextureCube::LoadFromFile(const std::array<std::string, 6>& filepaths, bool gammaCorrection)
{
	auto guard = THREADCONTEXT->GetBindGuard();

	if (!IsEmpty())
	{
		glDeleteTextures(1, &m_RendererID);
		m_RendererID = 0;
		m_Width = 0;
		m_Height = 0;
	}

	unsigned int textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

	int width, height, nrComponents;

	for (unsigned int i = 0; i < filepaths.size(); i++)
	{
		unsigned char* data = stbi_load(filepaths[i].c_str(), &width, &height, &nrComponents, 0);
		if (data)
		{
			GLenum internalFormat;
			GLenum dataFormat;
			if (nrComponents == 1)
			{
				internalFormat = dataFormat = GL_RED;
			}
			else if (nrComponents == 3)
			{
				internalFormat = gammaCorrection ? GL_SRGB : GL_RGB;
				dataFormat = GL_RGB;
			}
			else if (nrComponents == 4)
			{
				internalFormat = gammaCorrection ? GL_SRGB_ALPHA : GL_RGBA;
				dataFormat = GL_RGBA;
			}

			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
				0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data
			);
		}
		else
		{
			std::cout << "Cubemap texture failed to load at path: " << filepaths[i] << std::endl;
		}
		if (data)
			stbi_image_free(data);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, m_MinFilter);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, m_MagFilter);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, m_WrapS);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, m_WrapT);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, m_WrapR);

	m_Width = width;
	m_Height = height;
	m_RendererID = textureID;

	return true;
}
