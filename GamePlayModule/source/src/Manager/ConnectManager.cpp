#include "Manager/ConnectManager.h"
#include "ServiceDiscoveryClient.h"
#include "EndPointConfig.h"
#include "Net/Core/NetCore.h"
#include "command.h"
#include "Manager/UserInfoManager.h"
#include "Manager/MsgManager.h"

using namespace ServiceRegistryDataDef;

ConnectManager* ConnectManager::Instance()
{
	static ConnectManager* instance = new ConnectManager();
	return instance;
}

ConnectManager::ConnectManager()
{
	InitNetCore();
	RunNetCoreLoop(false);

	_socaialSession = std::make_shared<JsonProtocolClient>();
	_gameSession = std::make_shared<JsonProtocolClient>();
}

Task<bool> ConnectManager::ProcessLoginRequest()
{
	json login_req, login_res;
	login_req["command"] = LoginSubServiceCommand::LoginSubService_Login;
	login_req["name"] = UserInfoManager::Instance()->username();
	if (!co_await InnerSocialRequest(login_req, login_res))
		co_return false;

	if (!login_res.contains("result") || !login_res["result"].is_number_integer())
		co_return false;

	int result = login_res["result"];
	if (result < 0)
	{
		std::string reason = login_res.value("reason", "");
		std::cout << "LoginSubService_Login Request fail! reason:" << reason << '\n';
		co_return false;
	}

	if (!login_res.contains("jwt") || !login_res["jwt"].is_string()
		|| !login_res.contains("token") || !login_res["token"].is_string()
		|| !login_res.contains("name") || !login_res["name"].is_string())
		co_return false;

	std::string token = login_res["token"];
	std::string jwt = login_res["jwt"];
	std::string name = login_res["name"];

	UserInfoManager::Instance()->setUserJwt(jwt);
	UserInfoManager::Instance()->setUserToken(token);

	co_return true;
}

Task<bool> ConnectManager::ProcessLogoutRequest()
{
	json logout_req, logout_res;
	logout_req["command"] = LobbySubServiceCommand::LobbySubService_Logout;
	if (!SocialRequest(logout_req, logout_res))
		co_return false;

	if (logout_res.contains("result") && logout_res["result"].is_number_integer())
	{
		int result = logout_res["result"];
		if (result < 0)
		{
			std::string reason = logout_res.value("reason", "");
			std::cout << "LobbySubService_Logout Request fail! reason:" << reason << '\n';
			co_return false;
		}
	}

	co_return true;
}

Task<bool> ConnectManager::ProcessLoginGameSeerviceRequest()
{
	co_return true;
}

Task<bool> ConnectManager::ProcessLogoutGameSeerviceRequest()
{
	co_return true;
}

ConnectManager::~ConnectManager()
{
	_socaialSession->Release();
	_gameSession->Release();
}

ConnectStatus ConnectManager::connectStatus()
{
	return _status;
}

Task<void> ConnectManager::OnSessionClose(JsonProtocolClient* c)
{
	_status = ConnectStatus::disconnect;
	//LOGINMODEL->DisConnect();
	co_return;
}

Task<void> ConnectManager::OnRecvMessage(JsonProtocolClient* c, json& src)
{
	try {
		co_await MSGMANAGER->ProcessMsg(src);
	}
	catch (...)
	{
		std::cout << "OnRecvMessage error when process data:" << recv;
	}
}

std::shared_ptr<JsonProtocolClient> ConnectManager::SocialSession()
{
	return _socaialSession;
}

std::shared_ptr<JsonProtocolClient> ConnectManager::GameSession()
{
	return _gameSession;
}

Task<bool> ConnectManager::Login()
{
	_status = ConnectStatus::connecting;

	std::vector<ServiceInfo> services;
	ServiceDiscoveryClient client;
	client.SetEndpoint(ServiceDiscoveryIP, ServiceDiscoveryPort);
	if (!co_await client.GetAvailableServiceInfo(ServiceType::LOBBY, services) || services.empty())
	{
		_status = ConnectStatus::disconnect;
		co_return false;
	}

	for (auto& service : services)
	{
		const std::string IP = service.endpoint.ip;
		int port = service.endpoint.port;

		if (co_await _socaialSession->Connect(IP, port))
		{
			_status = ConnectStatus::connected;

			if (!co_await ProcessLoginRequest())
			{
				_socaialSession->Release();
				_status = ConnectStatus::disconnect;
				co_return false;
			}

			_socaialSession->BindCallBackJsonMessage(std::bind(&ConnectManager::OnRecvMessage, this, std::placeholders::_1, std::placeholders::_2));
			_socaialSession->BindCallBackCloseClient(std::bind(&ConnectManager::OnSessionClose, this, std::placeholders::_1));

			co_return true;
		}

	}
	_status = ConnectStatus::disconnect;
	co_return false;
}

Task<void> ConnectManager::Logout()
{
	if (_status != ConnectStatus::connected)
		co_return;

	co_await ProcessLogoutRequest();
	_socaialSession->Release();
	_gameSession->Release();
	UserInfoManager::Instance()->setUserJwt("");
	UserInfoManager::Instance()->setUserToken("");
	UserInfoManager::Instance()->setUserName("");
	UserInfoManager::Instance()->setGameId("");
	_status = ConnectStatus::disconnect;
}

Task<bool> ConnectManager::LoginGameSeervice(NetworkEndpoint endpoint)
{
	if (endpoint.ip.empty() || endpoint.port == 0)
		co_return false;

	if (co_await _gameSession->Connect(endpoint.ip, endpoint.port))
	{
		if (!co_await ProcessLoginGameSeerviceRequest())
		{
			_gameSession->Release();
			co_return false;
		}

		_gameSession->BindCallBackJsonMessage(std::bind(&ConnectManager::OnRecvMessage, this, std::placeholders::_1, std::placeholders::_2));
		//_gameSession->BindCallBackCloseClient(std::bind(&ConnectManager::OnSessionClose, this, std::placeholders::_1));

		co_return true;
	}
	co_return false;
}

bool ConnectManager::LogoutGameSeervice()
{
	_gameSession->Release();
	UserInfoManager::Instance()->setGameId(""); 
	return true;
}

bool ConnectManager::SocialSend(json& js)
{
	if (_status != ConnectStatus::connected)
		return false;

	if (!js.contains("jwt"))
		js["jwt"] = UserInfoManager::Instance()->userjwt();

	return InnerSocialSend(js);
}

Task<bool> ConnectManager::SocialRequest(json& js_req, json& js_resp)
{
	if (_status != ConnectStatus::connected)
		co_return false;

	if (!js_req.contains("jwt"))
		js_req["jwt"] = UserInfoManager::Instance()->userjwt();

	co_return co_await InnerSocialRequest(js_req, js_resp);
}

bool ConnectManager::GameSend(json& js)
{
	if (!_gameSession)
		return false;

	if (!js.contains("jwt"))
		js["jwt"] = UserInfoManager::Instance()->userjwt();

	if (!js.contains("playerid"))
		js["playerid"] = UserInfoManager::Instance()->usertoken();

	return InnerGameSend(js);
}

Task<bool> ConnectManager::GameRequest(json& js_req, json& js_resp)
{
	if (_status != ConnectStatus::connected)
		co_return false;

	if (!js_req.contains("jwt"))
		js_req["jwt"] = UserInfoManager::Instance()->userjwt();

	co_return co_await InnerGameRequest(js_req, js_resp);
}

bool ConnectManager::InnerSocialSend(const json& js)
{
	if (_status != ConnectStatus::connected)
		return false;

	return _socaialSession->Send(js);
}

bool ConnectManager::InnerGameSend(const json& js)
{
	if (!_gameSession)
		return false;

	return _gameSession->Send(js);
}

Task<bool> ConnectManager::InnerSocialRequest(const json& js_req, json& js_resp)
{
	if (_status != ConnectStatus::connected)
		co_return false;

	co_return co_await _socaialSession->Request(js_req, js_resp);
}

Task<bool> ConnectManager::InnerGameRequest(const json& js_req, json& js_resp)
{
	if (!_gameSession)
		co_return false;

	co_return co_await _gameSession->Request(js_req, js_resp);
}
