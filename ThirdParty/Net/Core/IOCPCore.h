#pragma once

#include "Connection/BaseTransportConnection.h"
#include "Core/DeleteLater.h"

#include "NetExportMarco.h"

class NET_API IOCPCoreProcessImpl;

class IOCPCoreProcess
{
public:
	static IOCPCoreProcess* Instance();

	int Run();
	void Stop();
	bool Running();

	bool AddNetFd(std::shared_ptr<BaseTransportConnection> Con);
	bool DelNetFd(BaseTransportConnection* Con);
	bool SendRes(std::shared_ptr<BaseTransportConnection> BaseCon);
	void AddPendingDeletion(DeleteLaterImpl* ptr);

	IOCPCoreProcess(const IOCPCoreProcess&) = delete;
	IOCPCoreProcess& operator=(const IOCPCoreProcess&) = delete;

private:
	IOCPCoreProcess();
	~IOCPCoreProcess() = default;

private:
	std::unique_ptr<IOCPCoreProcessImpl> pImpl;
};
