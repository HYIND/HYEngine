#pragma once

#include "Mesh.h"
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

#include "Coroutine.h"


struct MeshInfo
{
	mutable std::shared_ptr<Mesh> mesh;
	mutable std::shared_ptr<Material> material;

	MaterialData GetMaterialCompData() const;
	void GetMaterialCompData(MaterialData& data) const;
};

struct LoadedTexture {
	std::shared_ptr<Texture2D> tex;
	std::string path;
	Texture2DConfig config;
};

class Model
{
public:
	Model(const std::string& path);
	Model();

	void AddMesh(std::shared_ptr<Mesh>& mesh, std::shared_ptr<Material> material = nullptr);
	std::vector<MeshInfo>& getMeshInfos();

	AABB GetAABB();

	void MakeScale(const glm::vec3& scale);
	void MakeTranslate(const glm::vec3& trans);
	void MakeRotate(float angle, const glm::vec3& axis);
	void MakeTransform(const glm::mat4& mat);

	std::shared_ptr<Skeleton>& GetSkeleton();


public:
	struct CloneConfig
	{
		bool cloneMesh = true;
		bool cloneMaterial = true;
		bool cloneSkeleton = true;

		CloneConfig() {}
		CloneConfig(bool cloneMesh, bool cloneMaterial, bool cloneSkeleton)
			:cloneMesh(cloneMesh), cloneMaterial(cloneMaterial), cloneSkeleton(cloneSkeleton) {
		}
	};
	std::shared_ptr<Model> Clone(const CloneConfig& config = {});
	std::shared_ptr<Model> Clone(bool clonemesh, bool clonematerial, bool cloneskeleton);

private:
	void loadModel(std::string const& path);
	void processNode(aiNode* node, const aiScene* scene);

	MeshInfo processMesh(aiMesh* mesh, const aiScene* scene);
	std::vector<LoadedTexture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, const Texture2DConfig& config);

	void ExtractSkeletonWeightForVertices(std::vector<Vertex>& vertices, aiMesh* mesh, const aiScene* scene);

private:
	std::vector<MeshInfo> _meshes;
	std::shared_ptr<Skeleton> _skeleton;

	std::vector<LoadedTexture> _textures_loaded;

	std::string _directory;
	std::string _filepath;
};
