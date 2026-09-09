#pragma once

#include "stdafx.h"
#include <unordered_map>
#include <string>
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "Project/AssetDataBase.h"

struct Icon
{
	std::shared_ptr<Texture2D> tex;
	VkDescriptorSet descSet = VK_NULL_HANDLE;
};

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
	mutable std::unordered_map<std::string, Icon> m_icons;
};