#include <algorithm>
#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/SkinnedModel.h"
#include "VulkanRenderEngine/GlobalConfig.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>

#include "ThreadPool.h"
#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/AssimpGlmHelpers.h"
#include <filesystem>

namespace fs = std::filesystem;

inline std::string GetExt(const std::string& path)
{
	return fs::path(path).extension().string();
}


SkinnedModel::SkinnedModel(const std::string& path)
{
	LoadModel(path);
}

SkinnedModel::SkinnedModel()
{}

void SkinnedModel::AddMesh(std::shared_ptr<SkinnedMesh>& mesh, std::shared_ptr<Material> material)
{
	SkinnedMeshInfo info;
	info.mesh = mesh;
	info.material = material;
	_meshes.push_back(std::move(info));
}

std::vector<SkinnedMeshInfo>& SkinnedModel::GetMeshInfos()
{
	return _meshes;
}

void SkinnedModel::MakeScale(const glm::vec3& scale)
{
	if (scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f)
		return;
	if (_meshes.size() >= 150)
	{
		ThreadPool pool;
		pool.start();
		for (auto& info : _meshes)
		{
			if (info.mesh == nullptr)
				continue;
			pool.submit(
				[mesh = info.mesh, scale]()->void
				{
					if (mesh) mesh->MakeScale(scale);
				});
		}
		pool.stop();
	}
	else
	{
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->MakeScale(scale);
	}
}

void SkinnedModel::MakeTranslate(const glm::vec3& trans)
{
	if (trans.x == 0.0f && trans.y == 0.0f && trans.z == 0.0f)
		return;
	if (_meshes.size() >= 150)
	{

		ThreadPool pool;
		pool.start();
		for (auto& info : _meshes)
		{
			if (info.mesh == nullptr)
				continue;

			pool.submit(
				[mesh = info.mesh, trans]()->void
				{
					if (mesh) mesh->MakeTranslate(trans);
				});
		}
		pool.stop();
	}
	else
	{
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->MakeTranslate(trans);
	}
}

void SkinnedModel::MakeRotate(float angle, const glm::vec3& axis)
{
	if (angle == 0.0f)
		return;

	if (_meshes.size() >= 150)
	{
		ThreadPool pool;
		pool.start();
		for (auto& info : _meshes)
		{
			if (info.mesh == nullptr)
				continue;

			pool.submit(
				[mesh = info.mesh, angle, axis]()->void
				{
					if (mesh) mesh->MakeRotate(angle, axis);
				});
		}
		pool.stop();
	}
	else
	{
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->MakeRotate(angle, axis);
	}
}

void SkinnedModel::MakeTransform(const glm::mat4& mat)
{
	if (mat == glm::mat4(1.0f))
		return;
	if (_meshes.size() >= 150)
	{
		ThreadPool pool;
		pool.start();
		for (auto& info : _meshes)
		{
			if (info.mesh == nullptr)
				continue;

			pool.submit(
				[mesh = info.mesh, mat]()->void
				{
					if (mesh) mesh->MakeTransform(mat);
				});
		}
		pool.stop();
	}
	else
	{
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->MakeTransform(mat);
	}
}

std::shared_ptr<SkinnedModel> SkinnedModel::Clone(bool clonemesh, bool clonematerial)
{
	return Clone(SkinnedModel::CloneConfig(clonemesh, clonematerial));
}

std::shared_ptr<SkinnedModel> SkinnedModel::Clone(const SkinnedModel::CloneConfig& config)
{
	auto other = std::make_shared<SkinnedModel>();
	other->_assetGUID = _assetGUID;

	if (config.cloneMaterial)
	{
		other->_textures_loaded = _textures_loaded;
		other->_directory = _directory;
	}

	for (auto& info : _meshes)
	{
		std::shared_ptr<SkinnedMesh> mesh;
		std::shared_ptr<Material> material;

		if (info.mesh)
			mesh = config.cloneMesh ? info.mesh->Clone() : info.mesh;
		if (info.material)
			material = config.cloneMaterial ? info.material->Clone() : info.material;

		other->_meshes.push_back(SkinnedMeshInfo{ mesh,material });
	}

	return other;
}

void SkinnedModel::LoadModel(std::string const& path)
{
	std::string replace_path = path;
	std::replace(replace_path.begin(), replace_path.end(), '\\', '/');
	_filepath = path;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(replace_path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_PopulateArmatureData);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}

	_directory = replace_path.substr(0, replace_path.find_last_of('/'));

	// 没有任何骨骼的模型也照常加载：所有顶点都会停在 InvalidBoneID，
	// 绑定上去就是普通静态网格，不用在第二套缓冲和两套 path 之间再分叉。
	// 编号只在 BuildSkeletonIndex 里定义一次，骨架资产和蒙皮用同一张表
	SkeletonIndex index = BuildSkeletonIndex(scene->mRootNode);

	ProcessNode(scene->mRootNode, scene, index);
}

void SkinnedModel::ProcessNode(aiNode* node, const aiScene* scene, const SkeletonIndex& index)
{
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		_meshes.push_back(std::move(ProcessMesh(mesh, scene, index)));
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, index);
	}
}

SkinnedMeshInfo SkinnedModel::ProcessMesh(aiMesh* mesh, const aiScene* scene, const SkeletonIndex& index)
{
	// data to fill
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	std::shared_ptr<Material> material = std::make_shared<Material>();

	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;
		vertex.Position = AssimpGLMHelpers::GetGLMVec(mesh->mVertices[i]);

		if (mesh->HasNormals())
		{
			vertex.Normal = AssimpGLMHelpers::GetGLMVec(mesh->mNormals[i]);
		}

		if (mesh->HasTextureCoords(0))
		{
			vertex.TexCoords.x = mesh->mTextureCoords[0][i].x;
			vertex.TexCoords.y = mesh->mTextureCoords[0][i].y;
		}
		else
			vertex.TexCoords = glm::vec2(0.0f, 0.0f);


		if (mesh->HasTangentsAndBitangents())
		{
			vertex.Tangent = AssimpGLMHelpers::GetGLMVec(mesh->mTangents[i]);
			vertex.Bitangent = AssimpGLMHelpers::GetGLMVec(mesh->mBitangents[i]);
		}
		else
		{
			vertex.Tangent = glm::vec3(0.f);
			vertex.Bitangent = glm::vec3(0.f);
		}

		vertices.push_back(vertex);
	}
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	aiMaterial* ai_material = scene->mMaterials[mesh->mMaterialIndex];

	auto loadTexture = [&](aiTextureType ai_type, TextureType type) -> bool {
		std::vector<std::shared_ptr<Texture2D>> loadTextures = LoadMaterialTextures(ai_material, ai_type, TextureConfigMap[type]);
		if (!loadTextures.empty())
		{
			material->SetTexture(type, loadTextures[0]);
			return true;
		}
		return false;
		};

	bool hasMetal = false;

	if (!loadTexture(aiTextureType_BASE_COLOR, TextureType::Albedo))
		loadTexture(aiTextureType_DIFFUSE, TextureType::Albedo);
	hasMetal |= loadTexture(aiTextureType_METALNESS, TextureType::Metallic);
	loadTexture(aiTextureType_DIFFUSE_ROUGHNESS, TextureType::Roughness);
	loadTexture(aiTextureType_AMBIENT_OCCLUSION, TextureType::AO);
	loadTexture(aiTextureType_HEIGHT, TextureType::Height);
	if (!loadTexture(aiTextureType_NORMALS, TextureType::Normal))
		loadTexture(aiTextureType_NORMAL_CAMERA, TextureType::Normal);
	if (!loadTexture(aiTextureType_EMISSION_COLOR, TextureType::Emissive))
		loadTexture(aiTextureType_EMISSIVE, TextureType::Emissive);
	hasMetal |= loadTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, TextureType::MetallicRoughness);
	loadTexture(aiTextureType_OPACITY, TextureType::Opacity);

	if (!hasMetal)
		loadTexture(aiTextureType_SPECULAR, TextureType::Metallic);

	if (GetExt(_filepath) == ".gltf")
	{
		aiString alphaMode;
		if (AI_SUCCESS == ai_material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode))
		{
			if (alphaMode == aiString("MASK"))
			{
				material->SetAlpahMode(AlphaMode::Mask);
				float alphaCutoff = 0.5f;
				if (AI_SUCCESS == ai_material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff)) {
					material->SetMaskThreshold(alphaCutoff);
				}
			}
			else if (alphaMode == aiString("BLEND")) {
				material->SetAlpahMode(AlphaMode::Blend);
			}
			else if (alphaMode == aiString("OPAQUE"))
			{
				material->SetAlpahMode(AlphaMode::Opaque);
			}
		}
	}

	aiColor4D ai_baseColor = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
	if (AI_SUCCESS == ai_material->Get(AI_MATKEY_BASE_COLOR, ai_baseColor) || AI_SUCCESS == ai_material->Get(AI_MATKEY_COLOR_DIFFUSE, ai_baseColor))
	{
		// 颜色是黑色，但是有纹理，修正
		if (ai_baseColor.r == 0.0f &&
			ai_baseColor.g == 0.0f &&
			ai_baseColor.b == 0.0f &&
			!material->GetTextures().empty())
			ai_baseColor = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);

		if (ai_baseColor.r == 0.6f &&
			ai_baseColor.g == 0.6f &&
			ai_baseColor.b == 0.6f &&
			fs::path(_filepath).extension() == ".obj")
		{
			ai_baseColor.r = ai_baseColor.g = ai_baseColor.b = 1.0;
		}

		material->SetAlbedo(glm::vec3(ai_baseColor.r, ai_baseColor.g, ai_baseColor.b));
		material->SetOpacity(ai_baseColor.a);
	}

	float metallic = 0.f;
	if (AI_SUCCESS == ai_material->Get(AI_MATKEY_METALLIC_FACTOR, metallic)
		|| AI_SUCCESS == ai_material->Get(AI_MATKEY_SPECULAR_FACTOR, metallic)
		|| AI_SUCCESS == ai_material->Get(AI_MATKEY_GLOSSINESS_FACTOR, metallic)
		)
		material->SetMetallic(metallic);

	float roughness = 0.5f;
	if (AI_SUCCESS == ai_material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness))
		material->SetRoughness(roughness);

	float opacity = 1.0f;
	if (AI_SUCCESS == ai_material->Get(AI_MATKEY_OPACITY, opacity))
		material->SetOpacity(opacity);

	int twoSided = 0;
	if (AI_SUCCESS == ai_material->Get(AI_MATKEY_TWOSIDED, twoSided)) {
		material->SetTwoSided(twoSided != 0);
	}

	std::vector<SkinnedData> skinned(vertices.size());
	for (auto& data : skinned)
		data.SetVertexBoneDataToDefault();

	std::vector<glm::mat4> inverseBindMatrices(index.skeleton.nodes.size(), glm::mat4(1.0f));
	for (std::size_t nodeIndex = 0; nodeIndex < index.skeleton.nodes.size(); ++nodeIndex)
		inverseBindMatrices[nodeIndex] = index.skeleton.nodes[nodeIndex].inverseBindTransform;

	uint32_t unresolvedBones = 0;
	uint32_t byNameFallback = 0;
	uint32_t overflowVertices = 0;
	uint32_t unweightedVertices = 0;

	// 先按「顶点」收集，再按权重从大到小取前
	std::vector<std::vector<std::pair<BoneID, float>>> influences(vertices.size());

	if (mesh->mNumBones > 0)
	{
		for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			const aiBone* bone = mesh->mBones[boneIndex];
			if (!bone)
				continue;

			SkeletonNodeIndex nodeIndex = SkeletonNode::InvalidIndex;
			if (bone->mNode)
			{
				const auto it = index.nodeIds.find(bone->mNode);
				if (it != index.nodeIds.end())
					nodeIndex = it->second;
			}

			if (nodeIndex == SkeletonNode::InvalidIndex)
			{
				// 导入器没填 mNode 时的兜底：按名字查，重名会返回 InvalidIndex
				nodeIndex = index.skeleton.FindUniqueNodeByName(bone->mName.C_Str());
				if (nodeIndex != SkeletonNode::InvalidIndex)
					++byNameFallback;
			}

			if (nodeIndex == SkeletonNode::InvalidIndex)
			{
				++unresolvedBones;
				continue;
			}

			inverseBindMatrices[nodeIndex] = AssimpGLMHelpers::ConvertMatrixToGLMFormat(bone->mOffsetMatrix);

			for (unsigned int i = 0; i < bone->mNumWeights; ++i)
			{
				const aiVertexWeight& weight = bone->mWeights[i];
				if (weight.mVertexId >= vertices.size() || weight.mWeight <= 0.0f)
					continue;
				influences[weight.mVertexId].emplace_back(static_cast<BoneID>(nodeIndex), weight.mWeight);
			}
		}
	}

	for (size_t vertexIndex = 0; vertexIndex < influences.size(); ++vertexIndex)
	{
		auto& list = influences[vertexIndex];
		if (list.empty())
		{
			++unweightedVertices;
			continue;
		}

		const bool truncated = list.size() > GlobalConfig::Mesh_Max_Bone_Influence;
		if (truncated)
			++overflowVertices;

		std::sort(list.begin(), list.end(),
			[](const auto& a, const auto& b) { return a.second > b.second; });
		if (truncated)
			list.resize(GlobalConfig::Mesh_Max_Bone_Influence);

		float sum = 0.0f;
		for (const auto& influence : list)
			sum += influence.second;
		if (sum <= 0.0f)
		{
			++unweightedVertices;
			continue;
		}

		SkinnedData& data = skinned[vertexIndex];
		for (size_t i = 0; i < list.size(); ++i)
		{
			data.m_BoneIDs[i] = list[i].first;
			data.m_Weights[i] = list[i].second / sum;
		}
	}


	if (mesh->mNumBones > 0 && (unresolvedBones > 0 || overflowVertices > 0 || byNameFallback > 0 || unweightedVertices > 0))
	{
		std::cerr << "skinned mesh in " << _filepath
			<< ": bones matched by name=" << byNameFallback
			<< ", unresolved=" << unresolvedBones
			<< ", vertices over " << GlobalConfig::Mesh_Max_Bone_Influence << " influences=" << overflowVertices
			<< ", unweighted vertices=" << unweightedVertices
			<< std::endl;
	}

	auto meshptr = std::make_shared<SkinnedMesh>(vertices, indices, skinned);
	meshptr->SetInverseBindMatrices(std::move(inverseBindMatrices));

	return SkinnedMeshInfo{ meshptr,material };
}
