#pragma once

#include "EndPoint/WebSocketClient.h"
#include "Session/BaseNetWorkSession.h"

struct CustomWebSocketSessionPakage
{
	int seq = 0;
	int ack = -1;
	Buffer buffer;
};

class NET_API CustomWebSocketSession : public BaseNetWorkSession
{

	// 关注返回值的等待任务
	struct AwaitTask
	{
		int seq = 0;
		Buffer* respsonse;
		std::mutex _mtx;
		std::condition_variable _cv;
		int status = -1; //-1无效，0等待超时，1等待中
		bool time_out = false;
	};

public:
	CustomWebSocketSession(WebSocketClient* client = nullptr);
	~CustomWebSocketSession();
	virtual Task<bool> Connect(std::string IP, uint16_t Port);

	virtual bool Release();

	virtual bool AsyncSend(const Buffer& buffer);           // 异步发送，不关心返回结果
	bool AwaitSend(const Buffer& buffer, Buffer& response); // 等待返回结果的发送，关心返回的结果

	WebSocketClient* GetBaseClient();

public:
	virtual Task<bool> TryHandshake();

	virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer& buffer);
	virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer& buffer);

protected:
	virtual Task<void> OnSessionClose();
	virtual Task<void> OnRecvData(Buffer* buffer);
	virtual Task<void> OnBindRecvDataCallBack();
	virtual Task<void> OnBindSessionCloseCallBack();

private:
	bool Send(const Buffer& buffer, int ack = -1);
	Task<void> ProcessPakage(CustomWebSocketSessionPakage* newPak = nullptr);
	SpinLock _ProcessLock;

private:
	SafeQueue<CustomWebSocketSessionPakage*, CoroCriticalSectionLock> _RecvPaks;

	std::atomic<int> seq;
	SafeMap<int, AwaitTask*> _AwaitMap; // seq->AwaitTask
};
