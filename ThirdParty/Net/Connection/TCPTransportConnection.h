
/*
	该文件是对TCP传输层协议下的TCP连接对象的封装
 */

#pragma once

#include "BaseTransportConnection.h"
#include "SafeStl.h"
#include "SpinLock.h"

 // TCP传输层客户端(连接对象)
class NET_API TCPTransportConnection : public BaseTransportConnection
{

public:
	TCPTransportConnection();
	~TCPTransportConnection();
	Task<bool> Connect(std::string IP, uint16_t Port);

	void Apply(const BaseSocket fd, const sockaddr_in& sockaddr, const SocketType type);
	bool Release();
	bool Send(const Buffer& buffer);

	Task<void> BindBufferCallBack(std::function<Task<void>(TCPTransportConnection*, Buffer*)> callback);
	Task<void> BindRDHUPCallBack(std::function<Task<void>(TCPTransportConnection*)> callback);

	SafeQueue<Buffer*, CoroCriticalSectionLock>& GetRecvData();
	SafeQueue<Buffer*, CoroCriticalSectionLock>& GetSendData();
	CoroCriticalSectionLock& GetSendMtx();

protected:
#ifdef __linux__
	virtual Task<void> OnREAD(BaseSocket socket);									// 可读事件
	virtual Task<void> OnACCEPT(BaseSocket socket);									// 接受新连接事件
#endif
	virtual Task<void> OnREAD(BaseSocket socket, Buffer& buf);						// 可读事件
	virtual Task<void> OnACCEPT(BaseSocket socket, BaseSocket newsocket, sockaddr_in addr); // 接受新连接事件
	virtual Task<void> OnRDHUP();

protected:
	virtual Task<void> OnBindBufferCallBack();
	virtual Task<void> OnBindRDHUPCallBack();

private:
	Task<void> ProcessRecvQueue();

private:
	SafeQueue<Buffer*, CoroCriticalSectionLock> _RecvDatas;
	SafeQueue<Buffer*, CoroCriticalSectionLock> _SendDatas;

private:
	std::function<Task<void>(TCPTransportConnection*, Buffer*)> _callbackBuffer;
	std::function<Task<void>(TCPTransportConnection*)> _callbackRDHUP;
	CoroCriticalSectionLock _SendResMtx;
	SpinLock _ProcessLock;
};


