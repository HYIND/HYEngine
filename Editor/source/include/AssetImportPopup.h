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

	void DrawPopup(ProjectManager* projectManager);

private:
	void DrawTextureConfig();
	int GetFilterIndex(unsigned int currentValue, unsigned int* values, int count);

private:
	char m_wantImportFilePathBuffer[1000];
	char m_wantImportAssetPathBuffer[1000];
	AssetType m_importAssetType = AssetType::StaticMesh;
	bool visiable = false;
	std::function<void(const std::vector<AssetMeta>&)> m_importCallback;

private:
	TextureConfig m_config;
};