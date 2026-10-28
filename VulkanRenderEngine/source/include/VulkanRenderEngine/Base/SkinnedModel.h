#pragma once

#include "ModelBase.h"
#include "SkinnedMesh.h"
#include "Material.h"

struct SkinnedMeshInfo
{
	std::shared_ptr<SkinnedMesh> mesh;
	std::shared_ptr<Material> material;
};

class StaticModel :public ModelBase
{
private:
	struct LoadedTexture {
		std::shared_ptr<Texture2D> tex;
		std::string path;
		TextureConfig config;
	};

public:
	StaticModel(const std::string& path);
	StaticModel();

	void AddMesh(std::shared_ptr<SkinnedMesh>& mesh, std::shared_ptr<Material> material = nullptr);
	std::vector<SkinnedMeshInfo>& GetMeshInfos();

	void MakeScale(const glm::vec3& scale);
	void MakeTranslate(const glm::vec3& trans);
	void MakeRotate(float angle, const glm::vec3& axis);
	void MakeTransform(const glm::mat4& mat);

public:
	struct CloneConfig
	{
		bool cloneMesh = true;
		bool cloneMaterial = true;

		CloneConfig() {}
		CloneConfig(bool cloneMesh, bool cloneMaterial)
			:cloneMesh(cloneMesh), cloneMaterial(cloneMaterial) {}
	};
	std::shared_ptr<StaticModel> Clone(const CloneConfig& config = {});
	std::shared_ptr<StaticModel> Clone(bool clonemesh, bool clonematerial);

private:
	void LoadModel(std::string const& path);
	void ProcessNode(aiNode* node, const aiScene* scene);

	SkinnedMeshInfo ProcessMesh(aiMesh* mesh, const aiScene* scene);

private:
	std::vector<SkinnedMeshInfo> _meshes;
};
