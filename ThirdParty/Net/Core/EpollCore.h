#pragma once

#include "Core/DeleteLater.h"
#include "Connection/BaseTransportConnection.h"

#include "NetExportMarco.h"

class EpollCoreProcessImpl;

class NET_API EpollCoreProcess
{
public:
    static EpollCoreProcess *Instance();

    int Run();
    void Stop();
    bool Running();

    bool AddNetFd(std::shared_ptr<BaseTransportConnection> Con);
    bool DelNetFd(BaseTransportConnection *Con);
    bool SendRes(std::shared_ptr<BaseTransportConnection> BaseCon);
    void AddPendingDeletion(DeleteLaterImpl *ptr);

    EpollCoreProcess(const EpollCoreProcess &) = delete;
    EpollCoreProcess &operator=(const EpollCoreProcess &) = delete;

private:
    EpollCoreProcess();

private:
    std::unique_ptr<EpollCoreProcessImpl> pImpl;
};