#pragma once

#include "ModelBase.h"
#include "StaticMesh.h"
#include "Material.h"

struct StaticMeshInfo
{
	std::shared_ptr<StaticMesh> mesh;
	std::shared_ptr<Material> material;
};

class StaticModel :public ModelBase
{

public:
	StaticModel(const std::string& path);
	StaticModel();

	void AddMesh(std::shared_ptr<StaticMesh>& mesh, std::shared_ptr<Material> material = nullptr);
	std::vector<StaticMeshInfo>& GetMeshInfos();

	AABB GetAABB();

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

	StaticMeshInfo ProcessMesh(aiMesh* mesh, const aiScene* scene);

private:
	std::vector<StaticMeshInfo> _meshes;
};
