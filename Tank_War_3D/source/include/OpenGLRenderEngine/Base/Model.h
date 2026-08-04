#pragma once

#include "Mesh.h"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "Material.h"
#include "Animdata.h"

#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

struct MeshInfo
{
	struct alignas(16) MaterialData
	{
		glm::vec3 albedo = glm::vec3(1.0f);
		float metallic = 0.0f;
		float roughness = 0.5f;
		float ambientOcclusion = 1.0f;

		float opacity = 1.0f;

		float IOR = 1.f;

		AlphaMode alphamode = AlphaMode::Opaque;
		float maskthreshold = 0.5f;

		int twosided = 0;

		int	texture_albedo_count = 0;
		int	texture_metallic_count = 0;
		int	texture_roughness_count = 0;
		int	texture_normal_count = 0;
		int	texture_ao_count = 0;
		int	texture_emissive_count = 0;
		int	texture_metallicroughness_count = 0;
		int	texture_height_count = 0;
		int	texture_opacity_count = 0;

		GLuint64 texture_albedo;
		GLuint64 texture_metallic;
		GLuint64 texture_roughness;
		GLuint64 texture_ao;
		GLuint64 texture_normal;
		GLuint64 texture_emissive;
		GLuint64 texture_metallicroughness;
		GLuint64 texture_height;
		GLuint64 texture_opacity;

		MaterialData();
	};

	mutable std::shared_ptr<Mesh> mesh;
	mutable std::shared_ptr<Material> material;

	void Draw(Shader& shader) const;
	void DrawGeometry(Shader& shader) const;
	void DrawInstanced(Shader& shader, GLsizei count) const;
	void DrawGeometryInstanced(Shader& shader, GLsizei count) const;
	void ApplyMaterial() const;
	void GetMaterialCompData(MaterialData& comp_data) const;
};

struct LoadTexture {
	std::shared_ptr<Texture2D> tex;
	std::string path;
};

struct TextureConfig
{
	unsigned int minFilter;
	unsigned int magFilter;
	unsigned int wrapS;
	unsigned int wrapT;
	bool anisotropy;
	bool gammaCorrection;
};

class Model
{
public:
	Model(std::string const& path);
	Model();
	void Draw(Shader& shader);
	void DrawGeometry(Shader& shader);

	void DrawInstanced(Shader& shader, GLsizei count);
	void DrawGeometryInstanced(Shader& shader, GLsizei count);

	void AddMesh(std::shared_ptr<Mesh>& mesh, std::shared_ptr<Material> material = nullptr);
	std::vector<MeshInfo>& getMeshInfos();

	AABB GetAABB();

	void MakeScale(const glm::vec3& scale);
	void MakeTranslate(const glm::vec3& trans);
	void MakeRotate(float angle, const glm::vec3& axis);
	void MakeTransform(const glm::mat4& mat);

	std::shared_ptr<Skeleton>& GetSkeleton();

	std::shared_ptr<Model> Clone();
private:
	void loadModel(std::string const& path);
	void processNode(aiNode* node, const aiScene* scene);
	MeshInfo processMesh(aiMesh* mesh, const aiScene* scene);
	std::vector<LoadTexture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, const TextureConfig& config);

	void ExtractSkeletonWeightForVertices(std::vector<Vertex>& vertices, aiMesh* mesh, const aiScene* scene);

private:
	std::vector<MeshInfo> _meshes;
	std::shared_ptr<Skeleton> _skeleton;

	std::vector<LoadTexture> _textures_loaded;

	std::string _directory;
	std::string _filepath;
};
