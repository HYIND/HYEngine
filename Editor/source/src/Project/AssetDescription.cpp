#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

#include "Project/AssetDescription.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

NLOHMANN_JSON_SERIALIZE_ENUM(AlphaMode, {
	{AlphaMode::Opaque, "Opaque"},
	{AlphaMode::Mask, "Mask"},
	{AlphaMode::Blend, "Blend"}
	})

	NLOHMANN_JSON_SERIALIZE_ENUM(TextureType, {
	{TextureType::Albedo, "Albedo"},
	{TextureType::Metallic, "Metallic"},
	{TextureType::Roughness, "Roughness"},
	{TextureType::Normal, "Normal"},
	{TextureType::AO, "AO"},
	{TextureType::Emissive, "Emissive"},
	{TextureType::MetallicRoughness, "MetallicRoughness"},
	{TextureType::Height, "Height"},
	{TextureType::Opacity, "Opacity"},
		})

		namespace nlohmann {
	template<>
	struct adl_serializer<glm::vec3> {
		static void to_json(json& j, const glm::vec3& v) {
			j = json{ v.x, v.y, v.z };
		}

		static void from_json(const json& j, glm::vec3& v) {
			j.at(0).get_to(v.x);
			j.at(1).get_to(v.y);
			j.at(2).get_to(v.z);
		}
	};
}

void to_json(json& j, const MaterialProperties& p) {
	j = json{
		{"albedo", p.albedo},
		{"metallic", p.metallic},
		{"roughness", p.roughness},
		{"ambientOcclusion", p.ambientOcclusion},
		{"opacity", p.opacity},
		{"IOR", p.IOR},
		{"alphamode", p.alphamode},
		{"maskthreshold", p.maskthreshold},
		{"twosided", p.twosided}
	};
}

void from_json(const json& j, MaterialProperties& p) {
	// 基础属性
	j.at("albedo").get_to(p.albedo);
	j.at("metallic").get_to(p.metallic);
	j.at("roughness").get_to(p.roughness);
	j.at("ambientOcclusion").get_to(p.ambientOcclusion);
	j.at("opacity").get_to(p.opacity);
	j.at("IOR").get_to(p.IOR);
	j.at("alphamode").get_to(p.alphamode);
	j.at("maskthreshold").get_to(p.maskthreshold);
	j.at("twosided").get_to(p.twosided);
}

static bool LoadJsonFile(const std::string& filePath, json& j)
{
	if (!fs::exists(filePath)) return false;

	std::ifstream file(filePath);
	if (!file.is_open()) return false;

	try
	{
		file >> j;
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

static bool SaveJsonFile(const std::string& filePath, const json& j)
{
	std::ofstream file(filePath);
	if (!file.is_open()) return false;

	try
	{
		file << j.dump();
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

bool StaticMeshAssetDescription::LoadFromFile(const std::string& filePath)
{
	json content;
	if (!LoadJsonFile(filePath, content))
		return false;

	if (content.contains("StaticMeshes"))
	{
		for (const auto& a : content["StaticMeshes"])
		{
			auto path = a.value("path", "");
			if (path.empty()) continue;
			_paths.push_back(std::move(path));
		}
	}

	return true;
}

bool StaticMeshAssetDescription::SaveToFile(const std::string& filePath)
{
	json content;
	for (const auto& path : _paths)
	{
		json a;
		a["path"] = path;
		content["StaticMeshes"].push_back(a);
	}

	return SaveJsonFile(filePath, content);
}

std::vector<AssetPath>& StaticMeshAssetDescription::GetPath() { return _paths; }

void StaticMeshAssetDescription::SetPath(const std::vector<AssetPath>& paths)
{
	_paths = paths;
}

bool TextureAssetDescription::LoadFromFile(const std::string& filePath)
{
	json content;
	if (!LoadJsonFile(filePath, content))
		return false;

	if (content.contains("TextureSetting"))
	{
		auto& a = content["TextureSetting"];
		_path = a.value("path", "");
		_config.minFilter = a.value("minFilter", _config.minFilter);
		_config.magFilter = a.value("magFilter", _config.magFilter);
		_config.wrapS = a.value("wrapS", _config.wrapS);
		_config.wrapT = a.value("wrapT", _config.wrapT);
		_config.anisotropy = a.value("anisotropy", _config.anisotropy);
		_config.gammaCorrection = a.value("gammaCorrection", _config.gammaCorrection);
	}

	return true;
}

bool TextureAssetDescription::SaveToFile(const std::string& filePath)
{
	json content;

	{
		json a;
		a["path"] = _path;
		a["minFilter"] = _config.minFilter;
		a["magFilter"] = _config.magFilter;
		a["wrapS"] = _config.wrapS;
		a["wrapT"] = _config.wrapT;
		a["anisotropy"] = _config.anisotropy;
		a["gammaCorrection"] = _config.gammaCorrection;
		content["TextureSetting"] = a;
	}

	return SaveJsonFile(filePath, content);
}

AssetPath TextureAssetDescription::GetPath() const { return _path; }

TextureConfig TextureAssetDescription::GetConfig() const { return _config; }

void TextureAssetDescription::SetPath(const AssetPath& assetPath) { _path = assetPath; }

void TextureAssetDescription::SetConfig(const TextureConfig& config) { _config = config; }

bool AduioAssetDescription::LoadFromFile(const std::string& filePath)
{
	json content;
	if (!LoadJsonFile(filePath, content))
		return false;

	if (content.contains("AudioSetting"))
	{
		auto& a = content["AudioSetting"];
		_path = a.value("path", "");
	}

	return true;
}

bool AduioAssetDescription::SaveToFile(const std::string& filePath)
{
	json content;

	{
		json a;
		a["path"] = _path;
		content["AudioSetting"] = a;
	}

	return SaveJsonFile(filePath, content);
}

AssetPath AduioAssetDescription::GetPath() const { return _path; }

void AduioAssetDescription::SetPath(const AssetPath& assetPath) { _path = assetPath; }

bool MaterialAssetDescription::LoadFromFile(const std::string& filePath)
{
	json content;
	if (!LoadJsonFile(filePath, content))
		return false;
	_prop = content["prop"].get<MaterialProperties>();
	_dependencyTexture = content["dependency"].get<std::map<TextureType, AssetGUID>>();
	return true;
}

bool MaterialAssetDescription::SaveToFile(const std::string& filePath)
{
	json content;
	content["prop"] = _prop;
	content["dependency"] = _dependencyTexture;
	return SaveJsonFile(filePath, content);
}

MaterialProperties MaterialAssetDescription::GetProperties() const { return _prop; }

std::map<TextureType, AssetGUID> MaterialAssetDescription::GetTexture() const { return _dependencyTexture; }

void MaterialAssetDescription::SetProperties(const MaterialProperties& prop) { _prop = prop; }

void MaterialAssetDescription::SetTexture(TextureType type, const AssetGUID& guid) { _dependencyTexture[type] = guid; }

void MaterialAssetDescription::SetTexture(const std::map<TextureType, AssetGUID>& map) { _dependencyTexture = map; }

bool ModelAssetDescription::LoadFromFile(const std::string& filePath)
{
	json content;
	if (!LoadJsonFile(filePath, content))
		return false;
	_dependencyStaticMesh = content["meshes"].get<std::vector<AssetGUID>>();
	_dependencyMaterial = content["materials"].get<std::unordered_map<uint32_t, AssetGUID>>();
	return true;
}

bool ModelAssetDescription::SaveToFile(const std::string& filePath)
{
	json content;
	content["meshes"] = _dependencyStaticMesh;
	content["materials"] = _dependencyMaterial;
	return SaveJsonFile(filePath, content);
}

std::vector<AssetGUID> ModelAssetDescription::GetStaticMesh() const { return _dependencyStaticMesh; }

std::unordered_map<uint32_t, AssetGUID> ModelAssetDescription::GetMaterial() const { return _dependencyMaterial; }

void ModelAssetDescription::AddStaticMesh(const AssetGUID& guid) { _dependencyStaticMesh.push_back(guid); }

void ModelAssetDescription::SetMaterial(uint32_t index, const AssetGUID& guid) { _dependencyMaterial[index] = guid; }

void ModelAssetDescription::SetMaterial(const std::unordered_map<uint32_t, AssetGUID>& materials) { _dependencyMaterial = materials; }
