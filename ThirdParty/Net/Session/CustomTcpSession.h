#pragma once

#include "EndPoint/PureTCPClient.h"
#include "Session/BaseNetWorkSession.h"
#include "SpinLock.h"

struct CustomPackage
{
	int seq = 0;
	int ack = -1;
	Buffer buffer;
	uint8_t msgType = 0; // 0:null, 1:请求, 2:响应
};

enum class AwaitErrorCode
{
	InnerError = -1,
	Success = 0,
	InvalidBuffer = 1,
	TimeOut = 2,
	NetWorkError = 3
};
struct AwaitResult
{
	AwaitErrorCode code;
	Buffer response;
};

// 基于TCP应用层客户端的自定义通讯协议会话封装
class NET_API CustomTcpSession : public BaseNetWorkSession
{
	// 关注返回值的等待任务
	struct AwaitTask
	{
		int seq = 0;
		std::shared_ptr<AwaitResult> res;
		std::shared_ptr<CoTimer> timer;
	};

public:
	CustomTcpSession(PureTCPClient* client = nullptr);
	~CustomTcpSession();
	virtual Task<bool> Connect(std::string IP, uint16_t Port);

	virtual bool Release();

	bool AsyncSend(const Buffer& buffer);                   // 异步发送，不关心返回结果
	Task<std::shared_ptr<AwaitResult>> AwaitSend(Buffer buffer, std::chrono::milliseconds timeout = std::chrono::milliseconds(5 * 1000)); // 等待返回结果的发送，关心返回的结果
	PureTCPClient* GetBaseClient();

	Task<void> BindRecvRequestCallBack(std::function<Task<void>(BaseNetWorkSession*, Buffer* recv, Buffer* resp)> callback);

public:
	virtual Task<bool> TryHandshake();

	virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer& buffer);
	virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer& buffer);

protected:
	virtual Task<void> OnSessionClose();
	virtual Task<void> OnRecvData(Buffer* buffer);
	virtual Task<void> OnBindRecvDataCallBack();
	virtual Task<void> OnBindSessionCloseCallBack();

	Task<void> OnBindRecvRequestCallBack();

private:
	bool Send(const Buffer& buffer, int ack = -1); // 异步发送，不关心返回结果
	Task<void> ProcessPakage(CustomPackage* newPak = nullptr);
	SpinLock _ProcessLock;

private:
	std::atomic<int> seq;
	SafeQueue<CustomPackage*, CoroCriticalSectionLock> _RecvPaks;
	SafeQueue<CustomPackage*, CoroCriticalSectionLock> _SendPaks;

	std::function<Task<void>(BaseNetWorkSession*, Buffer* recv, Buffer* response)> _callbackRecvRequest;
	SafeUnorderedMap<int, std::shared_ptr<AwaitTask>, CoroCriticalSectionLock> _AwaitMap; // seq->AwaitTask

	Buffer cacheBuffer;
	CustomPackage* cachePak; // 握手/数据包解析缓存
};
