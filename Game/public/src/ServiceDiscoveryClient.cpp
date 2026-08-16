#include "ServiceDiscoveryClient.h"
#include "command.h"

using namespace ServiceDiscoveryCommand;

ServiceDiscoveryClient::ServiceDiscoveryClient()
	: isConnected{ false }
{
}

ServiceDiscoveryClient::ServiceDiscoveryClient(const std::string& IP, int Port)
	: _ip(IP), _port(Port), isConnected{ false }
{
}

ServiceDiscoveryClient::~ServiceDiscoveryClient()
{
	LockGuard guard(_sessionStatusChangeLock);
	isConnected.store(false);
	_client.Release();
}

void ServiceDiscoveryClient::SetAddr(const std::string& IP)
{
	if (_ip == IP)
		return;

	LockGuard guard(_sessionStatusChangeLock);
	isConnected.store(false);
	_ip = IP;
	_client.Release();
}

void ServiceDiscoveryClient::SetPort(int Port)
{
	if (_port == Port)
		return;

	LockGuard guard(_sessionStatusChangeLock);
	isConnected.store(false);
	_port = Port;
	_client.Release();
}

void ServiceDiscoveryClient::SetEndpoint(const std::string& IP, int Port)
{
	if (_port == Port && _ip == IP)
		return;

	LockGuard guard(_sessionStatusChangeLock);
	isConnected.store(false);
	_ip = IP;
	_port = Port;
	_client.Release();
}

Task<bool> ServiceDiscoveryClient::GetAvailableServiceInfo(ServiceType type, std::vector<ServiceInfo>& services)
{
	if (!co_await Connect())
		co_return false;

	json js_request;
	js_request["command"] = ServiceDiscovery_GetServiceInfo;
	js_request["type"] = (int)type;

	json js_response;
	if (!co_await Request(js_request, js_response) || js_response.is_null())
		co_return false;

	int result = js_response.value("result", -1);
	if (result != 1)
		co_return false;

	if (!js_response.contains("services") || !js_response["services"].is_array())
		co_return false;

	json js_services = js_response["services"];
	for (int i = 0; i < js_services.size(); i++)
	{
		ServiceInfo info = ServiceInfo::create_from_json(js_services.at(i));
		if (info.service_id.empty())
			co_return false;

		services.emplace_back(std::move(info));
	}

	co_return true;
}

Task<bool> ServiceDiscoveryClient::GetAllAvailableServiceInfo(std::vector<ServiceInfo>& services)
{
	if (!co_await Connect())
		co_return false;

	json js_request;
	js_request["command"] = ServiceDiscovery_GetAllServiceInfo;

	json js_response;
	if (!co_await Request(js_request, js_response) || js_response.is_null())
		co_return false;

	int result = js_response.value("result", -1);
	if (result != 1)
		co_return false;

	if (!js_response.contains("services") || !js_response["services"].is_array())
		co_return false;

	json js_services = js_response["services"];
	for (int i = 0; i < js_services.size(); i++)
	{
		auto info = ServiceInfo::create_from_json(js_services.at(i));
		if (info.service_id.empty())
			co_return false;

		services.emplace_back(std::move(info));
	}

	co_return true;
}

Task<bool> ServiceDiscoveryClient::GetAvailableServiceInfoByServicesIds(std::vector<std::string>& serviceid_list, std::vector<ServiceInfo>& services)
{
	if (!co_await Connect())
		co_return false;

	json js_request;
	js_request["command"] = ServiceDiscovery_GetServiceInfoByServiceIds;

	json js_ids = json::array();
	for (auto& serviceid : serviceid_list)
		js_ids.push_back(serviceid);

	js_request["serviceids"] = js_ids;

	json js_response;
	if (!co_await Request(js_request, js_response) || js_response.is_null())
		co_return false;

	int result = js_response.value("result", -1);
	if (result != 1)
		co_return false;

	if (!js_response.contains("services") || !js_response["services"].is_array())
		co_return false;

	json js_services = js_response["services"];
	for (int i = 0; i < js_services.size(); i++)
	{
		auto info = ServiceInfo::create_from_json(js_services.at(i));
		if (info.service_id.empty())
			co_return false;

		services.emplace_back(std::move(info));
	}

	co_return true;
}

Task<bool> ServiceDiscoveryClient::Connect()
{
	if (!isConnected.load())
	{
		LockGuard guard(_sessionStatusChangeLock);
		if (!isConnected.load())
		{
			_client.Release();
			isConnected.store(co_await _client.Connect(_ip, _port));
		}
	}
	co_return isConnected;
}

Task<bool> ServiceDiscoveryClient::Request(json& src, json& dest)
{
	co_return co_await _client.Request(src, dest);
}
