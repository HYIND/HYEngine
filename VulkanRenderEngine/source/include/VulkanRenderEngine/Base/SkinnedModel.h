#pragma once

#include "ModelBase.h"
#include "SkinnedMesh.h"
#include "Skeleton.h"
#include "Material.h"

struct SkinnedMeshInfo
{
	std::shared_ptr<SkinnedMesh> mesh;
	std::shared_ptr<Material> material;
};

class SkinnedModel :public ModelBase
{
public:
	SkinnedModel(const std::string& path);
	SkinnedModel();

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
	std::shared_ptr<SkinnedModel> Clone(const CloneConfig& config = {});
	std::shared_ptr<SkinnedModel> Clone(bool clonemesh, bool clonematerial);

private:
	void LoadModel(std::string const& path);
	void ProcessNode(aiNode* node, const aiScene* scene, const SkeletonIndex& index);

	SkinnedMeshInfo ProcessMesh(aiMesh* mesh, const aiScene* scene, const SkeletonIndex& index);

private:
	std::vector<SkinnedMeshInfo> _meshes;
};
