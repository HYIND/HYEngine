#pragma once

#include "TCPEndPoint.h"
#include "SpinLock.h"

enum class WSOpcodeType : uint8_t
{
	WSOpcode_Continue = 0x0,
	WSOpcode_Text = 0x1,
	WSOpcode_Binary = 0x2,
	WSOpcode_Close = 0x8,
	WSOpcode_Ping = 0x9,
	WSOpcode_Pong = 0xA,
};

struct WebSocketPackage
{
	WSOpcodeType opcode = WSOpcodeType::WSOpcode_Binary;
	Buffer buffer;
};

// 基于TCP协议的WebSocket应用层客户端客户端封装
class NET_API WebSocketClient : public TCPEndPoint
{
public:
	WebSocketClient(TCPTransportConnection* con = nullptr);
	WebSocketClient(std::shared_ptr<TCPTransportConnection> con);
	~WebSocketClient();

public:
	virtual Task<bool> Connect(std::string IP, uint16_t Port);

	virtual bool Release();

	virtual Task<void> OnRecvBuffer(Buffer* buffer); // 用于绑定网络层(TCP/UDP)触发的Buffer回调
	virtual Task<void> OnConnectClose();

	virtual bool Send(const Buffer& buffer);

public:
	virtual Task<bool> TryHandshake();

	virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer& buffer);
	virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer& buffer);

protected:
	virtual Task<void> OnBindMessageCallBack();
	virtual Task<void> OnBindCloseCallBack();

private:
	Task<void> ProcessPakage(WebSocketPackage* newpak = nullptr);
	SpinLock _ProcessLock;

private:
	SafeQueue<WebSocketPackage*, CoroCriticalSectionLock> _RecvPaks;
	SafeQueue<WebSocketPackage*, CoroCriticalSectionLock> _SendPaks;

	Buffer cacheBuffer;         // 握手消息/数据帧解析缓冲
	WebSocketPackage* cachePak; // 多帧数据组成的完整帧缓冲

	// 握手用
	std::string _SecWsKey;
};