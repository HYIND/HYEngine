#include "OpenGLRenderEngine/Base/Texture2D.h"
#include "OpenGLRenderEngine/OpenGLRenderContextManager.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>   
#include <iostream>			

class GlobalResidentContextManager
{
private:
	struct BindlessData
	{
		uint32_t LastResidentVersion = 0;
		GLuint64 id;
		bool isDirty = false;
	};

public:
	static GlobalResidentContextManager* Instance()
	{
		static GlobalResidentContextManager* instance = new GlobalResidentContextManager();
		return instance;
	}

	void SignDirtyThreadBindlessData(const Texture2D* tex)
	{
		auto curthread = std::this_thread::get_id();

		for (auto& [threadid, threadResident] : _GlobalResident)
		{
			auto it = threadResident.find(tex);
			if (it == threadResident.end())
				continue;
			if (threadid != curthread)
				it->second.isDirty = true;
			else
			{
				auto guard = THREADCONTEXT->GetBindGuard();
				glMakeTextureHandleNonResidentARB(it->second.id);
				threadResident.erase(it);
			}
		}
	}

	void ClearDirtyThreadResidentTexture(std::thread::id threadid)
	{
		if (_GlobalResident.find(threadid) == _GlobalResident.end())
			return;

		auto guard = THREADCONTEXT->GetBindGuard();
		auto& threadResident = _GlobalResident[threadid];

		for (auto it = threadResident.begin(); it != threadResident.end();)
		{
			auto& bindlessData = it->second;
			if (!bindlessData.isDirty)
			{
				it++;
				continue;
			}
			glMakeTextureHandleNonResidentARB(bindlessData.id);
			it = threadResident.erase(it);
		}
	}

	GLuint64 GetThreadBindlessData(std::thread::id threadid, const Texture2D* tex, uint32_t version)
	{
		auto& threadResident = _GlobalResident[threadid];

		auto it = threadResident.find(tex);
		if (it != threadResident.end())
		{
			auto& bindlessdata = it->second;
			if (bindlessdata.LastResidentVersion == version && !bindlessdata.isDirty)
				return it->second.id;
			else
			{
				auto BindlessID = it->second.id;
				if (BindlessID != 0)
				{
					auto guard = THREADCONTEXT->GetBindGuard();
					glMakeTextureHandleNonResidentARB(BindlessID);
				}
				threadResident.erase(it);
			}
		}

		auto texID = tex->GetID();
		if (texID != 0)
		{
			auto guard = THREADCONTEXT->GetBindGuard();
			auto BindlessID = glGetTextureHandleARB(texID);
			glMakeTextureHandleResidentARB(BindlessID);  // 常驻GPU
			threadResident[tex] = BindlessData{ version, BindlessID };
			return BindlessID;
		}
		return 0;
	}

private:
	GlobalResidentContextManager() {}

private:
	std::unordered_map<std::thread::id, std::unordered_map<const Texture2D*, BindlessData>> _GlobalResident;
};

class ThreadResidentProxy
{
public:
	~ThreadResidentProxy() {
		ClearDirtyThreadResidentTexture();
	}
public:
	GLuint64 GetThreadBindlessData(const Texture2D* tex, uint32_t version) {
		return GlobalResidentContextManager::Instance()->GetThreadBindlessData(std::this_thread::get_id(), tex, version);
	}
	void SignDirtyBindlessData(const Texture2D* tex) {
		GlobalResidentContextManager::Instance()->SignDirtyThreadBindlessData(tex);
	}
	void ClearDirtyThreadResidentTexture() {
		GlobalResidentContextManager::Instance()->ClearDirtyThreadResidentTexture(std::this_thread::get_id());
	}
};

thread_local ThreadResidentProxy t_ThreadResidentProxy;


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

bool IsDepthFormat(unsigned int format)
{
	switch (format) {
	case GL_DEPTH_COMPONENT:
	case GL_DEPTH_COMPONENT16:
	case GL_DEPTH_COMPONENT24:
	case GL_DEPTH_COMPONENT32:
	case GL_DEPTH_COMPONENT32F:
	case GL_DEPTH_STENCIL:
	case GL_DEPTH24_STENCIL8:
	case GL_DEPTH32F_STENCIL8:
		return true;
	default:
		return false;
	}
}

bool IsSingleChannelFloat(unsigned int format)
{
	switch (format) {
	case GL_R32F:
	case GL_R16F:
	case GL_R8:
	case GL_R16:
	case GL_R32I:
	case GL_R32UI:
		return true;
	default:
		return false;
	}
}

int GetChannelCount(unsigned int format)
{
	switch (format) {
		// 单通道
	case GL_R8:
	case GL_R16:
	case GL_R16F:
	case GL_R32F:
	case GL_R32I:
	case GL_R32UI:
	case GL_DEPTH_COMPONENT:
	case GL_DEPTH_COMPONENT16:
	case GL_DEPTH_COMPONENT24:
	case GL_DEPTH_COMPONENT32:
	case GL_DEPTH_COMPONENT32F:
		return 1;

		// 双通道
	case GL_RG8:
	case GL_RG16:
	case GL_RG16F:
	case GL_RG32F:
		return 2;

		// 三通道
	case GL_RGB8:
	case GL_RGB16:
	case GL_RGB16F:
	case GL_RGB32F:
		return 3;

		// 四通道
	case GL_RGBA8:
	case GL_RGBA16:
	case GL_RGBA16F:
	case GL_RGBA32F:
		return 4;

		// 深度+模板
	case GL_DEPTH_STENCIL:
	case GL_DEPTH24_STENCIL8:
	case GL_DEPTH32F_STENCIL8:
		return 2;  // 深度+模板

	default:
		return 0;  // 未知格式
	}
}

unsigned int GetComponentType(unsigned int format)
{
	switch (format) {
		// 浮点类型
	case GL_R16F:
	case GL_RG16F:
	case GL_RGB16F:
	case GL_RGBA16F:
	case GL_R32F:
	case GL_RG32F:
	case GL_RGB32F:
	case GL_RGBA32F:
	case GL_DEPTH_COMPONENT32F:
		return GL_FLOAT;

		// 整数类型
	case GL_R32I:
	case GL_R32UI:
		return GL_INT;

		// 无符号整数
	case GL_R8:
	case GL_R16:
	case GL_RG8:
	case GL_RG16:
	case GL_RGB8:
	case GL_RGB16:
	case GL_RGBA8:
	case GL_RGBA16:
	case GL_DEPTH_COMPONENT16:
	case GL_DEPTH_COMPONENT24:
	case GL_DEPTH_COMPONENT32:
	case GL_DEPTH24_STENCIL8:
	default:
		return GL_UNSIGNED_BYTE;  // 默认无符号
	}
}

bool IsCompatibleFormat(unsigned int f1, unsigned int f2)
{
	if (f1 == f2) return true;

	if (IsDepthFormat(f1) && IsDepthFormat(f2)) return true;

	if (IsDepthFormat(f1) && f2 == GL_R32F) return true;
	if (f1 == GL_R32F && IsDepthFormat(f2)) return true;

	if (IsSingleChannelFloat(f1) && IsSingleChannelFloat(f2)) return true;

	if (GetChannelCount(f1) == GetChannelCount(f2) &&
		GetComponentType(f1) == GetComponentType(f2)) return true;

	return false;
}

void Texture2D::ClearDirtyThreadResidentTexture()
{
	t_ThreadResidentProxy.ClearDirtyThreadResidentTexture();
}

bool Texture2D::CopyTexture(const Texture2D& src, const Texture2D& dest, uint32_t srcLevel, uint32_t destLevel)
{
	if (&src == &dest && srcLevel == destLevel)
		return true;

	if (src.m_MaxLevel <= srcLevel || dest.m_MaxLevel <= destLevel)
		return false;

	if (!IsCompatibleFormat(src.m_InternalFormat, dest.m_InternalFormat))
		return false;

	uint32_t srcWidth = std::max(1u, src.m_Width >> srcLevel);
	uint32_t srcHeight = std::max(1u, src.m_Height >> srcLevel);

	uint32_t destWidth = std::max(1u, dest.m_Width >> destLevel);
	uint32_t destHeight = std::max(1u, dest.m_Height >> destLevel);

	if (srcWidth != destWidth || srcHeight != destHeight)
		return false;


	glCopyImageSubData(
		src.GetID(), GL_TEXTURE_2D, srcLevel, 0, 0, 0,
		dest.GetID(), GL_TEXTURE_2D, destLevel, 0, 0, 0,
		srcWidth, srcHeight, 1
	);

	return true;
}

bool Texture2D::CopyTexture(const std::shared_ptr<Texture2D>& src, const std::shared_ptr<Texture2D>& dest, uint32_t srcLevel, uint32_t destLevel)
{
	if (!src || !dest)
		return false;
	return CopyTexture(*src, *dest, srcLevel, destLevel);
}

Texture2D::Texture2D(const std::string& filepath, bool gammaCorrection)
	: m_RendererID(0), m_Width(0), m_Height(0), m_InternalFormat(GL_RGBA8),
	m_MinFilter(GL_LINEAR), m_MagFilter(GL_LINEAR), m_WrapS(GL_REPEAT), m_WrapT(GL_REPEAT), m_Anisotropy(false), m_MaxLevel(1)
{
	LoadFromFile(filepath, gammaCorrection);
}

Texture2D::Texture2D(int width, int height, unsigned int internalFormat, uint32_t level)
	: m_RendererID(0), m_Width(width), m_Height(height), m_InternalFormat(internalFormat), m_Anisotropy(false), m_MaxLevel(std::max(1u, level)) {
	CreateEmpty(width, height, internalFormat, level);
}

Texture2D::~Texture2D()
{
	if (IsEmpty())
		return;

	auto guard = THREADCONTEXT->GetBindGuard();
	t_ThreadResidentProxy.SignDirtyBindlessData(this);
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
	return t_ThreadResidentProxy.GetThreadBindlessData(this, _BindlessVersion.load());
}

uint32_t Texture2D::GetWidth() const
{
	return m_Width;
}

uint32_t Texture2D::GetHeight() const
{
	return m_Height;
}

uint32_t Texture2D::GetMaxLevel() const
{
	return m_MaxLevel;
}

glm::u32vec2 Texture2D::GetSize() const
{
	return glm::u32vec2(m_Width, m_Height);
}

TextureConfig Texture2D::GetConfig() const
{
	TextureConfig config;
	config.wrapS = m_WrapS;
	config.wrapT = m_WrapT;
	config.minFilter = m_MinFilter;
	config.magFilter = m_MagFilter;
	config.anisotropy = m_Anisotropy;
	config.gammaCorrection = m_gammaCorrect;
	return config;
}


void Texture2D::UpdateTextureData(void* data, int format, int type, uint32_t level) {
	auto guard = THREADCONTEXT->GetBindGuard();
	Bind(0);
	glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, m_Width, m_Height,
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
		t_ThreadResidentProxy.SignDirtyBindlessData(this);
		glDeleteTextures(1, &m_RendererID);
		m_RendererID = 0;
		m_Width = 0;
		m_Height = 0;
		m_gammaCorrect = false;
	}

	m_gammaCorrect = gammaCorrection;

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
		m_MaxLevel = 1;
		GetFormatAndType(m_InternalFormat, m_Format, m_Type);

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
	CreateEmpty(width, height, m_InternalFormat, m_MaxLevel);
}


// ------------------------------------------------------------
// 创建空纹理（用于 RenderTarget 或者后续动态更新）
// ------------------------------------------------------------
void Texture2D::CreateEmpty(int width, int height, unsigned int internalFormat, uint32_t maxLevel) {
	auto guard = THREADCONTEXT->GetBindGuard();

	if (!IsEmpty())
	{
		_BindlessVersion++;
		t_ThreadResidentProxy.SignDirtyBindlessData(this);
		glDeleteTextures(1, &m_RendererID);
		m_RendererID = 0;
		m_Width = 0;
		m_Height = 0;
		m_gammaCorrect = false;
	}

	m_Width = width;
	m_Height = height;
	m_InternalFormat = internalFormat;
	m_MaxLevel = maxLevel;

	glGenTextures(1, &m_RendererID);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);

	GetFormatAndType(m_InternalFormat, m_Format, m_Type);

	if (m_MaxLevel <= 1)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
			m_Format, m_Type, nullptr);
	}
	else
	{
		//for (int level = 0; level < m_MaxLevel; level++) {
		//	int w = std::max(1, width >> level);
		//	int h = std::max(1, height >> level);
		//	glTexImage2D(GL_TEXTURE_2D, level, internalFormat, w, h, 0, m_Format, m_Type, nullptr);
		//}

		glTexStorage2D(GL_TEXTURE_2D, m_MaxLevel, internalFormat, width, height);
	}

	//if (m_InternalFormat == GL_R32F)
	//{
	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
	//}

	// 设置默认参数
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_MinFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_MagFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_WrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_WrapT);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_Anisotropy ? GetAnisotropicTextureFiltering() : 1.0f);

}

bool TextureConfig::operator==(const TextureConfig& other)
{
	return minFilter == other.minFilter
		&& magFilter == other.magFilter
		&& wrapS == other.wrapS
		&& wrapT == other.wrapT
		&& anisotropy == other.anisotropy
		&& gammaCorrection == other.gammaCorrection;
}

bool TextureConfig::operator!=(const TextureConfig& other)
{
	return *this == other;
}
