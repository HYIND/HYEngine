#include "Project/AssetAnalysisHelper.h"

#include "VulkanRenderEngine/Base/ModelBase.h"
#include "VulkanRenderEngine/Base/SkinnedModel.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb\stb_image.h>

#include "VulkanRenderEngine/VKContext.h"
#include "VulkanRenderEngine/Base/AssimpGlmHelpers.h"

#include <filesystem>

namespace fs = std::filesystem;

inline std::string GetExt(const std::string& path)
{
	return std::filesystem::path(path).extension().string();
}


bool isSameAnalysisTextureAssetMeta(AnalysisTextureAssetMeta& a, AnalysisTextureAssetMeta& b)
{
	return a.filepath == b.filepath && a.config == b.config;
}

bool isSameAnalysisMaterialAssetMeta(AnalysisMaterialAssetMeta& a, AnalysisMaterialAssetMeta& b)
{
	if (a.textures.size() != b.textures.size())
		return false;

	for (auto& [type, tex] : a.textures)
	{
		if (b.textures.count(type) == 0 || tex != b.textures[type])
			return false;
	}

	const auto& pa = a.prop;
	const auto& pb = b.prop;

	return pa.albedo == pb.albedo &&
		pa.metallic == pb.metallic &&
		pa.roughness == pb.roughness &&
		pa.ambientOcclusion == pb.ambientOcclusion &&
		pa.opacity == pb.opacity &&
		pa.IOR == pb.IOR &&
		pa.alphamode == pb.alphamode &&
		pa.maskthreshold == pb.maskthreshold &&
		pa.twosided == pb.twosided;
}


struct StaticModelAnalysisParams
{
	bool meshNeed = false;
	bool materialNeed = false;

	std::vector<std::shared_ptr<StaticMesh>> meshes;

	std::vector<std::shared_ptr<AnalysisTextureAssetMeta>> textureMetas;
	std::vector<std::shared_ptr<AnalysisMaterialAssetMeta>> materialMetas;
};

class StaticModelFileAnalysis
{
public:
	StaticModelFileAnalysis(const aiScene* scene, const std::string& filepath)
		:_scene(scene), _filepath(filepath) {
		_directory = fs::path(filepath).parent_path().string();
	};

	bool Excute(StaticModelAnalysisParams& params);

private:
	void ProcessNode(aiNode* node, const aiScene* scene, StaticModelAnalysisParams& params);
	void BuildSkeletonHierarchy(aiNode* node, uint32_t parentIndex, const std::string& path, Skeleton& skeleton);
	void ProcessMesh(aiMesh* mesh, const aiScene* scene, StaticModelAnalysisParams& params);
	std::vector<std::shared_ptr<AnalysisTextureAssetMeta>> FindMaterialTextures(aiMaterial* mat, aiTextureType type, const TextureConfig& config);

public:
	const aiScene* _scene = nullptr;
	std::string _filepath;
	std::string _directory;
};

std::vector<std::shared_ptr<StaticMesh>> AssetAnalysisHelper::LoadStaticMesh(const std::string& filePath)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);

	StaticModelFileAnalysis analysis(scene, filePath);
	StaticModelAnalysisParams params;
	params.meshNeed = true;
	analysis.Excute(params);
	return params.meshes;
}

std::shared_ptr<AnalysisResult> AssetAnalysisHelper::AnalysisModelImportData(const std::string& filePath)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);

	StaticModelFileAnalysis analysis(scene, filePath);
	StaticModelAnalysisParams params;
	params.meshNeed = true;
	params.materialNeed = true;
	analysis.Excute(params);

	auto res = std::make_shared<AnalysisResult>();
	res->meshes = std::move(params.meshes);
	res->textureMetas = std::move(params.textureMetas);
	res->materialMetas = std::move(params.materialMetas);

	// 有没有骨骼决定导入时产不产蒙皮资产：带骨骼的 mesh 存在，整个模型就按蒙皮处理。
	for (unsigned int meshIndex = 0; scene && meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (mesh && mesh->mNumBones > 0)
		{
			res->hasSkeleton = true;
			break;
		}
	}
	res->animationCount = scene ? scene->mNumAnimations : 0;

	return res;
}

std::vector<std::shared_ptr<SkinnedMesh>> AssetAnalysisHelper::LoadSkinnedMesh(const std::string& filePath)
{
	// 蒙皮网格的顶点骨骼权重只有走一遍模型加载才拆得出来，这里复用 SkinnedModel 的解析。
	SkinnedModel model(filePath);

	std::vector<std::shared_ptr<SkinnedMesh>> meshes;
	for (auto& meshInfo : model.GetMeshInfos())
	{
		if (meshInfo.mesh)
			meshes.push_back(meshInfo.mesh);
	}
	return meshes;
}

std::shared_ptr<Skeleton> AssetAnalysisHelper::LoadSkeleton(const std::string& filePath)
{
	return ::LoadSkeleton(filePath);
}

std::shared_ptr<AnimationClip> AssetAnalysisHelper::LoadAnimationClip(const std::string& filePath, int index)
{
	if (index < 0)
		return nullptr;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		return nullptr;
	if (static_cast<unsigned int>(index) >= scene->mNumAnimations)
		return nullptr;

	return std::make_shared<AnimationClip>(scene, index);
}

bool StaticModelFileAnalysis::Excute(StaticModelAnalysisParams& params)
{
	auto scene = _scene;
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		return false;

	ProcessNode(scene->mRootNode, scene, params);
	return true;
}

void StaticModelFileAnalysis::ProcessNode(aiNode* node, const aiScene* scene, StaticModelAnalysisParams& params)
{
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		ProcessMesh(mesh, scene, params);
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, params);
	}
}

// 辅助函数：将枚举转为字符串
const char* GetTextureTypeName(aiTextureType type) {
	switch (type) {
	case aiTextureType_NONE: return "NONE";
	case aiTextureType_DIFFUSE: return "DIFFUSE";
	case aiTextureType_SPECULAR: return "SPECULAR";
	case aiTextureType_AMBIENT: return "AMBIENT";
	case aiTextureType_EMISSIVE: return "EMISSIVE";
	case aiTextureType_HEIGHT: return "HEIGHT";
	case aiTextureType_NORMALS: return "NORMALS";
	case aiTextureType_SHININESS: return "SHININESS";
	case aiTextureType_OPACITY: return "OPACITY";
	case aiTextureType_DISPLACEMENT: return "DISPLACEMENT";
	case aiTextureType_LIGHTMAP: return "LIGHTMAP";
	case aiTextureType_REFLECTION: return "REFLECTION";
	case aiTextureType_BASE_COLOR: return "BASE_COLOR";
	case aiTextureType_NORMAL_CAMERA: return "NORMAL_CAMERA";
	case aiTextureType_EMISSION_COLOR: return "EMISSION_COLOR";
	case aiTextureType_METALNESS: return "METALNESS";
	case aiTextureType_DIFFUSE_ROUGHNESS: return "DIFFUSE_ROUGHNESS";
	case aiTextureType_AMBIENT_OCCLUSION: return "AMBIENT_OCCLUSION";
	case aiTextureType_GLTF_METALLIC_ROUGHNESS: return "GLTF_METALLIC_ROUGHNESS";
	case aiTextureType_UNKNOWN: return "UNKNOWN";
	default: return "UNKNOWN_TYPE";
	}
}

void EnumerateMaterialTextures(aiMaterial* material) {
	if (!material) return;

	// 获取所有纹理类型的数量
	unsigned int textureCount = material->GetTextureCount(aiTextureType_NONE);

	// 但实际上，每种纹理类型都需要单独查询
	// 更简单的方法：遍历所有可能的 aiTextureType

	std::cout << "========== 材质纹理列表 ==========\n";

	// 遍历所有纹理类型（从 0 到 aiTextureType_UNKNOWN）
	for (int type = 0; type <= AI_TEXTURE_TYPE_MAX; ++type) {
		aiTextureType texType = static_cast<aiTextureType>(type);
		unsigned int count = material->GetTextureCount(texType);

		if (count > 0) {
			std::cout << "类型: " << GetTextureTypeName(texType)
				<< " (枚举值: " << type << ")"
				<< " 数量: " << count << "\n";

			// 获取每个纹理的路径
			for (unsigned int i = 0; i < count; ++i) {
				aiString path;
				if (material->GetTexture(texType, i, &path) == AI_SUCCESS) {
					std::cout << "  [" << i << "] " << path.C_Str() << "\n";
				}
			}
		}
	}

	std::cout << "====================================\n";
}

void StaticModelFileAnalysis::ProcessMesh(aiMesh* mesh, const aiScene* scene, StaticModelAnalysisParams& params)
{
	if (params.meshNeed)
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;

		for (unsigned int i = 0; i < mesh->mNumVertices; i++)
		{
			Vertex vertex;
			vertex.Position = AssimpGLMHelpers::GetGLMVec(mesh->mVertices[i]);

			if (mesh->HasNormals())
				vertex.Normal = AssimpGLMHelpers::GetGLMVec(mesh->mNormals[i]);

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

		auto meshptr = std::make_shared<StaticMesh>(std::move(vertices), std::move(indices));
		params.meshes.push_back(meshptr);
	}

	if (params.materialNeed)
	{
		auto materialMeta = std::make_shared<AnalysisMaterialAssetMeta>();
		aiMaterial* ai_material = scene->mMaterials[mesh->mMaterialIndex];

		auto loadTexture = [&](aiTextureType ai_type, TextureType type) -> bool {
			auto metas = FindMaterialTextures(ai_material, ai_type, TextureConfigMap[type]);
			if (metas.empty())
				return false;

			auto& texturemeta = metas[0];

			bool isExist = false;
			for (auto& loaded : params.textureMetas)
			{
				//分析texture，如果是新的texture则加入，已经存在的则不重复生成元数据
				if (isSameAnalysisTextureAssetMeta(*texturemeta, *loaded))
				{
					texturemeta = loaded;
					isExist = true;
					break;
				}
			}

			if (!isExist)
				params.textureMetas.push_back(texturemeta);

			materialMeta->textures[type] = texturemeta;
			return true;
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

#ifdef _DEBUG
		EnumerateMaterialTextures(ai_material);
#endif // _DEBUG

		Material temp;
		if (GetExt(_filepath) == ".gltf")
		{
			aiString alphaMode;
			if (AI_SUCCESS == ai_material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode))
			{
				if (alphaMode == aiString("MASK"))
				{
					temp.SetAlpahMode(AlphaMode::Mask);
					float alphaCutoff = 0.5f;
					if (AI_SUCCESS == ai_material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff)) {
						temp.SetMaskThreshold(alphaCutoff);
					}
				}
				else if (alphaMode == aiString("BLEND")) {
					temp.SetAlpahMode(AlphaMode::Blend);
				}
				else if (alphaMode == aiString("OPAQUE"))
				{
					temp.SetAlpahMode(AlphaMode::Opaque);
				}
			}
		}

		aiColor4D ai_baseColor = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);
		if (AI_SUCCESS == ai_material->Get(AI_MATKEY_BASE_COLOR, ai_baseColor) || AI_SUCCESS == ai_material->Get(AI_MATKEY_COLOR_DIFFUSE, ai_baseColor))
		{
			// 颜色是黑色，但是有纹理，修正
			if (ai_baseColor.r == 0.0f
				&& ai_baseColor.g == 0.0f
				&& ai_baseColor.b == 0.0f
				&& !materialMeta->textures.empty())
				ai_baseColor = aiColor4D(1.0f, 1.0f, 1.0f, 1.0f);

			if (ai_baseColor.r == 0.6f &&
				ai_baseColor.g == 0.6f &&
				ai_baseColor.b == 0.6f &&
				fs::path(_filepath).extension() == ".obj")
			{
				ai_baseColor.r = ai_baseColor.g = ai_baseColor.b = 1.0;
			}

			temp.SetAlbedo(glm::vec3(ai_baseColor.r, ai_baseColor.g, ai_baseColor.b));
			temp.SetOpacity(ai_baseColor.a);
		}

		float metallic = 0.f;
		if (AI_SUCCESS == ai_material->Get(AI_MATKEY_METALLIC_FACTOR, metallic)
			|| AI_SUCCESS == ai_material->Get(AI_MATKEY_SPECULAR_FACTOR, metallic)
			|| AI_SUCCESS == ai_material->Get(AI_MATKEY_GLOSSINESS_FACTOR, metallic)
			)
			temp.SetMetallic(metallic);

		float roughness = 0.5f;
		if (AI_SUCCESS == ai_material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness))
			temp.SetRoughness(roughness);

		float opacity = 1.0f;
		if (AI_SUCCESS == ai_material->Get(AI_MATKEY_OPACITY, opacity))
			temp.SetOpacity(opacity);

		int twoSided = 0;
		if (AI_SUCCESS == ai_material->Get(AI_MATKEY_TWOSIDED, twoSided))
			temp.SetTwoSided(twoSided != 0);

		materialMeta->prop = temp.GetProperties();

		for (auto& loaded : params.materialMetas)
		{
			if (isSameAnalysisMaterialAssetMeta(*loaded, *materialMeta))
			{
				materialMeta = loaded;
				break;
			}
		}

		params.materialMetas.push_back(materialMeta);
	}
}

std::vector<std::shared_ptr<AnalysisTextureAssetMeta>> StaticModelFileAnalysis::FindMaterialTextures(aiMaterial* mat, aiTextureType type, const TextureConfig& config)
{
	std::vector<std::shared_ptr<AnalysisTextureAssetMeta>> metas;
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{

		aiString ai_str;
		mat->GetTexture(type, i, &ai_str);
		fs::path fs_all_path(std::string(ai_str.C_Str()));

		std::vector<fs::path> paths;
		paths.push_back(fs::path(_directory) / fs_all_path);
		if (fs_all_path.filename() != fs_all_path)
			paths.push_back(fs::path(_directory) / fs_all_path.filename());

		for (auto& filepath : paths)
		{
			if (!fs::exists(filepath) || fs::is_directory(filepath))
				continue;

			auto meta = std::make_shared<AnalysisTextureAssetMeta>();
			meta->filepath = filepath.string();
			meta->config = config;

			metas.push_back(meta);
			break;
		}
	}
	return metas;
}
