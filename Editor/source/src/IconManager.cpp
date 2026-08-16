#include "IconManager.h"
#include <filesystem>
#include "OpenGLRenderEngine/OpenGLRenderContextManager.h"

IconManager* IconManager::Get() {
	static IconManager* instance = new IconManager();
	return instance;
}

IconManager::IconManager() {}

void IconManager::Need() const
{
	static std::once_flag flag;
	std::call_once(flag, [&] {
		auto hdc = wglGetCurrentDC();
		auto hglrc = wglGetCurrentContext();
		{
			auto guard = THREADCONTEXT->GetBindGuard();
			LoadIcons();
		}
		wglMakeCurrent(hdc, hglrc);
		});
}

void IconManager::LoadIcons() const
{
	m_icons["folder"] = std::make_shared<Texture2D>("Icons/folder.png", false);
	m_icons["file"] = std::make_shared<Texture2D>("Icons/file.png", false);
	m_icons["model"] = std::make_shared<Texture2D>("Icons/mesh.png", false);
	m_icons["texture"] = std::make_shared<Texture2D>("Icons/texture.png", false);
	m_icons["material"] = std::make_shared<Texture2D>("Icons/material.png", false);
	m_icons["scene"] = std::make_shared<Texture2D>("Icons/scene.png", false);
	m_icons["audio"] = std::make_shared<Texture2D>("Icons/audio.png", false);
}

ImTextureID IconManager::GetIcon(const std::string& name) const
{
	Need();

	auto it = m_icons.find(name);
	return it != m_icons.end() ? it->second->GetID() : 0;
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