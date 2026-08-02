#pragma once

#include "Connection/BaseTransportConnection.h"
#include "Core/DeleteLater.h"

#include "NetExportMarco.h"

class IOuringCoreProcessImpl;

class NET_API IOuringCoreProcess
{
public:
    static IOuringCoreProcess *Instance();

    int Run();
    void Stop();
    bool Running();

    bool AddNetFd(std::shared_ptr<BaseTransportConnection> Con);
    bool DelNetFd(BaseTransportConnection *Con);
    bool SendRes(std::shared_ptr<BaseTransportConnection> BaseCon);
    void AddPendingDeletion(DeleteLaterImpl *ptr);

    IOuringCoreProcess(const IOuringCoreProcess &) = delete;
    IOuringCoreProcess &operator=(const IOuringCoreProcess &) = delete;

private:
    IOuringCoreProcess();

private:
    std::unique_ptr<IOuringCoreProcessImpl> pImpl;
};
