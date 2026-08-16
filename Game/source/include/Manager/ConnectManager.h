#pragma once

#include "ApplicationLayerCommunication/JsonProtocolClient.h"
#include "ServiceRegistryData.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

//用于管理与服务器的连接

enum class ConnectStatus
{
	disconnect = 0,
	connecting = 1,
	connected = 2
};

class ConnectManager
{

public:
	static ConnectManager* Instance();
	~ConnectManager();

	Task<bool> Login();
	Task<void> Logout();

	Task<bool> LoginGameSeervice(NetworkEndpoint endpoint);
	bool LogoutGameSeervice();

	bool SocialSend(json& js);
	Task<bool> SocialRequest(json& js_req, json& js_resp);

	bool GameSend(json& js_);
	Task<bool> GameRequest(json& js_req, json& js_resp);

	ConnectStatus connectStatus();

public:
	Task<void> OnSessionClose(JsonProtocolClient* _socaialSession);
	Task<void> OnRecvMessage(JsonProtocolClient* session, json& src);

	std::shared_ptr<JsonProtocolClient> SocialSession();
	std::shared_ptr<JsonProtocolClient> GameSession();

private:
	ConnectManager();

	bool InnerSocialSend(const json& js);
	bool InnerGameSend(const json& buf);

	Task<bool> InnerSocialRequest(const json& js_req, json& js_resp);
	Task<bool> InnerGameRequest(const json& js_req, json& js_resp);

	Task<bool> ProcessLoginRequest();
	Task<bool> ProcessLogoutRequest();

	Task<bool> ProcessLoginGameSeerviceRequest();
	Task<bool> ProcessLogoutGameSeerviceRequest();

private:
	ConnectStatus _status = ConnectStatus::disconnect;
	std::shared_ptr<JsonProtocolClient> _socaialSession = nullptr;
	std::shared_ptr<JsonProtocolClient> _gameSession = nullptr;
};

#define CONNECTMANAGER ConnectManager::Instance()
