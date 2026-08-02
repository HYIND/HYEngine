#include "OpenGLRenderEngine/Base/Texture2D.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>   
#include <iostream>			
#include "Manager/RenderContextManager.h"

class ThreadResidentContextManager
{
	struct BindlessData
	{
		uint32_t LastResidentVersion = 0;
		GLuint64 id;
	};

public:
	ThreadResidentContextManager() {};
	~ThreadResidentContextManager()
	{
		if (_ResidentInContext.empty())
			return;

		auto guard = THREADCONTEXT->GetBindGuard();
		for (auto& [tex, data] : _ResidentInContext)
		{
			if (data.id != 0)
				glMakeTextureHandleNonResidentARB(data.id);
		}
	};

	void ClearThreadBindlessData(const Texture2D* tex)
	{
		auto it = _ResidentInContext.find(tex);
		if (it == _ResidentInContext.end())
			return;

		auto BindlessID = it->second.id;
		if (BindlessID != 0)
		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glMakeTextureHandleNonResidentARB(BindlessID);
		}
		_ResidentInContext.erase(it);
	}

	GLuint64 GetThreadBindlessData(const Texture2D* tex, uint32_t version)
	{
		auto it = _ResidentInContext.find(tex);
		if (it != _ResidentInContext.end())
		{
			auto& bindlessdata = it->second;
			if (bindlessdata.LastResidentVersion == version)
				return it->second.id;
			else
			{
				auto BindlessID = it->second.id;
				if (BindlessID != 0)
				{
					auto guard = THREADCONTEXT->GetBindGuard();
					glMakeTextureHandleNonResidentARB(BindlessID);
				}
				_ResidentInContext.erase(it);
			}
		}

		auto texID = tex->GetID();
		if (texID != 0)
		{
			auto guard = THREADCONTEXT->GetBindGuard();
			auto BindlessID = glGetTextureHandleARB(texID);
			glMakeTextureHandleResidentARB(BindlessID);  // 常驻GPU
			_ResidentInContext[tex] = BindlessData{ version, BindlessID };
			return BindlessID;
		}
		return 0;
	}

private:
	std::unordered_map<const Texture2D*, BindlessData> _ResidentInContext;
};

thread_local ThreadResidentContextManager t_BindlessManager;

float GetAnisotropicTextureFiltering()
{
	static std::once_flag s_flag;
	static GLfloat flargest = 1.0f;

	std::call_once(s_flag, [&]()->void {
		auto guard = THREADCONTEXT->GetBindGuard();
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &flargest);
		});

	return std::min(4.0f, flargest);
}

void GetFormatAndType(unsigned int infernalFormat, unsigned int& format, unsigned int& type)
{

	switch (infernalFormat)
	{
	case GL_RGBA16F:
		format = GL_RGBA;
		type = GL_HALF_FLOAT;
		break;

	case GL_RGBA32F:
		format = GL_RGBA;
		type = GL_FLOAT;
		break;

	case GL_RGB16F:
		format = GL_RGB;
		type = GL_HALF_FLOAT;
		break;

	case GL_RGB32F:
		format = GL_RGB;
		type = GL_FLOAT;
		break;

	case GL_RG16F:
		format = GL_RG;
		type = GL_HALF_FLOAT;
		break;

	case GL_RG32F:
		format = GL_RG;
		type = GL_FLOAT;
		break;

	case GL_R16F:
		format = GL_RED;
		type = GL_HALF_FLOAT;
		break;

	case GL_R32F:
		format = GL_RED;
		type = GL_FLOAT;
		break;

	case GL_RGBA8:
	case GL_RGBA:
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		break;

	case GL_RGB8:
	case GL_RGB:
		format = GL_RGB;
		type = GL_UNSIGNED_BYTE;
		break;

	case GL_RG8:
	case GL_RG:
		format = GL_RG;
		type = GL_UNSIGNED_BYTE;
		break;

	case GL_R8:
	case GL_RED:
		format = GL_RED;
		type = GL_UNSIGNED_BYTE;
		break;

	case GL_DEPTH_COMPONENT:
		format = GL_DEPTH_COMPONENT;
		type = GL_FLOAT;
		break;

	case GL_DEPTH_COMPONENT16:
		format = GL_DEPTH_COMPONENT;
		type = GL_UNSIGNED_SHORT;
		break;

	case GL_DEPTH_COMPONENT24:
		format = GL_DEPTH_COMPONENT;
		type = GL_UNSIGNED_INT;
		break;

	case GL_DEPTH_COMPONENT32:
	case GL_DEPTH_COMPONENT32F:
		format = GL_DEPTH_COMPONENT;
		type = GL_FLOAT;
		break;

	case GL_DEPTH24_STENCIL8:
		format = GL_DEPTH_STENCIL;
		type = GL_UNSIGNED_INT_24_8;
		break;

	case GL_DEPTH32F_STENCIL8:
		format = GL_DEPTH_STENCIL;
		type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
		break;

	case GL_SRGB8:
		format = GL_RGB;
		type = GL_UNSIGNED_BYTE;
		break;

	case GL_SRGB8_ALPHA8:
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		break;

	default:
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		break;
	}

}

bool Texture2D::CopyTexture(const Texture2D& src, const Texture2D& dest)
{
	if (&src == &dest)
		return true;

	if (src.m_Width != dest.m_Width
		|| src.m_Height != dest.m_Height
		|| src.m_InternalFormat != dest.m_InternalFormat
		)
		return false;

	glCopyImageSubData(
		src.GetID(), GL_TEXTURE_2D, 0, 0, 0, 0,
		dest.GetID(), GL_TEXTURE_2D, 0, 0, 0, 0,
		src.m_Width, src.m_Height, 1
	);

	return true;
}

bool Texture2D::CopyTexture(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest)
{
	if (!src || !dest)
		return false;
	return CopyTexture(*src, *dest);
}

Texture2D::Texture2D(const std::string& filepath, bool gammaCorrection)
	: m_RendererID(0), m_Width(0), m_Height(0), m_InternalFormat(GL_RGBA8),
	m_MinFilter(GL_LINEAR), m_MagFilter(GL_LINEAR), m_WrapS(GL_REPEAT), m_WrapT(GL_REPEAT), m_Anisotropy(false)
{
	LoadFromFile(filepath, gammaCorrection);
}

Texture2D::Texture2D(int width, int height, unsigned int internalFormat)
	: m_RendererID(0), m_Width(width), m_Height(height), m_InternalFormat(internalFormat), m_Anisotropy(false) {
	CreateEmpty(width, height, internalFormat);
}

Texture2D::~Texture2D()
{
	if (IsEmpty())
		return;

	auto guard = THREADCONTEXT->GetBindGuard();
	t_BindlessManager.ClearThreadBindlessData(this);
	glDeleteTextures(1, &m_RendererID);
}

void Texture2D::Bind(unsigned int slot) const {
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void Texture2D::Unbind() const {
	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D& Texture2D::SetFiltering(unsigned int filter)
{
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	m_MinFilter = filter;
	m_MagFilter = filter;
	return *this;
}

Texture2D& Texture2D::SetFiltering(unsigned int minFilter, unsigned int magFilter) {
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
	m_MinFilter = minFilter;
	m_MagFilter = magFilter;
	return *this;
}

Texture2D& Texture2D::SetWrapping(unsigned int wrap)
{
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	m_WrapS = wrap;
	m_WrapT = wrap;
	return *this;
}

Texture2D& Texture2D::SetWrapping(unsigned int wrapS, unsigned int wrapT) {
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
	m_WrapS = wrapS;
	m_WrapT = wrapT;
	return *this;
}

Texture2D& Texture2D::SetAnisotropy(bool anisotropy)
{
	if (m_Anisotropy == anisotropy)
		return *this;

	m_Anisotropy = anisotropy;
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_Anisotropy ? GetAnisotropicTextureFiltering() : 1.0f);
	return *this;
}

// ---------- 获取属性 ----------
int Texture2D::GetInternalFormat() const
{
	return m_InternalFormat;
}

unsigned int Texture2D::GetFormat() const
{
	return m_Format;
}

unsigned int Texture2D::GetType() const
{
	return m_Type;
}

unsigned int Texture2D::GetMinFilter() const
{
	return m_MinFilter;
}

unsigned int Texture2D::GetMagFilter() const
{
	return m_MagFilter;
}

unsigned int Texture2D::GetWrapS() const
{
	return m_WrapS;
}

unsigned int Texture2D::GetWrapT() const
{
	return m_WrapT;
}

unsigned int Texture2D::GetID() const
{
	return m_RendererID;
}

GLuint64 Texture2D::GetBindlessID() const
{
	return t_BindlessManager.GetThreadBindlessData(this, _BindlessVersion.load());
}

uint32_t Texture2D::GetWidth() const
{
	return m_Width;
}

uint32_t Texture2D::GetHeight() const
{
	return m_Height;
}

glm::u32vec2 Texture2D::GetSize() const
{
	return glm::u32vec2(m_Width, m_Height);
}

void Texture2D::UpdateTextureData(void* data, int format, int type) {
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height,
		format, type, data);
}

bool Texture2D::IsEmpty() const
{
	return m_RendererID == 0;
}

bool Texture2D::LoadFromFile(const std::string& filepath, bool gammaCorrection)
{
	auto guard = THREADCONTEXT->GetBindGuard();

	if (!IsEmpty())
	{
		_BindlessVersion++;
		t_BindlessManager.ClearThreadBindlessData(this);
		glDeleteTextures(1, &m_RendererID);
		m_RendererID = 0;
		m_Width = 0;
		m_Height = 0;
	}

	int width, height, nrComponents;
	unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &nrComponents, 0);
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

		glGenTextures(1, &m_RendererID);
		glBindTexture(GL_TEXTURE_2D, m_RendererID);
		glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_MinFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_MagFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_WrapS);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_WrapT);

		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_Anisotropy ? GetAnisotropicTextureFiltering() : 1.0f);

		stbi_image_free(data);

		m_Width = width;
		m_Height = height;
		m_InternalFormat = internalFormat;

		return true;
	}
	else
	{
		//std::cout << "Texture failed to load at path: " << filepath << std::endl;
		stbi_image_free(data);
		return false;
	}
}

void Texture2D::Resize(uint32_t width, uint32_t height)
{
	if (width == m_Width && height == m_Height)
		return;
	CreateEmpty(width, height, m_InternalFormat);
}


// ------------------------------------------------------------
// 创建空纹理（用于 RenderTarget 或者后续动态更新）
// ------------------------------------------------------------
void Texture2D::CreateEmpty(int width, int height, unsigned int internalFormat) {
	auto guard = THREADCONTEXT->GetBindGuard();

	if (!IsEmpty())
	{
		_BindlessVersion++;
		t_BindlessManager.ClearThreadBindlessData(this);
		glDeleteTextures(1, &m_RendererID);
		m_RendererID = 0;
		m_Width = 0;
		m_Height = 0;
	}

	m_Width = width;
	m_Height = height;
	m_InternalFormat = internalFormat;

	glGenTextures(1, &m_RendererID);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);

	GetFormatAndType(m_InternalFormat, m_Format, m_Type);

	// 分配显存空间（不填充数据）
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
		m_Format, m_Type, nullptr);

	// 设置默认参数
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_MinFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_MagFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_WrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_WrapT);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_Anisotropy ? GetAnisotropicTextureFiltering() : 1.0f);
}
