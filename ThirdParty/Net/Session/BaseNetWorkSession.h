#pragma once

#include "EndPoint/TCPEndPoint.h"
#include "Core/DeleteLater.h"
#include "Coroutine.h"
#include "CriticalSectionLock.h"

class NET_API BaseNetWorkSession : public DeleteLaterImpl
{

public:
	BaseNetWorkSession();
	virtual ~BaseNetWorkSession();
	virtual Task<bool> Connect(std::string IP, uint16_t Port);

	virtual bool Release();

public: // 供Listener/EndPoint调用,须继承实现
	virtual bool AsyncSend(const Buffer& buffer) = 0;
	virtual Task<bool> TryHandshake() = 0;

	virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer& buffer) = 0;
	virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer& buffer) = 0;

public: // 供外部调用
	Task<void> BindRecvDataCallBack(std::function<Task<void>(BaseNetWorkSession*, Buffer* recv)> callback);
	Task<void> BindSessionCloseCallBack(std::function<Task<void>(BaseNetWorkSession*)> callback);
	char* GetIPAddr();
	uint16_t GetPort();

	void SetHandShakeTimeOut(uint32_t ms);
	uint32_t GetHandShakeTimeOut();

public: // 供Listener/EndPoint调用
	Task<void> RecvData(TCPEndPoint* client, Buffer* buffer);
	Task<void> SessionClose(TCPEndPoint* client);
	TCPEndPoint* GetBaseClient();

protected: // 须继承实现
	virtual Task<void> OnSessionClose() = 0;
	virtual Task<void> OnRecvData(Buffer* buffer) = 0;
	virtual Task<void> OnBindRecvDataCallBack() = 0;
	virtual Task<void> OnBindSessionCloseCallBack() = 0;

protected:
	TCPEndPoint* BaseClient;

	bool isHandshakeComplete;

	std::function<Task<void>(BaseNetWorkSession*, Buffer* recv)> _callbackRecvData;
	std::function<Task<void>(BaseNetWorkSession*)> _callbackSessionClose;

	std::shared_ptr<CoTimer> _handshaketimer;

	uint32_t _handshaketimeOutMs = 10 * 1000;
};
