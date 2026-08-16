#pragma once
#include <unordered_map>
#include <string>
#include "imgui.h"
#include "OpenGLRenderEngine/Base/Texture2D.h"
#include "Project/AssetDataBase.h"

class IconManager
{
public:
	static IconManager* Get();

	ImTextureID GetIcon(const std::string& name) const;
	ImTextureID GetIconByAssetType(AssetType type) const;
	ImTextureID GetIconByFilePath(const std::string& filePath) const;
	ImTextureID GetFolderIcon() const;

private:
	IconManager();
	void Need()const;
	void LoadIcons()const;

private:
	mutable std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_icons;
};