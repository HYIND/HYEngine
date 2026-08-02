#pragma once

#include "Connection/TCPTransportConnection.h"
#include "Core/DeleteLater.h"
#include "Coroutine.h"


enum class TCPNetProtocol
{
	None = 0,
	PureTCP = 10,
	WebSocket = 20
};

enum class CheckHandshakeStatus
{
	None = -1,
	Fail = 0,       // 失败
	Success = 1,    // 成功
	BufferAgain = 2 // 等待数据
};

// TCP终端
class NET_API TCPEndPoint : public DeleteLaterImpl
{
public:
	TCPEndPoint();
	virtual ~TCPEndPoint();

	virtual Task<bool> Connect(std::string IP, uint16_t Port);

	virtual bool Release();

	virtual Task<void> OnRecvBuffer(Buffer* buffer) = 0;
	virtual Task<void> OnConnectClose() = 0;

	virtual bool Send(const Buffer& buffer) = 0;

	std::shared_ptr<TCPTransportConnection> GetBaseCon();

	void SetHandShakeTimeOut(uint32_t ms);
	uint32_t GetHandShakeTimeOut();

public:
	virtual Task<bool> TryHandshake() = 0;   // 作为发起连接的一方，主动发送握手信息
#
	virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer& buffer) = 0;     // 作为接受连接的一方，检查连接发起者的握手信息，并返回回复信息
	virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer& buffer) = 0; // 作为发起连接的一方，检查连接接受者的返回的回复信息，若确认则连接建立

	Task<void> RecvBuffer(TCPTransportConnection* con, Buffer* buffer); // 用于绑定网络层(TCP/UDP)触发的Buffer回调
	Task<void> ConnectClose(TCPTransportConnection* con);               // 用于绑定网络层(TCP/UDP)触发的RDHUP回调

	Task<void> BindMessageCallBack(std::function<Task<void>(TCPEndPoint*, Buffer*)> callback);
	Task<void> BindCloseCallBack(std::function<Task<void>(TCPEndPoint*)> callback);

protected:
	virtual Task<void> OnBindMessageCallBack() = 0;
	virtual Task<void> OnBindCloseCallBack() = 0;

protected:
	TCPNetProtocol Protocol;
	std::shared_ptr<TCPTransportConnection> BaseCon;
	bool isHandshakeComplete = false;

	std::function<Task<void>(TCPEndPoint*, Buffer*)> _callbackMessage;
	std::function<Task<void>(TCPEndPoint*)> _callbackClose;

	std::shared_ptr<CoTimer> _handshaketimer;

	uint32_t _handshaketimeOutMs = 10 * 1000;
};
