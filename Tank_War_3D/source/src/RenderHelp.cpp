#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/Base/Particle.h"
#include "Manager/ResourceManager.h"
#include <glm/gtx/matrix_decompose.hpp>
#include "ThreadPool.h"
#include <execution>

#include "OpenGLRenderEngine/General/GPUTimer.h"

#ifdef far
#undef far
#endif

#ifdef near
#undef near
#endif

struct alignas(16) DirLightMetaInfo {
	alignas(16) glm::vec3 direction;
	alignas(16) glm::vec3 color;

	float luxIntensity;

	int cascadeFirst;
	int cascadeCount;
};

struct alignas(16) DirLightCascadeInfo {
	alignas(16) glm::mat4 lightSpaceMatrix;
	int atlasX;
	int atlasY;
	int atlasWidth;
	int atlasHeight;
	float cascadePlaneDistance;
};

struct SpotLightMetaInfo {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec3 direction;

	float cdIntensity;

	float cutOff;
	float outerCutOff;

	float radius;

	int atlasX;
	int atlasY;
	int atlasWidth;
	int atlasHeight;

	alignas(16) glm::mat4 lightSpaceMatrix;
};

struct alignas(16) PointLightMetaInfo {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec2 atlasStart[6];
	alignas(16) glm::vec2 atlasSize[6];
	float cdIntensity;
	float radius;
};

std::shared_ptr<Model> GetCubeModel(const glm::vec3& scale, float textureScale)
{
	auto guard = THREADCONTEXT->GetBindGuard();

	static std::vector<glm::vec3> cube_vertices = {
				glm::vec3(-1,-1,-1),
				glm::vec3(1,1,-1),
				glm::vec3(1,-1,-1),
				glm::vec3(-1,1,-1),
				glm::vec3(-1,-1,1),
				glm::vec3(1,-1,1),
				glm::vec3(1,1,1),
				glm::vec3(-1,1,1)
	};
	static std::vector<unsigned int> cube_indices = {
		0,1,2,
		1,0,3,
		4,5,6,
		6,7,4,
		7,3,0,
		0,4,7,
		6,2,1,
		2,6,5,
		0,2,5,
		5,4,0,
		3,6,1,
		6,3,7
	};
	static std::vector<glm::vec2> cubeTextureCoords = {
		glm::vec2(0.0f, 0.0f),
		glm::vec2(1.0f, 1.0f),
		glm::vec2(1.0f, 0.0f),
		glm::vec2(1.0f, 1.0f),
		glm::vec2(0.0f, 0.0f),
		glm::vec2(0.0f, 1.0f),

		//glm::vec2(1, 1),
		//glm::vec2(0, 0),
		//glm::vec2(0, 1),
		//glm::vec2(0, 0),
		//glm::vec2(1, 1),
		//glm::vec2(1, 0),
	};
	static std::vector<glm::vec3> cubeNormal =
	{
		glm::vec3(0.0f, 0.0f, -1.0f),
		glm::vec3(0.0f, 0.0f, 1.0f),
		glm::vec3(-1.0f, 0.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, -1.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f)
	};

	std::vector<Vertex> vertex;
	static std::vector<unsigned int> indices;
	for (int face = 0; face < 6; face++)
	{
		glm::vec3 textureCoordMask = glm::vec3(1) - glm::abs(cubeNormal[face]);
		for (int i = 0; i < 6; i++)
		{
			Vertex temp;
			int index = cube_indices[6 * face + i];
			temp.Position = cube_vertices[index];

			glm::vec2 texcoord;
			glm::vec3 tempMask = textureCoordMask * cube_vertices[index];
			if (tempMask.x == 0) texcoord = glm::vec2(tempMask.z, tempMask.y);
			else if (tempMask.y == 0) texcoord = glm::vec2(tempMask.x, tempMask.z);
			else if (tempMask.z == 0) texcoord = glm::vec2(tempMask.x, tempMask.y);
			texcoord = texcoord * 0.5f + glm::vec2(0.5);
			temp.TexCoords = texcoord * textureScale;
			temp.Normal = cubeNormal[face];
			vertex.push_back(temp);
			indices.push_back(indices.size());
		}
	}


	std::shared_ptr<Material> material = std::make_shared<Material>();
	auto cube_mesh = std::make_shared<Mesh>(vertex, indices);
	cube_mesh->CalculateTangentData();
	cube_mesh->MakeScale(scale);

	auto cube_Model = std::make_shared<Model>();
	cube_Model->AddMesh(cube_mesh, material);

	return cube_Model;
}

std::shared_ptr<Model> GetSphereModel(float radius, int sectors, int stacks)
{
	auto guard = THREADCONTEXT->GetBindGuard();

	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	auto getSphereVertex = [](float radius, float theta, float phi)->glm::vec3 {
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);

		float sinPhi = sin(phi);
		float cosPhi = cos(phi);

		// 计算顶点位置
		float x = radius * sinTheta * cosPhi;
		float y = radius * cosTheta;
		float z = radius * sinTheta * sinPhi;

		return glm::vec3(x, y, z);
		};

	const float PI = 3.14159265359f;
	for (int i = 0; i < stacks; ++i) {
		float theta1 = i * PI / stacks;
		float theta2 = (i + 1) * PI / stacks;

		for (int j = 0; j < sectors; ++j) {
			float phi1 = j * 2 * PI / sectors;
			float phi2 = (j + 1) * 2 * PI / sectors;

			// 四个顶点的位置
			glm::vec3 v1 = getSphereVertex(radius, theta1, phi1);
			glm::vec3 v2 = getSphereVertex(radius, theta1, phi2);
			glm::vec3 v3 = getSphereVertex(radius, theta2, phi1);
			glm::vec3 v4 = getSphereVertex(radius, theta2, phi2);

			// 四个顶点的法线（归一化位置）
			glm::vec3 n1 = glm::normalize(v1);
			glm::vec3 n2 = glm::normalize(v2);
			glm::vec3 n3 = glm::normalize(v3);
			glm::vec3 n4 = glm::normalize(v4);

			// 四个顶点的 UV
			glm::vec2 uv1 = glm::vec2((float)j / sectors, (float)i / stacks);
			glm::vec2 uv2 = glm::vec2((float)(j + 1) / sectors, (float)i / stacks);
			glm::vec2 uv3 = glm::vec2((float)j / sectors, (float)(i + 1) / stacks);
			glm::vec2 uv4 = glm::vec2((float)(j + 1) / sectors, (float)(i + 1) / stacks);

			// ========== 三角形1: v1, v2, v3 ==========
			Vertex vert1, vert2, vert3;

			vert1.Position = v1;
			vert1.Normal = n1;
			vert1.TexCoords = uv1;

			vert2.Position = v2;
			vert2.Normal = n2;
			vert2.TexCoords = uv2;

			vert3.Position = v3;
			vert3.Normal = n3;
			vert3.TexCoords = uv3;

			// 添加三个顶点
			unsigned int baseIndex = vertices.size();
			vertices.push_back(vert1);
			vertices.push_back(vert2);
			vertices.push_back(vert3);

			// 索引：连续三个顶点组成一个三角形
			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex + 2);

			// ========== 三角形2: v2, v4, v3 ==========
			Vertex vert4, vert5, vert6;

			vert4.Position = v2;
			vert4.Normal = n2;
			vert4.TexCoords = uv2;

			vert5.Position = v4;
			vert5.Normal = n4;
			vert5.TexCoords = uv4;

			vert6.Position = v3;
			vert6.Normal = n3;
			vert6.TexCoords = uv3;

			// 添加三个顶点
			baseIndex = vertices.size();
			vertices.push_back(vert4);
			vertices.push_back(vert5);
			vertices.push_back(vert6);

			indices.push_back(baseIndex + 0);
			indices.push_back(baseIndex + 1);
			indices.push_back(baseIndex + 2);
		}
	}

	std::shared_ptr<Material> material = std::make_shared<Material>();
	auto sphere_mesh = std::make_shared<Mesh>(vertices, indices);
	sphere_mesh->CalculateTangentData();
	sphere_mesh->setupMesh();

	auto sphere_Model = std::make_shared<Model>();
	sphere_Model->AddMesh(sphere_mesh, material);

	return sphere_Model;
}

std::shared_ptr<Model> GetCylinderModel(float radius, float height, int segments)
{
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	float halfHeight = height * 0.5f;

	// ===== 侧面：每个四边形拆成2个三角形，共6个独立顶点 =====
	for (int i = 0; i < segments; i++) {
		float theta1 = (float)i / segments * 2.0f * glm::pi<float>();
		float theta2 = (float)(i + 1) / segments * 2.0f * glm::pi<float>();

		float cos1 = cos(theta1);
		float sin1 = sin(theta1);
		float cos2 = cos(theta2);
		float sin2 = sin(theta2);

		// ===== 三角形1：v0-v1-v2 (完全独立) =====
		Vertex v0, v1, v2;

		// v0: 底部左侧
		v0.Position = glm::vec3(radius * cos1, -halfHeight, radius * sin1);
		v0.Normal = glm::vec3(cos1, 0.0f, sin1);
		v0.TexCoords = glm::vec2((float)i / segments, 0.0f);
		v0.Tangent = glm::vec3(-sin1, 0.0f, cos1);
		v0.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);

		// v1: 顶部左侧
		v1.Position = glm::vec3(radius * cos1, halfHeight, radius * sin1);
		v1.Normal = glm::vec3(cos1, 0.0f, sin1);
		v1.TexCoords = glm::vec2((float)i / segments, 1.0f);
		v1.Tangent = glm::vec3(-sin1, 0.0f, cos1);
		v1.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);

		// v2: 底部右侧
		v2.Position = glm::vec3(radius * cos2, -halfHeight, radius * sin2);
		v2.Normal = glm::vec3(cos2, 0.0f, sin2);
		v2.TexCoords = glm::vec2((float)(i + 1) / segments, 0.0f);
		v2.Tangent = glm::vec3(-sin2, 0.0f, cos2);
		v2.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);

		vertices.push_back(v0);
		vertices.push_back(v1);
		vertices.push_back(v2);

		// ===== 三角形2：v3-v4-v5 (完全独立，与v0,v1,v2不共享) =====
		Vertex v3, v4, v5;

		// v3: 顶部左侧 (位置同v1，但独立顶点)
		v3.Position = glm::vec3(radius * cos1, halfHeight, radius * sin1);
		v3.Normal = glm::vec3(cos1, 0.0f, sin1);
		v3.TexCoords = glm::vec2((float)i / segments, 1.0f);
		v3.Tangent = glm::vec3(-sin1, 0.0f, cos1);
		v3.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);

		// v4: 顶部右侧
		v4.Position = glm::vec3(radius * cos2, halfHeight, radius * sin2);
		v4.Normal = glm::vec3(cos2, 0.0f, sin2);
		v4.TexCoords = glm::vec2((float)(i + 1) / segments, 1.0f);
		v4.Tangent = glm::vec3(-sin2, 0.0f, cos2);
		v4.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);

		// v5: 底部右侧 (位置同v2，但独立顶点)
		v5.Position = glm::vec3(radius * cos2, -halfHeight, radius * sin2);
		v5.Normal = glm::vec3(cos2, 0.0f, sin2);
		v5.TexCoords = glm::vec2((float)(i + 1) / segments, 0.0f);
		v5.Tangent = glm::vec3(-sin2, 0.0f, cos2);
		v5.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);

		vertices.push_back(v3);
		vertices.push_back(v4);
		vertices.push_back(v5);
	}

	// ===== 顶部和底部的三角形（同样完全独立）=====
	// 底部中心
	Vertex bottomCenter;
	bottomCenter.Position = glm::vec3(0.0f, -halfHeight, 0.0f);
	bottomCenter.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
	bottomCenter.TexCoords = glm::vec2(0.5f, 0.5f);
	bottomCenter.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
	bottomCenter.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

	// 顶部中心
	Vertex topCenter;
	topCenter.Position = glm::vec3(0.0f, halfHeight, 0.0f);
	topCenter.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
	topCenter.TexCoords = glm::vec2(0.5f, 0.5f);
	topCenter.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
	topCenter.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

	for (int i = 0; i < segments; i++) {
		float theta1 = (float)i / segments * 2.0f * glm::pi<float>();
		float theta2 = (float)(i + 1) / segments * 2.0f * glm::pi<float>();

		float cos1 = cos(theta1);
		float sin1 = sin(theta1);
		float cos2 = cos(theta2);
		float sin2 = sin(theta2);

		// ===== 底部三角形：完全独立 =====
		Vertex b0, b1, b2;

		b0 = bottomCenter;  // 复制中心点（独立顶点）

		b1.Position = glm::vec3(radius * cos1, -halfHeight, radius * sin1);
		b1.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
		b1.TexCoords = glm::vec2(0.5f + 0.5f * cos1, 0.5f + 0.5f * sin1);
		b1.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		b1.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

		b2.Position = glm::vec3(radius * cos2, -halfHeight, radius * sin2);
		b2.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
		b2.TexCoords = glm::vec2(0.5f + 0.5f * cos2, 0.5f + 0.5f * sin2);
		b2.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		b2.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

		vertices.push_back(b0);
		vertices.push_back(b1);
		vertices.push_back(b2);

		// ===== 顶部三角形：完全独立 =====
		Vertex t0, t1, t2;

		t0 = topCenter;  // 复制中心点（独立顶点）

		t1.Position = glm::vec3(radius * cos1, halfHeight, radius * sin1);
		t1.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
		t1.TexCoords = glm::vec2(0.5f + 0.5f * cos1, 0.5f + 0.5f * sin1);
		t1.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		t1.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

		t2.Position = glm::vec3(radius * cos2, halfHeight, radius * sin2);
		t2.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
		t2.TexCoords = glm::vec2(0.5f + 0.5f * cos2, 0.5f + 0.5f * sin2);
		t2.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
		t2.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

		vertices.push_back(t0);
		vertices.push_back(t2);
		vertices.push_back(t1);
	}

	for (int i = 0; i < vertices.size(); i++)
		indices.push_back(i);

	std::shared_ptr<Material> material = std::make_shared<Material>();
	auto mesh = std::make_shared<Mesh>(vertices, indices);
	mesh->CalculateTangentData();
	mesh->setupMesh();

	auto model = std::make_shared<Model>();
	model->AddMesh(mesh, material);

	return model;
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb/stb_image_write.h"

bool DrawTexture(std::shared_ptr<Texture2D> texture, std::string path, uint32_t level)
{
	if (!texture)
		return false;

	uint32_t oriWidth = texture->GetWidth();
	uint32_t oriHeight = texture->GetHeight();
	if (oriWidth <= 0 || oriHeight <= 0)
		return false;

	uint32_t width = std::max(1u, oriWidth >> level);
	uint32_t height = std::max(1u, oriHeight >> level);

	int internalFormat = texture->GetInternalFormat();
	if (internalFormat == GL_RGBA16F || internalFormat == GL_RGBA32F)
	{
		int comp = 4;
		int stride = width * comp;
		std::vector<glm::vec4> pixels(width * height);

		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, pixels.data());
		}

		std::vector<unsigned char> byteData(width * height * comp);
		for (int y = 0; y < height; y++)
		{
			int flip_y = height - y - 1;
			for (int x = 0; x < width; x++)
			{
				int flipIndex = width * flip_y + x;
				int oriIndex = width * y + x;
				auto pix = glm::clamp(pixels[flipIndex] * 255.f, 0.f, 255.f);
				byteData[oriIndex * 4 + 0] = (unsigned char)(pix.x);
				byteData[oriIndex * 4 + 1] = (unsigned char)(pix.y);
				byteData[oriIndex * 4 + 2] = (unsigned char)(pix.z);
				byteData[oriIndex * 4 + 3] = (unsigned char)(pix.w);
			}
		}

		stbi_write_png(path.c_str(), width, height, comp, byteData.data(), stride);

	}
	else if (internalFormat == GL_RGB16F || internalFormat == GL_RGB32F)
	{
		int comp = 3;
		int stride = width * comp;
		std::vector<glm::vec3> pixels(width * height);

		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glGetTexImage(GL_TEXTURE_2D, level, GL_RGB, GL_FLOAT, pixels.data());
		}

		std::vector<unsigned char> byteData(width * height * comp);
		for (int y = 0; y < height; y++)
		{
			int flip_y = height - y - 1;
			for (int x = 0; x < width; x++)
			{
				int flipIndex = width * flip_y + x;
				int oriIndex = width * y + x;
				auto pix = glm::clamp(pixels[flipIndex] * 255.f, 0.f, 255.f);
				byteData[oriIndex * 3 + 0] = (unsigned char)(pix.x);
				byteData[oriIndex * 3 + 1] = (unsigned char)(pix.y);
				byteData[oriIndex * 3 + 2] = (unsigned char)(pix.z);
			}
		}

		stbi_write_png(path.c_str(), width, height, comp, byteData.data(), stride);

	}
	else if (internalFormat == GL_RGBA)
	{
		int comp = 4;
		int stride = width * comp;
		std::vector<unsigned char> byteData(width * height * comp);

		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, byteData.data());
		}

		for (int y = 0; y < height / 2; y++)
		{
			int flip_y = height - y - 1;
			auto* row1 = byteData.data() + y * stride;
			auto* row2 = byteData.data() + (height - y - 1) * stride;
			std::swap_ranges(row1, row1 + stride, row2);
		}

		stbi_write_png(path.c_str(), width, height, comp, byteData.data(), stride);
	}
	else if (internalFormat == GL_RG16F)
	{
		int comp = 2;
		int stride = width * comp;
		std::vector<glm::vec2> pixels(width * height);

		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glGetTexImage(GL_TEXTURE_2D, level, GL_RG, GL_FLOAT, pixels.data());
		}

		std::vector<unsigned char> byteData(width * height * comp);
		for (int y = 0; y < height; y++)
		{
			int flip_y = height - y - 1;
			for (int x = 0; x < width; x++)
			{
				int flipIndex = width * flip_y + x;
				int oriIndex = width * y + x;
				auto pix = glm::clamp(pixels[flipIndex] * 255.f, 0.f, 255.f);
				byteData[oriIndex * 2 + 0] = (unsigned char)(pix.x);
				byteData[oriIndex * 2 + 1] = (unsigned char)(pix.y);
			}
		}

		stbi_write_png(path.c_str(), width, height, comp, byteData.data(), stride);

	}
	else if (internalFormat == GL_RED)
	{
		int comp = 1;
		int stride = width * comp;
		std::vector<unsigned char> byteData(width * height);

		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glGetTexImage(GL_TEXTURE_2D, level, GL_RED, GL_UNSIGNED_BYTE, byteData.data());
		}

		for (int y = 0; y < height / 2; y++)
		{
			int flip_y = height - y - 1;
			auto* row1 = byteData.data() + y * stride;
			auto* row2 = byteData.data() + (height - y - 1) * stride;
			std::swap_ranges(row1, row1 + stride, row2);
		}

		stbi_write_png(path.c_str(), width, height, comp, byteData.data(), stride);
	}
	else if (internalFormat == GL_DEPTH24_STENCIL8)
	{
		int comp = 4;
		int stride = width * comp;
		std::vector<GLuint> oriData(width * height);

		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glGetTexImage(GL_TEXTURE_2D, level, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, oriData.data());
		}

		std::vector<unsigned char> byteData(width * height * comp);
		for (int y = 0; y < height; y++)
		{
			int flip_y = height - y - 1;
			for (int x = 0; x < width; x++)
			{
				int flipIndex = width * flip_y + x;
				int oriIndex = width * y + x;
				auto packed = oriData[flipIndex];

				GLuint depth_int = (packed >> 8) & 0x00FFFFFFu;
				float depth_float = depth_int / 16777215.0f;		// 提取深度（归一化到[0-1]）
				GLuint stencil = (GLubyte)(packed & 0x000000FFu);	// 提取模板
				stencil = 255 - stencil;

				float Grayscale = std::clamp(depth_float * 255.f, 0.f, 255.f);
				stencil = std::clamp(stencil, GLuint(0), GLuint(255));

				byteData[oriIndex * 4 + 0] = (unsigned char)(Grayscale);
				byteData[oriIndex * 4 + 1] = (unsigned char)(Grayscale);
				byteData[oriIndex * 4 + 2] = (unsigned char)(Grayscale);
				byteData[oriIndex * 4 + 3] = (unsigned char)(stencil);
			}
		}

		stbi_write_png(path.c_str(), width, height, comp, byteData.data(), stride);
	}
	else if (internalFormat == GL_DEPTH_COMPONENT || internalFormat == GL_DEPTH_COMPONENT32F || internalFormat == GL_R16F || internalFormat == GL_R32F)
	{
		int comp = 1;
		int stride = width * comp;
		std::vector<float> pixels(width * height);

		{
			auto guard = THREADCONTEXT->GetBindGuard();
			glBindTexture(GL_TEXTURE_2D, texture->GetID());
			glGetTexImage(GL_TEXTURE_2D, level,
				internalFormat == GL_DEPTH_COMPONENT || internalFormat == GL_DEPTH_COMPONENT32F ? GL_DEPTH_COMPONENT : GL_RED,
				GL_FLOAT, pixels.data());
		}

		std::vector<unsigned char> byteData(width * height * comp);
		for (int y = 0; y < height; y++)
		{
			int flip_y = height - y - 1;
			for (int x = 0; x < width; x++)
			{
				int flipIndex = width * flip_y + x;
				int oriIndex = width * y + x;
				auto pix = glm::clamp(pixels[flipIndex] * 255.f, 0.f, 255.f);
				byteData[oriIndex] = (unsigned char)(pix);
			}
		}

		stbi_write_png(path.c_str(), width, height, comp, byteData.data(), stride);
	}

	return true;
}

static bool GetCubeViewPorts(GLfloat viewports[6][4], const std::shared_ptr<PointLightInfo>& info, const std::shared_ptr<AtlasMap>& atlasShadowMap)
{
	if (!info || !info->atlas || !atlasShadowMap)
		return false;

	for (int i = 0; i < 6; i++)
	{
		AtlasMap::AtlasRect rect;
		if (!atlasShadowMap->GetSpace(info->atlas->ids[i], rect))
			return false;

		viewports[i][0] = rect.x;
		viewports[i][1] = rect.y;
		viewports[i][2] = rect.width;
		viewports[i][3] = rect.height;
	}
	return true;
}

static auto writePaddingCount = [](int count, std::shared_ptr<SSBO>& ssbo)-> void {
	glm::ivec4 padding = glm::ivec4(count, 0.f, 0.f, 0.f);
	ssbo->WriteData(&padding, sizeof(padding), 0);
	};

void SetupDirLightData(Shader& shader, const std::vector<std::shared_ptr<DirLightInfo>>& dirLights, const std::shared_ptr<AtlasMap>& atlasShadowMap)
{
	static std::shared_ptr<SSBO> meta_ssbo = std::make_shared<SSBO>();
	static std::shared_ptr<SSBO> cascade_ssbo = std::make_shared<SSBO>();

	auto dirLightMeta_ssbo = shader.FindSSBO("DirLightMetaData");
	if (!dirLightMeta_ssbo)
	{
		if (shader.bindSSBO("DirLightMetaData", meta_ssbo))
		{
			dirLightMeta_ssbo = meta_ssbo;
		}
		else
		{
			shader.setInt("dirLightCount", 0);
			return;
		}
	}

	auto dirLightCascade_ssbo = shader.FindSSBO("DirLightCascadeData");
	if (!dirLightCascade_ssbo)
	{
		if (shader.bindSSBO("DirLightCascadeData", cascade_ssbo))
		{
			dirLightCascade_ssbo = cascade_ssbo;
		}
		else
		{
			shader.setInt("dirLightCount", 0);
			return;
		}
	}

	if (!dirLightMeta_ssbo || !dirLightCascade_ssbo)
	{
		shader.setInt("dirLightCount", 0);
		return;
	}

	int cascadeOffset = 0;
	std::vector<DirLightMetaInfo> dirLightMetaData;
	std::vector<DirLightCascadeInfo> dirLightCascadeData;
	for (auto& info : dirLights)
	{
		if (!info || !info->light || info->cascades.empty())
			continue;

		auto light = info->light;

		DirLightMetaInfo metainfo;
		metainfo.direction = light->getDirection();
		metainfo.color = light->getColor();

		metainfo.luxIntensity = light->getIntensity();

		std::vector<DirLightCascadeInfo> cascadeInfos;
		for (auto& cascade : info->cascades)
		{

			AtlasMap::AtlasRect rect;
			if (!atlasShadowMap->GetSpace(cascade.id, rect))
				continue;

			DirLightCascadeInfo cascadeinfo;

			cascadeinfo.atlasX = rect.x;
			cascadeinfo.atlasY = rect.y;
			cascadeinfo.atlasWidth = rect.width;
			cascadeinfo.atlasHeight = rect.height;
			cascadeinfo.cascadePlaneDistance = cascade.cascadePlaneDistance;
			cascadeinfo.lightSpaceMatrix = cascade.lightSpaceMatrix;
			cascadeInfos.push_back(cascadeinfo);
		}

		metainfo.cascadeFirst = cascadeOffset;
		metainfo.cascadeCount = cascadeInfos.size();

		dirLightMetaData.push_back(metainfo);
		if (!cascadeInfos.empty())
			dirLightCascadeData.insert(dirLightCascadeData.end(), cascadeInfos.begin(), cascadeInfos.end());

		cascadeOffset += metainfo.cascadeCount;
	}

	writePaddingCount(dirLightMetaData.size(), dirLightMeta_ssbo);
	dirLightMeta_ssbo->WriteData(dirLightMetaData.data(), dirLightMetaData.size() * sizeof(DirLightMetaInfo), 16);

	writePaddingCount(dirLightMetaData.size(), dirLightCascade_ssbo);
	dirLightCascade_ssbo->WriteData(dirLightCascadeData.data(), dirLightCascadeData.size() * sizeof(DirLightCascadeInfo), 16);
}

void SetupPointLightData(Shader& shader, const std::vector<std::shared_ptr<PointLightInfo>>& pointLights, const std::shared_ptr<AtlasMap>& atlasShadowMap)
{
	static std::shared_ptr<SSBO> meta_ssbo = std::make_shared<SSBO>();

	auto pointLightMeta_ssbo = shader.FindSSBO("PointLightMetaData");
	if (!pointLightMeta_ssbo)
	{
		if (shader.bindSSBO("PointLightMetaData", meta_ssbo))
		{
			pointLightMeta_ssbo = meta_ssbo;
		}
		else
		{
			shader.setInt("pointLightCount", 0);
			return;
		}
	}

	std::vector<PointLightMetaInfo> pointLightMetaData;
	for (auto& info : pointLights)
	{
		auto light = info->light;

		GLfloat viewports[6][4];
		if (!GetCubeViewPorts(viewports, info, atlasShadowMap))
			continue;

		PointLightMetaInfo metainfo;
		metainfo.position = light->getPosition();
		metainfo.color = light->getColor();
		metainfo.cdIntensity = light->getIntensity();
		metainfo.radius = light->getRadius();
		for (int i = 0; i < 6; i++)
		{
			metainfo.atlasStart[i] = glm::vec2(viewports[i][0], viewports[i][1]);
			metainfo.atlasSize[i] = glm::vec2(viewports[i][2], viewports[i][3]);
		}
		pointLightMetaData.push_back(metainfo);
	}

	writePaddingCount(pointLightMetaData.size(), pointLightMeta_ssbo);
	pointLightMeta_ssbo->WriteData(pointLightMetaData.data(), pointLightMetaData.size() * sizeof(PointLightMetaInfo), 16);
}

void SetupSpotLightData(Shader& shader, const std::vector<std::shared_ptr<SpotLightInfo>>& spotLights, const std::shared_ptr<AtlasMap>& atlasShadowMap)
{
	static std::shared_ptr<SSBO> meta_ssbo = std::make_shared<SSBO>();

	auto spotLight_ssbo = shader.FindSSBO("SpotLightMetaData");
	if (!spotLight_ssbo)
	{
		if (shader.bindSSBO("SpotLightMetaData", meta_ssbo))
		{
			spotLight_ssbo = meta_ssbo;
		}
		else
		{
			shader.setInt("spotLightCount", 0);
			return;
		}
	}

	std::vector<SpotLightMetaInfo> spotLightMetaInfos;
	for (auto& info : spotLights)
	{
		if (!info || !info->light || !info->atlas)
			continue;

		auto light = info->light;

		AtlasMap::AtlasRect rect;
		if (!atlasShadowMap->GetSpace(info->atlas->id, rect))
			continue;

		SpotLightMetaInfo metainfo;
		metainfo.position = light->getPosition();
		metainfo.color = light->getColor();
		metainfo.cdIntensity = light->getIntensity();
		metainfo.direction = light->getDirection();
		metainfo.cutOff = glm::cos(glm::radians(light->getCutOffAngle()));
		metainfo.outerCutOff = glm::cos(glm::radians(light->getOuterCutOffAngle()));
		metainfo.lightSpaceMatrix = light->getLightSpaceMatrix();
		metainfo.radius = light->getRadius();
		metainfo.atlasX = rect.x;
		metainfo.atlasY = rect.y;
		metainfo.atlasWidth = rect.width;
		metainfo.atlasHeight = rect.height;

		spotLightMetaInfos.push_back(std::move(metainfo));
	}

	writePaddingCount(spotLightMetaInfos.size(), spotLight_ssbo);
	spotLight_ssbo->WriteData(spotLightMetaInfos.data(), spotLightMetaInfos.size() * sizeof(SpotLightMetaInfo), 16);
}


void RenderHelp::renderSceneGeometryPassStatic(
	RenderState& state, Shader& shader,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<size_t>& renderIndex
)
{
	if (opaqueMeshes.empty())
		return;

	GLboolean isCullFaceEnabled = glIsEnabled(GL_CULL_FACE);

	std::shared_ptr<Material> cur_Material;
	glm::mat4 cur_Model = glm::mat4(1.0f);
	glm::mat4 cur_PreModel = glm::mat4(1.0f);

	shader.setMat4("model", cur_Model);
	shader.setMat4("prevModel", cur_PreModel);

	for (size_t i = 0; i < renderIndex.size(); i++)
	{
		size_t inedx = renderIndex[i];

		auto& mesh = opaqueMeshes[inedx];
		auto& material = mesh.meshinfo.material;

		if (cur_Model != mesh.transform)
		{
			shader.setMat4("model", mesh.transform);
			cur_Model = mesh.transform;
		}
		if (cur_PreModel != mesh.prevTransform)
		{
			shader.setMat4("prevModel", mesh.prevTransform);
			cur_PreModel = mesh.prevTransform;
		}
		if (cur_Material != mesh.meshinfo.material)
		{
			mesh.meshinfo.ApplyMaterial();
			cur_Material = mesh.meshinfo.material;
		}
		mesh.meshinfo.DrawGeometry(shader);
	}

	if (isCullFaceEnabled == GL_TRUE)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}

void RenderHelp::renderSceneGeometryPassSkinned(
	RenderState& state, Shader& shader,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& opaqueSinnedModels,
	std::vector<std::vector<size_t>>& sortIndexArrays
)
{
	if (opaqueSinnedModels.empty())
		return;

	GLboolean isCullFaceEnabled = glIsEnabled(GL_CULL_FACE);

	std::shared_ptr<Material> cur_Material;
	glm::mat4 cur_Model = glm::mat4(1.0f);
	glm::mat4 cur_PreModel = glm::mat4(1.0f);

	SetupAnimatorGroupData(shader, {});
	shader.setMat4("model", cur_Model);
	shader.setMat4("prevModel", cur_PreModel);

	for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
	{
		auto& item = opaqueSinnedModels[i];

		if (cur_Model != item.transform)
		{
			shader.setMat4("model", item.transform);
			cur_Model = item.transform;
		}
		if (cur_PreModel != item.prevTransform)
		{
			shader.setMat4("prevModel", item.prevTransform);
			cur_PreModel = item.prevTransform;
		}
		SetupAnimatorGroupData(shader, *item.animators);

		auto& sort = sortIndexArrays[i];
		for (int i = 0; i < sort.size(); i++)
		{
			auto meshIndex = sort[i];
			auto& meshinfo = item.models[meshIndex];
			if (cur_Material != meshinfo.material)
			{
				meshinfo.ApplyMaterial();
				cur_Material = meshinfo.material;
			}
			meshinfo.DrawGeometry(shader);
		}
	}

	if (isCullFaceEnabled == GL_TRUE)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}

void RenderHelp::renderSceneLightShadowPassScene(
	RenderState& state,
	Shader& shader,
	GLsizei count,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& opaqueSinnedModels
)
{
	shader.setInt("count", count);

	SetupAnimatorGroupData(shader, {});
	glm::mat4 cur_Model = glm::mat4(1.0f);

	shader.setMat4("model", cur_Model);

	for (size_t i = 0; i < opaqueMeshes.size(); i++)
	{
		auto& mesh = opaqueMeshes[i];

		if (cur_Model != mesh.transform)
		{
			shader.setMat4("model", mesh.transform);
			cur_Model = mesh.transform;
		}
		mesh.meshinfo.DrawGeometry(shader);
	}

	for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
	{
		auto& model = opaqueSinnedModels[i];

		if (cur_Model != model.transform)
		{
			shader.setMat4("model", model.transform);
			cur_Model = model.transform;
		}
		SetupAnimatorGroupData(shader, *model.animators);

		for (auto& meshinfo : model.models)
		{
			meshinfo.DrawGeometry(shader);
		}
	}
}

void RenderHelp::renderSceneLightShadowPassSceneInstance(
	RenderState& state, Shader& shader, GLsizei count,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueSkinnedModelItem>& opaqueSinnedModels
)
{
	shader.setInt("count", count);

	SetupAnimatorGroupData(shader, {});
	glm::mat4 cur_Model = glm::mat4(1.0f);

	shader.setMat4("model", cur_Model);

	for (size_t i = 0; i < opaqueMeshes.size(); i++)
	{
		auto& mesh = opaqueMeshes[i];

		if (cur_Model != mesh.transform)
		{
			shader.setMat4("model", mesh.transform);
			cur_Model = mesh.transform;
		}
		mesh.meshinfo.DrawGeometryInstanced(shader, count);
	}

	for (size_t i = 0; i < opaqueSinnedModels.size(); i++)
	{
		auto& model = opaqueSinnedModels[i];

		if (cur_Model != model.transform)
		{
			shader.setMat4("model", model.transform);
			cur_Model = model.transform;
		}
		SetupAnimatorGroupData(shader, *model.animators);

		for (auto& meshinfo : model.models)
		{
			meshinfo.DrawGeometryInstanced(shader, count);
		}
	}
}

void RenderHelp::renderSceneTransparent(
	RenderState& state, Shader& shader,
	std::vector<OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem>& transMeshes,
	std::vector<OpenGLRenderObjectData::SceneRenderData::TransparentSkinnedMeshItem>& transSkinnedMeshes
)
{
	Frustum& frustum = state.camera.frustum;

	std::vector<bool> FrustumCullResult;
	FrustumCullResult.resize(transMeshes.size(), false);

	std::for_each(std::execution::par, transMeshes.begin(), transMeshes.end(),
		[&](OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem& mesh)
		{
			size_t index = &mesh - transMeshes.data();
			AABB aabbworld = mesh.meshinfo.mesh->GetAABB();
			aabbworld.MakeTransform(mesh.transform);
			FrustumCullResult[index] = !frustum.IsAABBOnFrustum(aabbworld);
		});

	SetupAnimatorGroupData(shader, {});
	std::shared_ptr<Material> prevMaterial;
	for (size_t i = 0; i < transMeshes.size(); i++)
	{
		auto& mesh = transMeshes[i];
		if (FrustumCullResult[i])
			continue;

		auto& material = mesh.meshinfo.material;

		shader.setMat4("model", mesh.transform);

		if (prevMaterial != mesh.meshinfo.material)
		{
			mesh.meshinfo.ApplyMaterial();
			prevMaterial = mesh.meshinfo.material;
		}
		mesh.meshinfo.DrawGeometry(shader);
	}

	prevMaterial = nullptr;
	for (size_t i = 0; i < transSkinnedMeshes.size(); i++)
	{
		auto& mesh = transSkinnedMeshes[i];
		if (FrustumCullResult[i])
			continue;

		auto& material = mesh.meshinfo.material;
		shader.setMat4("model", mesh.transform);
		SetupAnimatorGroupData(shader, *mesh.animators);

		if (prevMaterial != mesh.meshinfo.material)
		{
			mesh.meshinfo.ApplyMaterial();
			prevMaterial = mesh.meshinfo.material;
		}
		mesh.meshinfo.DrawGeometry(shader);
	}
}

void RenderHelp::renderFirstPerson(
	RenderState& state, Shader& shader,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueMeshItem>& opaqueMeshes,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem>& opaqueSkinned,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::TransparentMeshItem>& transparentMeshes,
	std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem>& transparentModels
)
{
	//for (auto& item : items)
	//{
	//	SetupAnimatorGroupData(shader, item.animatorViews);

	//	shader.setMat4("CmaeraView", item.cameraView);

	//	item.meshinfo.Draw(shader);
	//}
}

void RenderHelp::renderScreenQuad()
{
	static thread_local GLuint quadVAO = 0;
	static thread_local GLuint quadVBO;
	if (quadVAO == 0)
	{
		float quadVertices[] = {
			// positions        // texture Coords
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		// setup plane VAO
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	}
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void RenderHelp::renderBillboardQuad(Shader& shader)
{
	static std::shared_ptr<Model> s_model;

	if (!s_model)
	{
		auto model = ResFactory->GetModelRes(ResName::Quad);
		if (!model)
			return;
		s_model = model->Clone();
		if (!s_model->getMeshInfos().empty() && s_model->getMeshInfos()[0].material)
			s_model->getMeshInfos()[0].material->SetTwoSided(true);
	}

	s_model->Draw(shader);
}

void RenderHelp::renderCube(Shader& shader)
{
	auto model = ResFactory->GetModelRes(ResName::Cube);
	if (!model)
		return;

	model->Draw(shader);
}

void RenderHelp::renderSphere(Shader& shader)
{
	auto model = ResFactory->GetModelRes(ResName::Sphere);
	if (!model)
		return;

	model->Draw(shader);
}

void RenderHelp::renderCylinder(Shader& shader)
{
	auto model = ResFactory->GetModelRes(ResName::Cylinder);
	if (!model)
		return;

	model->Draw(shader);
}

void RenderHelp::renderLightCube()
{
	static float vertices_cube[] = {
		// back face
		-1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		// front face
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,
		// left face
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		// right face
		 1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		 // bottom face
		 -1.0f, -1.0f, -1.0f,
		  1.0f, -1.0f, -1.0f,
		  1.0f, -1.0f,  1.0f,
		  1.0f, -1.0f,  1.0f,
		 -1.0f, -1.0f,  1.0f,
		 -1.0f, -1.0f, -1.0f,
		 // top face
		 -1.0f,  1.0f, -1.0f,
		  1.0f,  1.0f , 1.0f,
		  1.0f,  1.0f, -1.0f,
		  1.0f,  1.0f,  1.0f,
		 -1.0f,  1.0f, -1.0f,
		 -1.0f,  1.0f,  1.0f,
	};

	static GLuint light_VAO;
	static GLuint light_VBO;

	if (light_VAO == 0)
	{
		/* 设置顶点缓冲对象(VBO) + 设置顶点数组对象(VAO) */
		glGenVertexArrays(1, &light_VAO);
		glGenBuffers(1, &light_VBO);
		glBindVertexArray(light_VAO);
		glBindBuffer(GL_ARRAY_BUFFER, light_VBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices_cube), vertices_cube, GL_STATIC_DRAW);

		/* 设置链接顶点属性 */
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
		glEnableVertexAttribArray(0);
	}

	glBindVertexArray(light_VAO);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
}

void RenderHelp::renderLightSphere()
{
	static auto generateSphereVertices = [](float radius, int sectors, int stacks) ->std::vector<glm::vec3>
		{
			std::vector<glm::vec3> vertices;

			const float PI = 3.14159265359f;

			for (int i = 0; i <= stacks; ++i) {
				float theta = i * PI / stacks;  // 从0到PI (北极到南极)
				float sinTheta = sin(theta);
				float cosTheta = cos(theta);

				for (int j = 0; j <= sectors; ++j) {
					float phi = j * 2 * PI / sectors;  // 从0到2PI (环绕一周)
					float sinPhi = sin(phi);
					float cosPhi = cos(phi);

					// 计算顶点位置
					float x = radius * sinTheta * cosPhi;
					float y = radius * cosTheta;
					float z = radius * sinTheta * sinPhi;

					vertices.push_back(glm::vec3(x, y, z));
				}
			}

			return vertices;
		};

	// 生成球体索引数据（用于三角形绘制）

	static auto generateSphereIndices = [](int sectors, int stacks)->std::vector<unsigned int> {
		std::vector<unsigned int> indices;

		for (int i = 0; i < stacks; ++i) {
			for (int j = 0; j < sectors; ++j) {
				int a = i * (sectors + 1) + j;
				int b = i * (sectors + 1) + j + 1;
				int c = (i + 1) * (sectors + 1) + j;
				int d = (i + 1) * (sectors + 1) + j + 1;

				// 两个三角形构成一个四边形
				indices.push_back(a);
				indices.push_back(b);
				indices.push_back(c);

				indices.push_back(b);
				indices.push_back(d);
				indices.push_back(c);
			}
		}

		return indices;
		};

	static GLuint sphere_VAO = 0;
	static GLuint sphere_VBO = 0;
	static GLuint sphere_EBO = 0;
	static int indexCount = 0;

	if (sphere_VAO == 0)
	{
		// 生成球体数据
		float radius = 10.f;
		int sectors = 36;  // 经线条数，越大越平滑
		int stacks = 18;   // 纬线条数

		std::vector<glm::vec3> vertices = generateSphereVertices(radius, sectors, stacks);
		std::vector<unsigned int> indices = generateSphereIndices(sectors, stacks);
		indexCount = indices.size();

		// 创建VAO, VBO, EBO
		glGenVertexArrays(1, &sphere_VAO);
		glGenBuffers(1, &sphere_VBO);
		glGenBuffers(1, &sphere_EBO);

		glBindVertexArray(sphere_VAO);

		// 顶点数据
		glBindBuffer(GL_ARRAY_BUFFER, sphere_VBO);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);

		// 索引数据
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphere_EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

		// 设置顶点属性
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
		glEnableVertexAttribArray(0);
	}

	// 绘制球体
	glBindVertexArray(sphere_VAO);
	glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

enum class WriteAnimatorSource { Current = 0, Previous };
void SetUpAnimatorGroupData(
	Shader& shader, const std::vector<OpenGLRenderContext::AnimatorView>& animatorViews,
	std::shared_ptr<SSBO>& meta_ssbo, std::shared_ptr<SSBO>& mat_ssbo,
	const std::string& AnimationMetaDataName, const std::string& AnimationMatDataName,
	WriteAnimatorSource source
)
{
	auto ani_Meta_ssbo = shader.FindSSBO(AnimationMetaDataName);
	if (!ani_Meta_ssbo)
	{
		if (shader.bindSSBO(AnimationMetaDataName, meta_ssbo))
			ani_Meta_ssbo = meta_ssbo;
		else
			return;
	}

	if (animatorViews.empty())
	{
		int count = 0;
		ani_Meta_ssbo->WriteData(&count, sizeof(int), 0);
		return;
	}

	auto ani_Mat_ssbo = shader.FindSSBO(AnimationMatDataName);
	if (!ani_Mat_ssbo)
	{
		if (shader.bindSSBO(AnimationMatDataName, mat_ssbo))
			ani_Mat_ssbo = mat_ssbo;
		else
		{
			int count = 0;
			ani_Meta_ssbo->WriteData(&count, sizeof(int), 0);
			return;
		}
	}

	struct MetaInfo
	{
		int offset;
		int count;
	};

	int offset = 0;
	int animationcount = 0;
	for (auto& view : animatorViews)
	{
		if (source == WriteAnimatorSource::Current)
		{
			if (view.matTripleBuffer)
			{
				auto& transforms = view.matTripleBuffer->acquireReadBuffer();
				int mat_count = transforms.size();

				MetaInfo info{ offset,mat_count };

				ani_Mat_ssbo->WriteData(transforms.data(), mat_count * sizeof(glm::mat4), offset * sizeof(glm::mat4));
				ani_Meta_ssbo->WriteData(&info, sizeof(MetaInfo), animationcount * sizeof(MetaInfo) + 4);

				offset += mat_count;
				animationcount++;

				*view.prevRenderMats = transforms;
			}
		}
		else if (source == WriteAnimatorSource::Previous)
		{
			auto& transforms = *view.prevRenderMats;
			int mat_count = transforms.size();

			MetaInfo info{ offset,mat_count };

			ani_Mat_ssbo->WriteData(transforms.data(), mat_count * sizeof(glm::mat4), offset * sizeof(glm::mat4));
			ani_Meta_ssbo->WriteData(&info, sizeof(MetaInfo), animationcount * sizeof(MetaInfo) + 4);

			offset += mat_count;
			animationcount++;
		}
	}
	ani_Meta_ssbo->WriteData(&animationcount, sizeof(int), 0);
}

void RenderHelp::SetupAnimatorGroupData(Shader& shader, const std::vector<OpenGLRenderContext::AnimatorView>& animatorViews)
{
	static std::shared_ptr<SSBO> meta_ssbo = std::make_shared<SSBO>();
	static std::shared_ptr<SSBO> mat_ssbo = std::make_shared<SSBO>();
	static std::shared_ptr<SSBO> prev_meta_ssbo = std::make_shared<SSBO>();
	static std::shared_ptr<SSBO> prev_mat_ssbo = std::make_shared<SSBO>();

	SetUpAnimatorGroupData(shader, animatorViews, prev_meta_ssbo, prev_mat_ssbo, "PrevAnimationMetaData", "PrevAnimationMatData", WriteAnimatorSource::Previous);
	SetUpAnimatorGroupData(shader, animatorViews, meta_ssbo, mat_ssbo, "AnimationMetaData", "AnimationMatData", WriteAnimatorSource::Current);
}

void RenderHelp::SetupLightingData(Shader& shader,
	const std::vector<std::shared_ptr<DirLightInfo>>& dirLights,
	const std::vector<std::shared_ptr<PointLightInfo>>& pointLights,
	const std::vector<std::shared_ptr<SpotLightInfo>>& spotLights,
	const std::shared_ptr<AtlasMap>& atlasShadowMap
)
{
	SetupDirLightData(shader, dirLights, atlasShadowMap);
	SetupPointLightData(shader, pointLights, atlasShadowMap);
	SetupSpotLightData(shader, spotLights, atlasShadowMap);
}
