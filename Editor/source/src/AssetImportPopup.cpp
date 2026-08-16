#pragma once

#include <functional>
#include <vector>
#include <string>
#include <filesystem>

#include "imgui.h"
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
		type = AssetType::Model;
		return true;
	}
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga")
	{
		type = AssetType::Texture;
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
	DetectType(filePath, m_importAssetType);
}

void AssetImportPopup::SetImportPath(const std::string& filePath)
{
	memset(m_wantImportAssetPathBuffer, 0, sizeof(m_wantImportAssetPathBuffer));
	strcpy(m_wantImportAssetPathBuffer, filePath.c_str());
}

void AssetImportPopup::DrawPopup(ProjectManager* projectManager)
{

	ImGui::OpenPopup("AssetImportPopup");
	if (ImGui::BeginPopupModal("AssetImportPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("ImportAsset");
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
				DetectType(filePath, m_importAssetType);
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
		const char* items[] = { "未知", "贴图", "音频", "材质", "静态网格体","模型", "场景" };
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
		case AssetType::Model:
			break;
		case AssetType::Scene:
			break;
		case AssetType::Audio:
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
					switch (m_importAssetType)
					{
					case AssetType::Unknown:
						break;
					case AssetType::StaticMesh:
						projectManager->ImportStaticMeshAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer);
						break;
					case AssetType::Texture:
						projectManager->ImportTextureAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer, m_config);
						break;
					case AssetType::Material:
						break;
					case AssetType::Model:
						projectManager->ImportModelAsset(m_wantImportFilePathBuffer, m_wantImportAssetPathBuffer);
						break;
					case AssetType::Scene:
						break;
					case AssetType::Audio:
						break;
					default:
						break;
					}

				}

				memset(m_wantImportFilePathBuffer, 0, sizeof(m_wantImportFilePathBuffer));
				memset(m_wantImportAssetPathBuffer, 0, sizeof(m_wantImportAssetPathBuffer));
				ImGui::CloseCurrentPopup();
				visiable = false;
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("取消导入")) {
			memset(m_wantImportFilePathBuffer, 0, sizeof(m_wantImportFilePathBuffer));
			memset(m_wantImportAssetPathBuffer, 0, sizeof(m_wantImportAssetPathBuffer));
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
		const char* filterItems[] = { "GL_NEAREST", "GL_LINEAR", "GL_NEAREST_MIPMAP_NEAREST",
									   "GL_LINEAR_MIPMAP_NEAREST", "GL_NEAREST_MIPMAP_LINEAR", "GL_LINEAR_MIPMAP_LINEAR" };
		unsigned int filterValues[] = { GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST,
										 GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR };
		int currentMinFilter = GetFilterIndex(m_config.minFilter, filterValues, 6);
		if (ImGui::Combo("Min Filter", &currentMinFilter, filterItems, IM_ARRAYSIZE(filterItems)))
		{
			m_config.minFilter = filterValues[currentMinFilter];
		}

		// Mag Filter
		const char* magFilterItems[] = { "GL_NEAREST", "GL_LINEAR" };
		unsigned int magFilterValues[] = { GL_NEAREST, GL_LINEAR };
		int currentMagFilter = GetFilterIndex(m_config.magFilter, magFilterValues, 2);
		if (ImGui::Combo("Mag Filter", &currentMagFilter, magFilterItems, IM_ARRAYSIZE(magFilterItems)))
		{
			m_config.magFilter = magFilterValues[currentMagFilter];
		}

		// Wrap S
		const char* wrapItems[] = { "GL_CLAMP_TO_EDGE", "GL_CLAMP_TO_BORDER", "GL_REPEAT", "GL_MIRRORED_REPEAT" };
		unsigned int wrapValues[] = { GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER, GL_REPEAT, GL_MIRRORED_REPEAT };
		int currentWrapS = GetFilterIndex(m_config.wrapS, wrapValues, 4);
		if (ImGui::Combo("Wrap S", &currentWrapS, wrapItems, IM_ARRAYSIZE(wrapItems)))
		{
			m_config.wrapS = wrapValues[currentWrapS];
		}

		// Wrap T
		int currentWrapT = GetFilterIndex(m_config.wrapT, wrapValues, 4);
		if (ImGui::Combo("Wrap T", &currentWrapT, wrapItems, IM_ARRAYSIZE(wrapItems)))
		{
			m_config.wrapT = wrapValues[currentWrapT];
		}

		// Anisotropy
		ImGui::Checkbox("Anisotropy Filtering", &m_config.anisotropy);

		// Gamma Correction
		ImGui::Checkbox("Gamma Correction", &m_config.gammaCorrection);
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