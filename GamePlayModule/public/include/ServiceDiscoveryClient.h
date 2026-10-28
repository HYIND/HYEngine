#pragma once

#include "ServiceRegistryData.h"
#include "ApplicationLayerCommunication/JsonProtocolClient.h"

using namespace ServiceRegistryDataDef;

// 服务发现客户端
class ServiceDiscoveryClient
{
public:
	ServiceDiscoveryClient();
	ServiceDiscoveryClient(const std::string& IP, int Port);
	~ServiceDiscoveryClient();

	void SetAddr(const std::string& IP);
	void SetPort(int Port);
	void SetEndpoint(const std::string& IP, int Port);

	Task<bool> GetAvailableServiceInfo(ServiceType type, std::vector<ServiceInfo>& services);
	Task<bool> GetAllAvailableServiceInfo(std::vector<ServiceInfo>& services);
	Task<bool> GetAvailableServiceInfoByServicesIds(std::vector<std::string>& serviceid_list, std::vector<ServiceInfo>& services);

private:
	Task<bool> Connect();
	Task<bool> Request(json& src, json& dest);

private:
	std::string _ip;
	int _port;

	JsonProtocolClient _client;
	std::atomic<bool> isConnected;
	CoroCriticalSectionLock _sessionStatusChangeLock;
};