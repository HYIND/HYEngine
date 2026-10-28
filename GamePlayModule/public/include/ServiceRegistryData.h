#pragma once

#include <variant>
#include <set>
#include <chrono>
#include <iostream>
#include "stdafx.h"

struct NetworkEndpoint
{
	std::string ip; // IP或域名
	uint16_t port;  // 端口

	void update_from_json(const json& j);
	json to_json() const;
};

namespace ServiceRegistryDataDef
{
    enum class ServiceType
    {
        MinValue = -1,
        GAMESTATE = 0,
        LOBBY = 1,
        GAME = 2,
        MaxValue
    };

    enum class ServiceStatus
    {
        None = -1,
        STARTING = 0, // 启动中
        DRAINING = 1, // 排空中
        OFFLINE = 2   // 离线
    };

    // GameState服务专用元数据
    struct GameStateMetadata
    {
        json to_json() const;                 // 转换为JSON
        void update_from_json(const json &j); // 从JSON更新
    };

    // Lobby服务专用元数据
    struct LobbyMetadata
    {
        int max_players = 0;     // 最大玩家数
        int current_players = 0; // 当前玩家数

        json to_json() const;                 // 转换为JSON
        void update_from_json(const json &j); // 从JSON更新
    };

    // Game服务专用元数据
    struct GameMetadata
    {
        // 游戏相关
        int max_rooms = 0;     // 最大房间数
        int current_rooms = 0; // 当前房间数

        // 性能指标
        float tick_rate = 60.0f;     // Tick频率
        float avg_latency_ms = 0.0f; // 平均延迟
        int packet_loss_percent = 0; // 丢包率

        json to_json() const;
        void update_from_json(const json &j);
    };

    class DynamicMetadata
    {
    private:
        using MetadataVariant = std::variant<
            GameStateMetadata,
            LobbyMetadata,
            GameMetadata>;

        MetadataVariant _typed_metadata;
        ServiceType _type;
        json _extra_metadata; // 额外的动态字段

    public:
        // 根据服务类型构造
        DynamicMetadata(ServiceType type);

        // 获取特定类型的元数据
        template <typename T>
        T *get()
        {
            return std::get_if<T>(&_typed_metadata);
        }

        template <typename T>
        const T *get() const
        {
            return std::get_if<T>(&_typed_metadata);
        }

        void update(const json &j);                                // 更新元数据
        json get_extra(const std::string &key) const;              // 获取额外元数据
        void set_extra(const std::string &key, const json &value); // 设置额外元数据
        json to_json() const;                                      // 转换为完整JSON
        std::optional<int> get_current_load() const;
        std::optional<int> get_max_capacity() const;
        std::optional<float> get_load_percentage() const;
    };

    struct ServiceInfo
    {
        std::string service_id;   // 服务唯一ID
        ServiceType service_type; // 服务类型
        ServiceStatus status;     // 服务状态
        DynamicMetadata metadata_;
        
        NetworkEndpoint endpoint;
        NetworkEndpoint delegate_endpoint;

        ServiceInfo(const std::string &service_id, ServiceType type);
        void set_endpoint(const std::string &ip, uint16_t port);
        void set_delegate_endpoint(const std::string &ip, uint16_t port);

        // ========== 类型化元数据访问 ==========
        template <typename T>
        T *metadata()
        {
            return metadata_.get<T>();
        }

        template <typename T>
        const T *metadata() const
        {
            return metadata_.get<T>();
        }

        // 获取Lobby元数据
        LobbyMetadata *lobby_metadata();
        const LobbyMetadata *lobby_metadata() const;
        // 获取Game元数据
        GameMetadata *game_metadata();
        const GameMetadata *game_metadata() const;

        GameStateMetadata *gamestate_metadata();
        const GameStateMetadata *gamestate_metadata() const;

        // ========== 通用元数据操作 ==========
        void update_metadata(const json &j);
        json get_extra_metadata(const std::string &key) const;
        void set_extra_metadata(const std::string &key, const json &value);
        std::optional<int> current_load() const;
        std::optional<float> load_percentage() const;

        // ========== 序列化 ==========
        json to_json() const;
        void update_from_json(const json &j);

        static std::shared_ptr<ServiceInfo> create_from_json_shared(const json &j);
        static ServiceInfo create_from_json(const json &j);
    };
};