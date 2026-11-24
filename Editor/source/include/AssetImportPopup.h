#pragma once

#include "Project/ProjectManager.h"
#include <functional>
#include <vector>

class AssetImportPopup
{
public:
	bool GetVisiable();
	void SetVisiable(bool value);
	void SetFilePath(const std::string& filePath);
	void SetImportPath(const std::string& filePath);
	// 一次导入可能产出多个资产（网格体有骨骼时是静态+蒙皮，模型更多），所以回调收整批。
	void SetImportCallback(std::function<void(const std::vector<AssetMeta>&)> callback);
	// 预设导入类型和配置（贴图 <-> 精灵互转时用：类型、配置从已有资产带过来，用户还可以改完再确定）。
	// 要在 SetFilePath 之后调：SetFilePath 会按扩展名重新判定类型
	void SetAssetType(AssetType type) { m_importAssetType = type; }
	void SetTextureConfig(const TextureConfig& config) { m_config = config; }
	void SetImage2DConfig(const TextureConfig& config) { m_image2DConfig = config; }
	// 弹窗顶部的一行说明，关闭弹窗时清掉
	void SetHint(const std::string& hint) { m_hint = hint; }

	void DrawPopup(ProjectManager* projectManager);

private:
	void DrawTextureConfig();
	void DrawImage2DConfig();
	int GetFilterIndex(unsigned int currentValue, unsigned int* values, int count);

private:
	char m_wantImportFilePathBuffer[1000];
	char m_wantImportAssetPathBuffer[1000];
	AssetType m_importAssetType = AssetType::StaticMesh;
	bool visiable = false;
	std::function<void(const std::vector<AssetMeta>&)> m_importCallback;
	std::string m_hint;

private:
	TextureConfig m_config;
	// 精灵 / 动图的导入配置：只用 min/magFilter 和 gammaCorrection
	TextureConfig m_image2DConfig{ .minFilter = vk::Filter::eLinear, .magFilter = vk::Filter::eLinear };
};
