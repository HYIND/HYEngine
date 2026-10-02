#pragma once

#include "Project/ProjectManager.h"

class AssetImportPopup
{
public:
	bool GetVisiable();
	void SetVisiable(bool value);
	void SetFilePath(const std::string& filePath);
	void SetImportPath(const std::string& filePath);

	void DrawPopup(ProjectManager* projectManager);

private:
	void DrawTextureConfig();
	int GetFilterIndex(unsigned int currentValue, unsigned int* values, int count);

private:
	char m_wantImportFilePathBuffer[1000];
	char m_wantImportAssetPathBuffer[1000];
	AssetType m_importAssetType = AssetType::StaticMesh;
	bool visiable = false;

private:
	TextureConfig m_config;
};