#include "OpenGLRenderEngine/Base/Model.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>

#include "ThreadPool.h"
#include "Manager/RenderContextManager.h"
#include "OpenGLRenderEngine/Base/AssimpGlmHelpers.h"
#include <filesystem>


std::map<TextureType, TextureConfig> TextureConfigMap =
{
	// ========== 颜色类贴图（必须三线性，开启AF） ==========
	{
		TextureType::Albedo,
		{
			GL_LINEAR_MIPMAP_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			true,
			true
		}
	},
	{
		TextureType::Emissive,
		{
			GL_LINEAR_MIPMAP_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			true,
			true
		}
	},

	{
		TextureType::Normal,
		{
			GL_LINEAR_MIPMAP_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			true,
			false
		}
	},
	{
		TextureType::Roughness,
		{
			GL_LINEAR_MIPMAP_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			true,
			false
		}
	},
	{
		TextureType::Metallic,
		{
			GL_LINEAR_MIPMAP_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			true,
			false
		}
	},
	{
		TextureType::MetallicRoughness,
		{
			GL_LINEAR_MIPMAP_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			true,
			false
		}
	},
	{
		TextureType::AO,
		{
			GL_LINEAR_MIPMAP_LINEAR,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			false,
			false
		}
	},

	{
		TextureType::Height,
		{
			GL_NEAREST_MIPMAP_NEAREST,
			GL_LINEAR,
			GL_REPEAT,
			GL_REPEAT,
			false,
			false
		}
	},
	{
		TextureType::Opacity,
		{
			GL_NEAREST_MIPMAP_NEAREST,
			GL_NEAREST,
			GL_CLAMP_TO_EDGE,
			GL_CLAMP_TO_EDGE,
			false,
			false
		}
	}
};

inline std::string GetExt(const std::string& path)
{
	return std::filesystem::path(path).extension().string();
}

MeshInfo::MaterialData::MaterialData()
{
	static auto emptytex = std::make_shared<Texture2D>(1, 1);

	texture_albedo = emptytex->GetBindlessID();
	texture_metallic = emptytex->GetBindlessID();
	texture_roughness = emptytex->GetBindlessID();
	texture_ao = emptytex->GetBindlessID();
	texture_normal = emptytex->GetBindlessID();
	texture_emissive = emptytex->GetBindlessID();
	texture_metallicroughness = emptytex->GetBindlessID();
	texture_height = emptytex->GetBindlessID();
	texture_opacity = emptytex->GetBindlessID();
}

GLuint GetMaterialUBO()
{
	static GLuint materialUBO = 0;
	if (materialUBO == 0)
	{
		glGenBuffers(1, &materialUBO);
		glBindBuffer(GL_UNIFORM_BUFFER, materialUBO);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(MeshInfo::MaterialData), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, materialUBO);
	}
	return materialUBO;
}

void MeshInfo::Draw(Shader& shader) const
{
	GLuint materialUBO = GetMaterialUBO();
	glBindBuffer(GL_UNIFORM_BUFFER, materialUBO);
	MaterialData comp_data;
	GetMaterialCompData(comp_data);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MaterialData), &comp_data);

	if (material && material->GetTwoSided())
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);

	mesh->Draw(shader);
}
void MeshInfo::DrawGeometry(Shader& shader) const
{
	mesh->Draw(shader);
}
void MeshInfo::DrawInstanced(Shader& shader, GLsizei count) const
{
	GLuint materialUBO = GetMaterialUBO();
	glBindBuffer(GL_UNIFORM_BUFFER, materialUBO);
	MaterialData comp_data;
	GetMaterialCompData(comp_data);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MaterialData), &comp_data);
	if (material && material->GetTwoSided())
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	mesh->DrawInstanced(shader, count);
}
void MeshInfo::DrawGeometryInstanced(Shader& shader, GLsizei count) const
{
	mesh->DrawInstanced(shader, count);
}

void MeshInfo::ApplyMaterial() const
{
	GLuint materialUBO = GetMaterialUBO();
	glBindBuffer(GL_UNIFORM_BUFFER, materialUBO);
	MaterialData comp_data;
	GetMaterialCompData(comp_data);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MaterialData), &comp_data);
	if (material && material->GetTwoSided())
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
}

void MeshInfo::GetMaterialCompData(MaterialData& comp_data) const
{
	if (!material)
		return;

	auto prop = material->GetProperties();
	comp_data.albedo = prop.albedo;
	comp_data.metallic = prop.metallic;
	comp_data.roughness = prop.roughness;
	comp_data.ambientOcclusion = prop.ambientOcclusion;
	comp_data.opacity = prop.opacity;
	comp_data.IOR = prop.IOR;
	comp_data.alphamode = prop.alphamode;
	comp_data.maskthreshold = prop.maskthreshold;
	comp_data.twosided = prop.twosided ? 1 : 0;

	auto textures = material->GetTextures();
	for (auto& [type, tex] : textures)
	{
		if (!tex) continue;

		static auto writeTex = [](std::shared_ptr<Texture2D>& tex, GLuint64& socket, bool useFlag, int& count)-> void {
			if (!useFlag || count > 0) return;
			socket = tex->GetBindlessID();
			count++;
			};

		switch (type)
		{
		case TextureType::Albedo:
			writeTex(tex, comp_data.texture_albedo, prop.useAlbedoTexture, comp_data.texture_albedo_count);
			break;
		case TextureType::Metallic:
			writeTex(tex, comp_data.texture_metallic, prop.useMetallicTexture, comp_data.texture_metallic_count);
			break;
		case TextureType::Roughness:
			writeTex(tex, comp_data.texture_roughness, prop.useRoughnessTexture, comp_data.texture_roughness_count);
			break;
		case TextureType::Normal:
			writeTex(tex, comp_data.texture_normal, prop.useNormalTexture, comp_data.texture_normal_count);
			break;
		case TextureType::AO:
			writeTex(tex, comp_data.texture_ao, prop.useAOTexture, comp_data.texture_ao_count);
			break;
		case TextureType::MetallicRoughness:
			writeTex(tex, comp_data.texture_metallicroughness, prop.useMetallicRoughnessTexture, comp_data.texture_metallicroughness_count);
			break;
		case TextureType::Height:
			writeTex(tex, comp_data.texture_height, prop.useHeightTexture, comp_data.texture_height_count);
			break;
		case TextureType::Opacity:
			writeTex(tex, comp_data.texture_opacity, prop.useOpacityTexture, comp_data.texture_opacity_count);
		default:
			break;
		}

	}
}

void SetVertexBoneDataToDefault(Vertex& vertex)
{
	for (int i = 0; i < OpenGLRenderConfig::Mesh_Max_Bone_Influence; i++)
	{
		vertex.m_BoneIDs[i] = -1;
		vertex.m_Weights[i] = 0.0f;
	}
}

void SetVertexBoneData(Vertex& vertex, int boneID, float weight)
{
	if (vertex.m_BoneIDs[OpenGLRenderConfig::Mesh_Max_Bone_Influence - 1] >= 0)
		return;

	for (int i = 0; i < OpenGLRenderConfig::Mesh_Max_Bone_Influence; ++i)
	{
		if (vertex.m_BoneIDs[i] < 0)
		{
			vertex.m_Weights[i] = weight;
			vertex.m_BoneIDs[i] = boneID;
			break;
		}
	}
}

Model::Model(std::string const& path)
{
	loadModel(path);
}

Model::Model()
{
}

void Model::Draw(Shader& shader)
{
	for (auto& info : _meshes)
		info.Draw(shader);
}

void Model::DrawGeometry(Shader& shader)
{
	for (auto& info : _meshes)
		info.DrawGeometry(shader);
}

void Model::DrawInstanced(Shader& shader, GLsizei count)
{
	for (auto& info : _meshes)
		info.DrawInstanced(shader, count);
}

void Model::DrawGeometryInstanced(Shader& shader, GLsizei count)
{
	for (auto& info : _meshes)
		info.DrawGeometryInstanced(shader, count);
}

void Model::AddMesh(std::shared_ptr<Mesh>& mesh, std::shared_ptr<Material> material)
{
	MeshInfo info{ mesh, material };
	info.mesh = mesh;
	info.material = material;
	_meshes.push_back(info);
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
					if (mesh) mesh->MakeScale(scale, false);
				});
		}
		pool.stop();
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->setupMesh();  // 串行更新GPU
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
					if (mesh) mesh->MakeTranslate(trans, false);
				});
		}
		pool.stop();
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->setupMesh();  // 串行更新GPU
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
					if (mesh) mesh->MakeRotate(angle, axis, false);
				});
		}
		pool.stop();
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->setupMesh();  // 串行更新GPU
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
					if (mesh) mesh->MakeTransform(mat, false);
				});
		}
		pool.stop();
		for (auto& info : _meshes)
			if (info.mesh) info.mesh->setupMesh();  // 串行更新GPU
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

std::shared_ptr<Model> Model::Clone()
{
	auto other = std::make_shared<Model>();
	other->_textures_loaded = _textures_loaded;
	other->_directory = _directory;

	if (_skeleton) other->_skeleton = _skeleton->Clone();
	for (auto& info : _meshes)
	{
		std::shared_ptr<Mesh> cloneMesh;
		std::shared_ptr<Material> cloneMaterial;

		if (info.mesh)
			cloneMesh = info.mesh->Clone();
		if (info.material)
			cloneMaterial = info.material->Clone();
		other->_meshes.push_back(MeshInfo{ cloneMesh,cloneMaterial });
	}

	return other;
}

void Model::loadModel(std::string const& path)
{
	std::string replace_path = path;
	std::replace(replace_path.begin(), replace_path.end(), '\\', '/');
	_filepath = path;

	// read file via ASSIMP
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(replace_path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
	//const aiScene* scene = importer.ReadFile(replace_path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);

	// check for errors
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
	{
		std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}
	// retrieve the _directory path of the filepath
	_directory = replace_path.substr(0, replace_path.find_last_of('/'));

	// process ASSIMP's root node recursively
	processNode(scene->mRootNode, scene);
}

void Model::processNode(aiNode* node, const aiScene* scene)
{
	// process each mesh located at the current node
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		// the node object only contains indices to index the actual objects in the scene. 
		// the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		_meshes.push_back(std::move(processMesh(mesh, scene)));
	}
	// after we've processed all of the _meshes (if any) we then recursively process each of the children nodes
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

	// walk through each of the mesh's vertices
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;
		SetVertexBoneDataToDefault(vertex);

		// positions
		vertex.Position = AssimpGLMHelpers::GetGLMVec(mesh->mVertices[i]);

		// normals
		if (mesh->HasNormals())
		{
			vertex.Normal = AssimpGLMHelpers::GetGLMVec(mesh->mNormals[i]);
		}

		// texture coordinates
		if (mesh->HasTextureCoords(0)) // does the mesh contain texture coordinates?
		{
			// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
			// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
			vertex.TexCoords.x = mesh->mTextureCoords[0][i].x;
			vertex.TexCoords.y = mesh->mTextureCoords[0][i].y;
		}
		else
			vertex.TexCoords = glm::vec2(0.0f, 0.0f);


		if (mesh->HasTangentsAndBitangents())
		{
			// tangent
			vertex.Tangent = AssimpGLMHelpers::GetGLMVec(mesh->mTangents[i]);

			// bitangent
			vertex.Bitangent = AssimpGLMHelpers::GetGLMVec(mesh->mBitangents[i]);
		}
		else
		{
			vertex.Tangent = glm::vec3(0.f);
			vertex.Bitangent = glm::vec3(0.f);
		}

		vertices.push_back(vertex);
	}
	// now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		// retrieve all indices of the face and store them in the indices vector
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	// process materials
	aiMaterial* ai_material = scene->mMaterials[mesh->mMaterialIndex];

	auto loadTexture = [&](aiTextureType ai_type, TextureType type) -> bool {
		std::vector<LoadTexture> loadTextures = loadMaterialTextures(ai_material, ai_type, TextureConfigMap[type]);
		if (!loadTextures.empty())
		{
			material->SetTexture(type, loadTextures[0].tex);
			return true;
		}
		return false;
		};

	if (!loadTexture(aiTextureType_BASE_COLOR, TextureType::Albedo))
		loadTexture(aiTextureType_DIFFUSE, TextureType::Albedo);

	loadTexture(aiTextureType_METALNESS, TextureType::Metallic);

	loadTexture(aiTextureType_DIFFUSE_ROUGHNESS, TextureType::Roughness);

	loadTexture(aiTextureType_AMBIENT_OCCLUSION, TextureType::AO);

	loadTexture(aiTextureType_HEIGHT, TextureType::Height);

	if (!loadTexture(aiTextureType_NORMALS, TextureType::Normal))
		loadTexture(aiTextureType_NORMAL_CAMERA, TextureType::Normal);

	if (!loadTexture(aiTextureType_EMISSION_COLOR, TextureType::Emissive))
		loadTexture(aiTextureType_EMISSIVE, TextureType::Emissive);

	loadTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, TextureType::MetallicRoughness);

	loadTexture(aiTextureType_OPACITY, TextureType::Opacity);

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

		material->SetAlbedo(glm::vec3(ai_baseColor.r, ai_baseColor.g, ai_baseColor.b));
		material->SetOpacity(ai_baseColor.a);
	}

	float metallic = 0.f;
	if (AI_SUCCESS == ai_material->Get(AI_MATKEY_METALLIC_FACTOR, metallic))
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

std::vector<LoadTexture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, const TextureConfig& config)
{
	std::vector<LoadTexture> textures;
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{

		aiString ai_str;
		mat->GetTexture(type, i, &ai_str);
		std::filesystem::path fs_all_path(std::string(ai_str.C_Str()));

		std::vector<std::string> paths;
		paths.push_back(std::string(this->_directory + '/' + fs_all_path.string()));
		if (fs_all_path.filename().string() != fs_all_path.string())
			paths.push_back(std::string(this->_directory + '/' + fs_all_path.filename().string()));

		bool skip = false;
		for (auto& filepath : paths)
		{
			// check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
			for (unsigned int j = 0; j < _textures_loaded.size(); j++)
			{
				if (std::strcmp(_textures_loaded[j].path.data(), filepath.c_str()) == 0)
				{
					textures.push_back(_textures_loaded[j]);
					skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
					break;
				}
			}
		}

		if (skip)
			continue;

		bool isLoadSuccess = false;
		for (auto& filepath : paths)
		{   // if texture hasn't been loaded already, load it
			std::shared_ptr<Texture2D> tex = std::make_shared<Texture2D>(filepath, config.gammaCorrection);
			if (tex->IsEmpty())
				continue;

			tex->SetFiltering(config.minFilter, config.magFilter)
				.SetWrapping(config.wrapS, config.wrapT)
				.SetAnisotropy(config.anisotropy);

			LoadTexture texture;
			texture.tex = tex;
			texture.path = filepath;
			textures.push_back(texture);
			_textures_loaded.push_back(texture);  // store it as texture loaded for entire model, to ensure we won't unnecessary load duplicate textures.
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

	auto& boneInfoMap = _skeleton->BoneInfoMap;
	int& boneCount = _skeleton->BoneCounter;

	int realBoneCount = 0;
	for (int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
	{
		auto weights = mesh->mBones[boneIndex]->mWeights;
		int numWeights = mesh->mBones[boneIndex]->mNumWeights;

		if (!mesh->mBones[boneIndex]->mWeights || mesh->mBones[boneIndex]->mNumWeights <= 0)
			continue;

		realBoneCount++;
		int boneID = -1;
		std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();
		if (boneInfoMap.find(boneName) == boneInfoMap.end())
		{
			BoneInfo newBoneInfo;
			newBoneInfo.id = boneCount;
			newBoneInfo.offset = AssimpGLMHelpers::ConvertMatrixToGLMFormat(mesh->mBones[boneIndex]->mOffsetMatrix);
			boneInfoMap[boneName] = newBoneInfo;
			boneID = boneCount;
			boneCount++;
		}
		else
		{
			boneID = boneInfoMap[boneName].id;
		}

		assert(boneID != -1);

		for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
		{
			int vertexId = weights[weightIndex].mVertexId;
			float weight = weights[weightIndex].mWeight;
			assert(vertexId <= vertices.size());
			SetVertexBoneData(vertices[vertexId], boneID, weight);
		}
	}
	if (realBoneCount < 0)
		std::cout << "realBoneCount:" << realBoneCount << '\n';
}
