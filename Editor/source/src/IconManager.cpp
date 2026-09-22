#include "IconManager.h"
#include <filesystem>
#include "VulkanRenderEngine/VKContext.h"

IconManager* IconManager::Get() {
	static IconManager* instance = new IconManager();
	return instance;
}

IconManager::IconManager() {}

void IconManager::Need() const
{
	static std::once_flag flag;
	std::call_once(flag, [&] {
		LoadIcons();
		});
}

void IconManager::LoadIcons() const
{
	auto cmd = VKCONTEXT->GetCommandBuffer();

	auto LoadTex = [&](const std::string& path)->Icon {
		auto tex = std::make_shared<Texture2D>(path);
		if (!tex->IsEmpty())
			tex->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
		return Icon{ .tex = tex };
		};

	m_icons["folder"] = LoadTex("Icons/folder.png");
	m_icons["file"] = LoadTex("Icons/file.png");
	m_icons["model"] = LoadTex("Icons/mesh.png");
	m_icons["texture"] = LoadTex("Icons/texture.png");
	m_icons["material"] = LoadTex("Icons/material.png");
	m_icons["scene"] = LoadTex("Icons/scene.png");
	m_icons["audio"] = LoadTex("Icons/audio.png");

	if (cmd->IsRecording())
		cmd->SubmitNowAndWait();
}

ImTextureID IconManager::GetIcon(const std::string& name) const
{
	Need();

	auto it = m_icons.find(name);
	if (it == m_icons.end())
		return ImTextureID(0);

	auto& icon = it->second;
	if (icon.descSet == VK_NULL_HANDLE && icon.tex && !icon.tex->IsEmpty())
	{
		icon.descSet = ImGui_ImplVulkan_AddTexture(
			icon.tex->GetImageView(vk::ImageAspectFlagBits::eColor),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);
	}

	return ImTextureID(icon.descSet);
}

ImTextureID IconManager::GetIconByAssetType(AssetType type) const
{
	Need();

	if (type == AssetType::StaticMesh || type == AssetType::Model) {
		return GetIcon("model");
	}
	else if (type == AssetType::Texture) {
		return GetIcon("texture");
	}
	else if (type == AssetType::Material) {
		return GetIcon("material");
	}
	else if (type == AssetType::Scene) {
		return GetIcon("scene");
	}
	else if (type == AssetType::Audio) {
		return GetIcon("audio");
	}

	return GetIcon("file");
}

ImTextureID IconManager::GetIconByFilePath(const std::string& filePath) const
{
	Need();

	std::string ext = std::filesystem::path(filePath).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".stl") {
		return GetIcon("model");
	}
	else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
		return GetIcon("texture");
	}
	else if (ext == ".mat") {
		return GetIcon("material");
	}
	else if (ext == ".scene") {
		return GetIcon("scene");
	}
	else if (ext == ".mp3" || ext == ".wav") {
		return GetIcon("audio");
	}
	return GetIcon("file");
}

ImTextureID IconManager::GetFolderIcon() const
{
	Need();

	return GetIcon("folder");
}