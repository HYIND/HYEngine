#pragma once

#include "Connection/TCPTransportListener.h"
#include "EndPoint/TCPEndPoint.h"
#include "Timer.h"

struct ClientData;

// 用于监听指定协议的TCP连接，用于校验连接协议
class NET_API TcpEndPointListener
{
public:
	TcpEndPointListener(TCPNetProtocol proto = TCPNetProtocol::PureTCP);
	~TcpEndPointListener();

	TCPNetProtocol Protocol();
	void SetProtocol(const TCPNetProtocol& proto);
	bool Listen(const std::string& IP, int Port);
	void BindEstablishConnectionCallBack(std::function<Task<void>(TCPEndPoint*)> callback);

private:
	Task<void> RecvCon(std::shared_ptr<TCPTransportConnection> waitCon);
	Task<void> ConClose(TCPTransportConnection* Con);
	Task<void> Handshake(TCPTransportConnection* waitCon, Buffer* buf);
	void CleanExpiredClient();

private:
	std::shared_ptr<TCPTransportListener> BaseListener;
	TCPNetProtocol _Protocol;
	std::function<Task<void>(TCPEndPoint*)> _callBackEstablish;
	SafeArray<std::shared_ptr<ClientData>, CoroCriticalSectionLock> waitClients; // 等待校验协议的客户端
	std::shared_ptr<TimerTask> CleanExpiredTask;
};
