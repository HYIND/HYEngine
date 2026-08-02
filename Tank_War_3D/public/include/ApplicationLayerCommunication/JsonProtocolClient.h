#pragma once

#include "Net/Session/CustomTcpSession.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

using JsonProtocolSessionID = std::string;

// 基于CustomTcpSession封装的Json通信客户端
class JsonProtocolClient
{
	using CallBackJsonMessage = std::function<Task<void>(JsonProtocolClient*, json&)>;
	using CallBackJsonRequest = std::function<Task<void>(JsonProtocolClient*, json&, json&)>;
	using CallBackCloseClient = std::function<Task<void>(JsonProtocolClient*)>;

public:
	JsonProtocolClient();
	~JsonProtocolClient();

	bool Release();

	Task<bool> Connect(const std::string& IP, uint16_t Port);

	bool Send(const json& js);
	Task<bool> Request(const json& js_request, json& response);

	void BindCallBackJsonMessage(CallBackJsonMessage&& callback);
	void BindCallBackJsonRequest(CallBackJsonRequest&& callback);
	void BindCallBackCloseClient(CallBackCloseClient&& callback);

public:
	Task<void> RecvData(BaseNetWorkSession* session, Buffer* recv);
	Task<void> RecvRequest(BaseNetWorkSession* session, Buffer* recv, Buffer* resp);
	Task<void> SessionClose(BaseNetWorkSession* session);

private:
	Task<bool> RequestSessionId();

private:
	JsonProtocolSessionID _sessionId;
	std::shared_ptr<CustomTcpSession> _session;
	CallBackJsonMessage _callbackjsonmessage;
	CallBackJsonRequest _callbackjsonrequest;
	CallBackCloseClient _callbackcloseclient;
};