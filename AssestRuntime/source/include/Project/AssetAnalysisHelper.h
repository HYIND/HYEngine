#pragma once

/*
 该模块为资产解析，负责将资产转化为实际的运行时内容，提供以下功能
 1、加载器，加载资产
 2、分析器，导入资产时可提供分析结果，产生分析元数据，用以资产数据库构建
 */

#include "VulkanRenderEngine/Base/StaticMesh.h"
#include "VulkanRenderEngine/Base/SkinnedMesh.h"
#include "VulkanRenderEngine/Base/AnimationClip.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/Material.h"
#include "VulkanRenderEngine/Base/Skeleton.h"

struct AnalysisTextureAssetMeta
{
	std::string filepath;
	TextureConfig config;
};

struct AnalysisMaterialAssetMeta
{
	MaterialProperties prop;
	std::map<TextureType, std::shared_ptr<AnalysisTextureAssetMeta>> textures;
};

struct AnalysisResult
{
	std::vector<std::shared_ptr<StaticMesh>> meshes;
	std::vector<std::shared_ptr<AnalysisTextureAssetMeta>> textureMetas;
	std::vector<std::shared_ptr<AnalysisMaterialAssetMeta>> materialMetas;

	// 导入时按这两项决定产出哪些资产，不靠文件名或用户选择。
	bool hasSkeleton = false;
	uint32_t animationCount = 0;
};

class AssetAnalysisHelper
{
	//Load
public:
	static std::vector<std::shared_ptr<StaticMesh>> LoadStaticMesh(const std::string& filePath);
	static std::vector<std::shared_ptr<SkinnedMesh>> LoadSkinnedMesh(const std::string& filePath);
	static std::shared_ptr<Skeleton> LoadSkeleton(const std::string& filePath);

	// 一个文件里可能有多段动画，index 选其中一段。
	static std::shared_ptr<AnimationClip> LoadAnimationClip(const std::string& filePath, int index);

	//Analysis
public:
	static std::shared_ptr<AnalysisResult> AnalysisModelImportData(const std::string& filePath);
};