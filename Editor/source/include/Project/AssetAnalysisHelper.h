#pragma once

/*
 该模块为资产解析，负责将资产转化为实际的运行时内容，提供以下功能
 1、加载器，加载资产
 2、分析器，导入资产时可提供分析结果，产生分析元数据，用以资产数据库构建
 */

#include "OpenGLRenderEngine/Base/Mesh.h"
#include "OpenGLRenderEngine/Base/Texture2D.h"
#include "OpenGLRenderEngine/Base/Material.h"
#include "OpenGLRenderEngine/Base/Animation.h"

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
	std::vector<std::shared_ptr<Mesh>> meshes;
	std::shared_ptr<Skeleton> skeleton;
	std::vector<std::shared_ptr<AnalysisTextureAssetMeta>> textureMetas;
	std::vector<std::shared_ptr<AnalysisMaterialAssetMeta>> materialMetas;
};

class AssetAnalysisHelper
{
	//Load
public:
	static std::vector<std::shared_ptr<Mesh>> LoadStaticMesh(const std::string& filePath);

	//Analysis
public:
	static std::shared_ptr<AnalysisResult> AnalysisModelImportData(const std::string& filePath);
};