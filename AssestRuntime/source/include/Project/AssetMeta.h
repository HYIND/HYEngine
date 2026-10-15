#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <variant>

// =================== 虚拟资产 ===================
// 依赖于物理文件，记录物理文件 + 导入配置
// StaticMesh	--- 静态网格体，由一到多个Mesh组合而成，也就是std::vector<Mesh>，通过解析obj、fbx等文件获得
// Texture		--- 贴图 + 导入配置，png、jpg......
// 
// ================== 纯虚拟资产 ====================
// 没有实际的物理文件依赖，只记录虚拟资产的组合，依赖于虚拟资产
// Material		--- 材质球，由一到多个Texture，加上材质属性组合而成，也就是std::map<TextureType,Texture> + Properties;
// Model		--- 模型，由(Mesh + Material)的集合组成，其中Mesh可以由一个或多个StaticMesh解压获得，通过解压后的Mesh下标和材质球下标一一对应连线，理论上来说Mesh数量应该与Material数量相等（包括复用的Material）
// Scene		--- 记录World的状态快照

enum class AssetType {
	Unknown = 0, Texture, Audio, Material, StaticMesh, Model, Scene
};

using AssetGUID = std::string;
using AssetPath = std::string;
using AssetName = std::string;

struct AssetMeta
{
	AssetGUID guid;
	AssetPath path;
	AssetName name;
	AssetType type;
};

class Scene {
};

class IMetadataProvider {
public:
	virtual ~IMetadataProvider() = default;
	virtual std::shared_ptr<AssetMeta> GetAssetMetaByGuid(const AssetGUID& guid) = 0;
};

