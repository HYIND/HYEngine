#pragma once

#include <functional>
#include <vector>
#include <string>
#include <filesystem>

#include "stdafx.h"
#include "Helper/FileIO.h"
#include "Project/AssetMeta.h"
#include "Project/AssetDescription.h"
#include "AssetImportPopup.h"

namespace fs = std::filesystem;

// ============ 检测类型 ============
static bool DetectType(const std::string& path, AssetType& type)
{
	std::string ext = fs::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".stl" || ext == ".pmx")
	{
		type = AssetType::StaticModel;
		return true;
	}
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga")
	{
		type = AssetType::Texture;
		return true;
	}
	if (ext == ".gif")
	{
		type = AssetType::AnimatedImage;
		return true;
	}
	if (ext == ".mat")
	{
		type = AssetType::Material;
		return true;
	}
	if (ext == ".scene")
	{
		type = AssetType::Scene;
		return true;
	}
	if (ext == ".lua")
	{
		type = AssetType::Script;
		return true;
	}
	if (ext == ".ui")
	{
		type = AssetType::UiDocument;
		return true;
	}
	if (ext == ".uistyle")
	{
		type = AssetType::UiStyle;
		return true;
	}

	return false;

}


bool AssetImportPopup::GetVisiable()
{
	return visiable;
}

void AssetImportPopup::SetVisiable(bool value)
{
	visiable = value;
}

void AssetImportPopup::SetFilePath(const std::string& filePath)
{
	memset(m_wantImportFilePathBuffer, 0, sizeof(m_wantImportFilePathBuffer));
	strcpy(m_wantImportFilePathBuffer, filePath.c_str());
	if (!DetectType(filePath, m_importAssetType))
		m_importAssetType = AssetType::Unknown;
}

void AssetImportPopup::SetImportPath(const std::string& filePath)
{
	memset(m_wantImportAssetPathBuffer, 0, sizeof(m_wantImportAssetPathBuffer));
	strcpy(m_wantImportAssetPathBuffer, filePath.c_str());
}

void AssetImportPopup::SetImportCallback(std::function<void(const std::vector<AssetMeta>&)> callback)
{
	m_importCallback = std::move(callback);
}

void AssetImportPopup::DrawPopup(ProjectManager* projectManager)
{

	ImGui::OpenPopup("AssetImportPopup");
	if (ImGui::BeginPopupModal("AssetImportPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("ImportAsset");
		if (!m_hint.empty())
			ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", m_hint.c_str());
		ImGui::Separator();

		ImGui::Text("Location:");
		ImGui::SameLine();

		ImGui::InputText("##FilePath", m_wantImportFilePathBuffer, sizeof(m_wantImportFilePathBuffer));
		ImGui::SameLine();

		uint32_t buttonId = 1;

		ImGui::PushID(buttonId++);
		if (ImGui::Button("浏览...")) {
			std::string filePath;
			if (FileIO::OpenOneFile(filePath))
			{
				memset(m_wantImportFilePathBuffer, 0, sizeof(m_wantImportFilePathBuffer));
				strcpy(m_wantImportFilePathBuffer, filePath.c_str());
			}
			if (!filePath.empty())
			{
				if (!DetectType(filePath, m_importAssetType))
					m_importAssetType = AssetType::Unknown;
			}
		}
		ImGui::PopID();

		ImGui::InputText("##AssetPath", m_wantImportAssetPathBuffer, sizeof(m_wantImportAssetPathBuffer));
		ImGui::SameLine();

		ImGui::PushID(buttonId++);
		if (ImGui::Button("浏览...")) {
			fs::path filePath(m_wantImportFilePathBuffer);
			AssetPath assetPath;
			if (FileIO::SaveOneFile(assetPath, filePath.filename().string(), filePath.extension().string()))
			{
				memset(m_wantImportAssetPathBuffer, 0, sizeof(m_wantImportAssetPathBuffer));
				strcpy(m_wantImportAssetPathBuffer, assetPath.c_str());
			}
		}
		ImGui::PopID();

		int currentItem = static_cast<int>(m_importAssetType);
		// 下标就是 AssetType 的值
		const char* items[] = { "未知", "贴图", "音频", "材质", "网格体","模型", "场景", "脚本", "UI 文档", "UI 样式",
			"蒙皮网格", "蒙皮模型", "骨骼", "动画片段", "精灵", "动图" };
		if (ImGui::Combo("资产类型", &currentItem, items, IM_ARRAYSIZE(items)))
			m_importAssetType = static_cast<AssetType>(currentItem);

		switch (m_importAssetType)
		{
		case AssetType::Unknown:
			break;
		case AssetType::StaticMesh:
			break;
		case AssetType::Texture:
			DrawTextureConfig();
			break;
		case AssetType::Material:
			break;
		case AssetType::StaticModel:
			break;
		case AssetType::Scene:
			break;
		case AssetType::Audio:
			break;
		case AssetType::Script:
			break;
		case AssetType::UiDocument:
			break;
		case AssetType::UiStyle:
			break;
		case AssetType::AnimatedImage:
		case AssetType::Sprite:
			DrawImage2DConfig();
			break;
		default:
			break;
		}

		if (ImGui::Button("确定"))
		{
			if (strlen(m_wantImportFilePathBuffer) > 0 && strlen(m_wantImportAssetPathBuffer) > 0)
			{
				if (projectManager)
				{
					std::vector<AssetMeta> importedMetas;
					bool imported = false;
					switch (m_importAssetType)
					{
					case AssetType::Unknown:
						break;
					case AssetType::StaticMesh:
						imported = projectManager->ImportMeshAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, &importedMetas);
						break;
					case AssetType::Texture:
						imported = projectManager->ImportTextureAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, m_config, &importedMetas);
						break;
					case AssetType::Material:
						break;
					case AssetType::StaticModel:
						imported = projectManager->ImportModelAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, &importedMetas);
						break;
					case AssetType::Scene:
						break;
					case AssetType::Audio:
						break;
					case AssetType::Script:
						imported = projectManager->ImportScriptAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, &importedMetas);
						break;
					case AssetType::UiDocument:
						imported = projectManager->ImportUIDocumentAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, &importedMetas);
						break;
					case AssetType::UiStyle:
						imported = projectManager->ImportUIStyleAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, &importedMetas);
						break;
					case AssetType::AnimatedImage:
						imported = projectManager->ImportAnimatedImageAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, m_image2DConfig, &importedMetas);
						break;
					case AssetType::Sprite:
						imported = projectManager->ImportSpriteAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, m_image2DConfig, &importedMetas);
						break;
					default:
						break;
					}
					if (imported && m_importCallback)
						m_importCallback(importedMetas);

				}

				memset(m_wantImportFilePathBuffer, 0, sizeof(m_wantImportFilePathBuffer));
				memset(m_wantImportAssetPathBuffer, 0, sizeof(m_wantImportAssetPathBuffer));
				ImGui::CloseCurrentPopup();
				visiable = false;
				m_importCallback = nullptr;
				m_hint.clear();
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("取消导入")) {
			memset(m_wantImportFilePathBuffer, 0, sizeof(m_wantImportFilePathBuffer));
			memset(m_wantImportAssetPathBuffer, 0, sizeof(m_wantImportAssetPathBuffer));
			m_importCallback = nullptr;
			m_hint.clear();
			ImGui::CloseCurrentPopup();
			visiable = false;
		}
		ImGui::EndPopup();
	}
}

void AssetImportPopup::DrawTextureConfig()
{
	if (ImGui::CollapsingHeader("Texture Configuration", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Min Filter

		const char* minFilterItems[] = { "NEAREST", "LINEAR" };
		unsigned int minFilterValues[] = { (int)vk::Filter::eNearest, (int)vk::Filter::eLinear };
		int currentMinFilter = GetFilterIndex((int)m_config.minFilter, minFilterValues, 2);
		if (ImGui::Combo("Min Filter", &currentMinFilter, minFilterItems, IM_ARRAYSIZE(minFilterItems)))
		{
			m_config.minFilter = (vk::Filter)minFilterValues[currentMinFilter];
		}

		// Mag Filter
		const char* magFilterItems[] = { "NEAREST", "LINEAR" };
		unsigned int magFilterValues[] = { (int)vk::Filter::eNearest, (int)vk::Filter::eLinear };
		int currentMagFilter = GetFilterIndex((int)m_config.magFilter, magFilterValues, 2);
		if (ImGui::Combo("Mag Filter", &currentMagFilter, magFilterItems, IM_ARRAYSIZE(magFilterItems)))
		{
			m_config.magFilter = (vk::Filter)magFilterValues[currentMagFilter];
		}

		// Wrap U
		const char* wrapItems[] = { "CLAMP_TO_EDGE", "CLAMP_TO_BORDER", "REPEAT", "MIRRORED_REPEAT" };
		unsigned int wrapValues[] = { (int)vk::SamplerAddressMode::eClampToEdge,  (int)vk::SamplerAddressMode::eClampToBorder, (int)vk::SamplerAddressMode::eRepeat, (int)vk::SamplerAddressMode::eMirroredRepeat };
		int currentWrapU = GetFilterIndex((int)m_config.wrapU, wrapValues, 4);
		if (ImGui::Combo("Wrap U", &currentWrapU, wrapItems, IM_ARRAYSIZE(wrapItems)))
		{
			m_config.wrapU = (vk::SamplerAddressMode)wrapValues[currentWrapU];
		}

		// Wrap T
		int currentWrapV = GetFilterIndex((int)m_config.wrapV, wrapValues, 4);
		if (ImGui::Combo("Wrap V", &currentWrapV, wrapItems, IM_ARRAYSIZE(wrapItems)))
		{
			m_config.wrapV = (vk::SamplerAddressMode)wrapValues[currentWrapV];
		}

		// Anisotropy
		ImGui::Checkbox("Anisotropy Filtering", &m_config.anisotropy);

		// Gamma Correction
		ImGui::Checkbox("Gamma Correction", &m_config.gammaCorrection);
	}
}

void AssetImportPopup::DrawImage2DConfig()
{
	if (ImGui::CollapsingHeader("2D 图像导入配置", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 精灵和动图只关心采样过滤和 sRGB；环绕固定 ClampToEdge、不生成 mip、不开各向异性（区域 / 帧挨在一起）。
		// 精灵的区域导入后在精灵编辑器里编辑；动图的播放方式（循环、速度）在 AnimatedImagePlayer 组件 / UI 节点上设置
		const char* filterItems[] = { "NEAREST", "LINEAR" };
		unsigned int filterValues[] = { (int)vk::Filter::eNearest, (int)vk::Filter::eLinear };
		int currentFilter = GetFilterIndex((int)m_image2DConfig.magFilter, filterValues, 2);
		if (ImGui::Combo("过滤方式", &currentFilter, filterItems, IM_ARRAYSIZE(filterItems)))
		{
			m_image2DConfig.minFilter = (vk::Filter)filterValues[currentFilter];
			m_image2DConfig.magFilter = (vk::Filter)filterValues[currentFilter];
		}
		ImGui::Checkbox("Gamma 校正（sRGB）", &m_image2DConfig.gammaCorrection);
	}
}

// 辅助方法：获取过滤器索引
int AssetImportPopup::GetFilterIndex(unsigned int currentValue, unsigned int* values, int count)
{
	for (int i = 0; i < count; i++)
	{
		if (values[i] == currentValue)
			return i;
	}
	return 0; // 默认返回第一个
}