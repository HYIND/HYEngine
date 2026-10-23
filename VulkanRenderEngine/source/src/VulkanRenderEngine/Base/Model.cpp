#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Model.h"
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

static std::map<TextureType, TextureConfig> TextureConfigMap =
{
	// ========== 颜色类贴图（必须三线性，开启AF） ==========
	{
		TextureType::Albedo,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			true
		}
	},
	{
		TextureType::Emissive,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			true
		}
	},

	{
		TextureType::Normal,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::Roughness,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::Metallic,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::MetallicRoughness,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			true,
			false
		}
	},
	{
		TextureType::AO,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			false,
			false
		}
	},

	{
		TextureType::Height,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eRepeat,
			vk::SamplerAddressMode::eRepeat,
			false,
			false
		}
	},
	{
		TextureType::Opacity,
		{
			vk::Filter::eLinear,
			vk::Filter::eLinear,
			vk::SamplerAddressMode::eClampToEdge,
			vk::SamplerAddressMode::eClampToEdge,
			false,
			false
		}
	}
};

inline std::string GetExt(const std::string& path)
{
	return fs::path(path).extension().string();
}


Model::Model(const std::string& path)
{
	loadModel(path);
}

Model::Model()
{}

void Model::AddMesh(std::shared_ptr<Mesh>& mesh, std::shared_ptr<Material> material)
{
	MeshInfo info;
	info.mesh = mesh;
	info.material = material;
	_meshes.push_back(std::move(info));
}

std::vector<MeshInfo>& Model::getMeshInfos()
{
	return _meshes;
}

AABB Model::GetAABB()
{
	AABB aabb;
	for (auto& info : _meshes)
	{
		auto& mesh = info.mesh;
		if (!mesh)
			continue;

		auto meshaabb = mesh->GetAABB();
		aabb.extend(meshaabb.min);
		aabb.extend(meshaabb.max);
	}
	return aabb;
}

void Model::MakeScale(const glm::vec3& scale)
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

void Model::MakeTranslate(const glm::vec3& trans)
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

void Model::MakeRotate(float angle, const glm::vec3& axis)
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

void Model::MakeTransform(const glm::mat4& mat)
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

std::shared_ptr<Skeleton>& Model::GetSkeleton()
{
	return _skeleton;
}

void Model::SetAssetGUID(const AssetGUID& guid)
{
	_assetGUID = guid;
}

const AssetGUID& Model::GetAssetGUID() const
{
	return _assetGUID;
}

std::shared_ptr<Model> Model::Clone(bool clonemesh, bool clonematerial, bool cloneskeleton)
{
	return Clone(Model::CloneConfig(clonemesh, clonematerial, cloneskeleton));
}

std::shared_ptr<Model> Model::Clone(const Model::CloneConfig& config)
{
	auto other = std::make_shared<Model>();
	other->_assetGUID = _assetGUID;

	if (config.cloneMaterial)
	{
		other->_textures_loaded = _textures_loaded;
		other->_directory = _directory;
	}

	if (_skeleton)
		other->_skeleton = config.cloneSkeleton ? _skeleton->Clone() : _skeleton;

	for (auto& info : _meshes)
	{
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Material> material;

		if (info.mesh)
			mesh = config.cloneMesh ? info.mesh->Clone() : info.mesh;
		if (info.material)
			material = config.cloneMaterial ? info.material->Clone() : info.material;

		other->_meshes.push_back(MeshInfo{ mesh,material });
	}

	return other;
}

void Model::loadModel(std::string const& path)
{
	std::string replace_path = path;
	std::replace(replace_path.begin(), replace_path.end(), '\\', '/');
	_filepath = path;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(replace_path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
	//const aiScene* scene = importer.ReadFile(replace_path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
	{
		std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}

	_directory = replace_path.substr(0, replace_path.find_last_of('/'));
	bool hasSkinning = false;
	for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		if (scene->mMeshes[meshIndex] && scene->mMeshes[meshIndex]->mNumBones > 0)
		{
			hasSkinning = true;
			break;
		}
	}
	if (hasSkinning)
	{
		_skeleton = std::make_shared<Skeleton>();
		BuildSkeletonHierarchy(scene->mRootNode, SkeletonNode::InvalidIndex, "/" + std::string(scene->mRootNode->mName.C_Str()));
	}

	processNode(scene->mRootNode, scene);
}

void Model::BuildSkeletonHierarchy(aiNode* node, uint32_t parentIndex, const std::string& path)
{
	if (!node || !_skeleton)
		return;

	const std::string nodeName = node->mName.C_Str();
	const uint32_t nodeIndex = _skeleton->AddNode(nodeName, path, parentIndex, AssimpGLMHelpers::ConvertMatrixToGLMFormat(node->mTransformation));
	for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
	{
		aiNode* child = node->mChildren[childIndex];
		const std::string childName = child ? child->mName.C_Str() : std::string();
		BuildSkeletonHierarchy(child, nodeIndex, path + "/" + childName + "[" + std::to_string(childIndex) + "]");
	}
}

void Model::processNode(aiNode* node, const aiScene* scene)
{
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		_meshes.push_back(std::move(processMesh(mesh, scene)));
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		processNode(node->mChildren[i], scene);
	}
}

MeshInfo Model::processMesh(aiMesh* mesh, const aiScene* scene)
{
	// data to fill
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	std::shared_ptr<Material> material = std::make_shared<Material>();

	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;
		vertex.SetVertexBoneDataToDefault();

		vertex.Position = AssimpGLMHelpers::GetGLMVec(mesh->mVertices[i]);

		if (mesh->HasNormals())
		{
			vertex.Normal = AssimpGLMHelpers::GetGLMVec(mesh->mNormals[i]);
		}

		if (mesh->HasTextureCoords(0)) // does the mesh contain texture coordinates?
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
		std::vector<LoadedTexture> loadTextures = loadMaterialTextures(ai_material, ai_type, TextureConfigMap[type]);
		if (!loadTextures.empty())
		{
			material->SetTexture(type, loadTextures[0].tex);
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

	ExtractSkeletonWeightForVertices(vertices, mesh, scene);

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

	auto meshptr = std::make_shared<Mesh>(std::move(vertices), std::move(indices));

	return MeshInfo{ meshptr,material };
}

std::vector<Model::LoadedTexture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, const TextureConfig& config)
{
	std::vector<LoadedTexture> textures;
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{

		aiString ai_str;
		mat->GetTexture(type, i, &ai_str);
		fs::path fs_all_path(std::string(ai_str.C_Str()));

		std::vector<fs::path> paths;
		paths.push_back(fs::path(_directory) / fs_all_path);
		if (fs_all_path.filename() != fs_all_path)
			paths.push_back(fs::path(_directory) / fs_all_path.filename());

		bool skip = false;
		for (auto& filepath : paths)
		{
			for (unsigned int j = 0; j < _textures_loaded.size(); j++)
			{
				if (_textures_loaded[j].path == filepath && _textures_loaded[j].config == config)
				{
					textures.push_back(_textures_loaded[j]);
					skip = true;
					break;
				}
			}
		}

		if (skip)
			continue;

		bool isLoadSuccess = false;
		for (auto& filepath : paths)
		{
			std::shared_ptr<Texture2D> tex = std::make_shared<Texture2D>(filepath.string(), config, true);
			if (tex->IsEmpty())
				continue;

			LoadedTexture texture;
			texture.tex = tex;
			texture.path = filepath.string();
			texture.config = config;
			textures.push_back(texture);
			_textures_loaded.push_back(texture);
			isLoadSuccess = true;
			break;
		}
		if (!isLoadSuccess)
			std::cout << "Texture failed to load at path: " << fs_all_path.string() << std::endl;
	}
	return textures;
}

void Model::ExtractSkeletonWeightForVertices(std::vector<Vertex>& vertices, aiMesh* mesh, const aiScene* scene)
{
	if (!_skeleton)
		_skeleton = std::make_shared<Skeleton>();

	for (int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
	{
		auto weights = mesh->mBones[boneIndex]->mWeights;
		int numWeights = mesh->mBones[boneIndex]->mNumWeights;

		if (!mesh->mBones[boneIndex]->mWeights || mesh->mBones[boneIndex]->mNumWeights <= 0)
			continue;

		std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();
		const SkeletonNodeIndex nodeIndex = _skeleton->FindUniqueNodeByName(boneName);
		if (nodeIndex == SkeletonNode::InvalidIndex)
		{
			std::cerr << "Assimp bone has no unique node in imported hierarchy: " << boneName << std::endl;
			continue;
		}

		const BoneID boneID = static_cast<BoneID>(nodeIndex);

		for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
		{
			int vertexId = weights[weightIndex].mVertexId;
			float weight = weights[weightIndex].mWeight;
			assert(vertexId <= vertices.size());
			vertices[vertexId].SetVertexBoneData(boneID, weight);
		}
	}
}
