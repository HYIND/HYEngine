#include "ServiceRegistryData.h"

using namespace ServiceRegistryDataDef;

void NetworkEndpoint::update_from_json(const json &j)
{
	if (j.contains("ip") && j["ip"].is_string())
		ip = j.value("ip", ip);
	if (j.contains("port") && j["port"].is_number_integer())
		port = j.value("port", port);
}
json NetworkEndpoint::to_json() const
{
    return {
        {"ip", ip},
        {"port", port}};
}

json LobbyMetadata::to_json() const
{
    return {
        {"max_players", max_players},
        {"current_players", current_players}};
}

void LobbyMetadata::update_from_json(const json &j)
{
    max_players = j.value("max_players", max_players);
    current_players = j.value("current_players", current_players);
}

json GameMetadata::to_json() const
{
    return {
        {"max_rooms", max_rooms},
        {"current_rooms", current_rooms},
        {"tick_rate", tick_rate},
        {"avg_latency_ms", avg_latency_ms},
        {"packet_loss_percent", packet_loss_percent}};
}

void GameMetadata::update_from_json(const json &j)
{
    max_rooms = j.value("max_rooms", max_rooms);
    current_rooms = j.value("current_rooms", current_rooms);
    tick_rate = j.value("tick_rate", tick_rate);
    avg_latency_ms = j.value("avg_latency_ms", avg_latency_ms);
    packet_loss_percent = j.value("packet_loss_percent", packet_loss_percent);
}

json ServiceRegistryDataDef::GameStateMetadata::to_json() const
{
    return {};
}

void ServiceRegistryDataDef::GameStateMetadata::update_from_json(const json &j)
{
}

DynamicMetadata::DynamicMetadata(ServiceType type)
{
    switch (type)
    {
    case ServiceType::GAMESTATE:
        _typed_metadata = GameStateMetadata{};
        break;
    case ServiceType::LOBBY:
        _typed_metadata = LobbyMetadata{};
        break;
    case ServiceType::GAME:
        _typed_metadata = GameMetadata{};
        break;
    }
    _type = type;
}

void DynamicMetadata::update(const json &j)
{
    // 1. 更新类型化元数据
    std::visit([&j](auto &metadata)
               { metadata.update_from_json(j); }, _typed_metadata);

    // 2. 更新额外元数据（不包含已处理的字段）
    static std::map<ServiceType, std::set<std::string>> standard_fields_map = {
        {ServiceType::LOBBY, {"max_players", "current_players"}},
        {ServiceType::GAME, {"max_rooms", "current_rooms", "tick_rate", "avg_latency_ms", "packet_loss_percent"}},
    };
    auto &standard_fields = standard_fields_map[_type];
    for (auto &[key, value] : j.items())
    {
        // 检查是否是标准字段
        if (standard_fields.find(key) != standard_fields.end())
            continue;
        _extra_metadata[key] = value;
    }
}

json DynamicMetadata::get_extra(const std::string &key) const
{
    return _extra_metadata.value(key, json());
}

void DynamicMetadata::set_extra(const std::string &key, const json &value)
{
    _extra_metadata[key] = value;
}

json DynamicMetadata::to_json() const
{
    json j;

    // 添加类型化元数据
    std::visit([&j](const auto &metadata)
               { json metajson =metadata.to_json();
                if(!metajson.is_null())
                 j.update(metajson); },
               _typed_metadata);

    // 添加额外元数据
    if (!_extra_metadata.is_null())
        j.update(_extra_metadata);

    return j;
}

std::optional<int> DynamicMetadata::get_current_load() const
{
    return std::visit([](const auto &metadata) -> std::optional<int>
                      {
            using T = std::decay_t<decltype(metadata)>;
            
            if constexpr (std::is_same_v<T, LobbyMetadata>) {
                return metadata.current_players;
            } else if constexpr (std::is_same_v<T, GameMetadata>) {
                return metadata.current_rooms;
            }
            return std::nullopt; }, _typed_metadata);
}

std::optional<int> DynamicMetadata::get_max_capacity() const
{
    return std::visit([](const auto &metadata) -> std::optional<int>
                      {
            using T = std::decay_t<decltype(metadata)>;
            
            if constexpr (std::is_same_v<T, LobbyMetadata>) {
                return metadata.max_players;
            } else if constexpr (std::is_same_v<T, GameMetadata>) {
                return metadata.max_rooms;
            }
            return std::nullopt; }, _typed_metadata);
}

std::optional<float> DynamicMetadata::get_load_percentage() const
{
    auto current = get_current_load();
    auto max = get_max_capacity();

    if (current && max && *max > 0)
    {
        return static_cast<float>(*current) / *max;
    }
    return std::nullopt;
}

ServiceInfo::ServiceInfo(const std::string &service_id, ServiceType type)
    : service_id(service_id),
      service_type(type),
      status(ServiceStatus::STARTING),
      metadata_(type)
{
}

void ServiceInfo::set_endpoint(const std::string &ip, uint16_t port)
{
    endpoint.ip = ip;
    endpoint.port = port;
}

void ServiceRegistryDataDef::ServiceInfo::set_delegate_endpoint(const std::string &ip, uint16_t port)
{
    delegate_endpoint.ip = ip;
    delegate_endpoint.port = port;
}

// 获取Lobby元数据（方便使用）
LobbyMetadata *ServiceInfo::lobby_metadata()
{
    return metadata_.get<LobbyMetadata>();
}

const LobbyMetadata *ServiceInfo::lobby_metadata() const
{
    return metadata_.get<LobbyMetadata>();
}

// 获取Game元数据
GameMetadata *ServiceInfo::game_metadata()
{
    return metadata_.get<GameMetadata>();
}

const GameMetadata *ServiceInfo::game_metadata() const
{
    return metadata_.get<GameMetadata>();
}

GameStateMetadata *ServiceRegistryDataDef::ServiceInfo::gamestate_metadata()
{
    return metadata_.get<GameStateMetadata>();
}

const GameStateMetadata *ServiceRegistryDataDef::ServiceInfo::gamestate_metadata() const
{
    return metadata_.get<GameStateMetadata>();
}

// ========== 通用元数据操作 ==========
void ServiceInfo::update_from_json(const json &j)
{
    update_metadata(j);
}

void ServiceInfo::update_metadata(const json &j)
{
    metadata_.update(j);
}

json ServiceInfo::get_extra_metadata(const std::string &key) const
{
    return metadata_.get_extra(key);
}

void ServiceInfo::set_extra_metadata(const std::string &key, const json &value)
{
    metadata_.set_extra(key, value);
}

// 快速获取负载信息
std::optional<int> ServiceInfo::current_load() const
{
    return metadata_.get_current_load();
}

std::optional<float> ServiceInfo::load_percentage() const
{
    return metadata_.get_load_percentage();
}

// ========== 序列化 ==========
json ServiceInfo::to_json() const
{
    return {
        {"service_id", service_id},
        {"service_type", static_cast<int>(service_type)},
        {"status", static_cast<int>(status)},
        {"metadata", metadata_.to_json()},
        {"endpoint", {{"ip", endpoint.ip}, {"port", endpoint.port}}},
        {"delegate_endpoint", {{"ip", delegate_endpoint.ip}, {"port", delegate_endpoint.port}}}};
}

std::shared_ptr<ServiceInfo> ServiceInfo::create_from_json_shared(const json &j)
{
    auto serviceinfo = std::make_shared<ServiceInfo>(
        j.value("service_id", ""),
        static_cast<ServiceType>(j.value("service_type", 0)));

    serviceinfo->status = static_cast<ServiceStatus>(j.value("status", 0));

    if (j.contains("endpoint"))
    {
        serviceinfo->endpoint.ip = j["endpoint"].value("ip", "");
        serviceinfo->endpoint.port = j["endpoint"].value("port", 0);
    }

    if (j.contains("delegate_endpoint"))
    {
        serviceinfo->delegate_endpoint.ip = j["delegate_endpoint"].value("ip", "");
        serviceinfo->delegate_endpoint.port = j["delegate_endpoint"].value("port", 0);
    }

    if (j.contains("metadata"))
    {
        serviceinfo->metadata_.update(j["metadata"]);
    }

    return serviceinfo;
}

ServiceInfo ServiceInfo::create_from_json(const json &j)
{
    ServiceInfo serviceinfo(j.value("service_id", ""), static_cast<ServiceType>(j.value("service_type", 0)));

    serviceinfo.status = static_cast<ServiceStatus>(j.value("status", 0));

    if (j.contains("endpoint"))
    {
        serviceinfo.endpoint.ip = j["endpoint"].value("ip", "");
        serviceinfo.endpoint.port = j["endpoint"].value("port", 0);
    }

    if (j.contains("delegate_endpoint"))
    {
        serviceinfo.delegate_endpoint.ip = j["delegate_endpoint"].value("ip", "");
        serviceinfo.delegate_endpoint.port = j["delegate_endpoint"].value("port", 0);
    }

    if (j.contains("metadata"))
    {
        serviceinfo.metadata_.update(j["metadata"]);
    }

    return std::move(serviceinfo);
}