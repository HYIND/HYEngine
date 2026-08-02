#include "ApplicationLayerCommunication/JsonProtocolClient.h"
#include "Helper/Tools.h"

JsonProtocolClient::JsonProtocolClient()
{
	_session = std::make_shared<CustomTcpSession>();
}

JsonProtocolClient::~JsonProtocolClient()
{
	Release();
}

Task<bool> JsonProtocolClient::Connect(const std::string& IP, uint16_t Port)
{
	_sessionId = "";
	bool result = co_await _session->Connect(IP, Port);
	if (result)
		result = co_await RequestSessionId();
	if (!result)
		_session->Release();

	if (result)
	{
		co_await _session->BindSessionCloseCallBack(std::bind(&JsonProtocolClient::SessionClose, this, std::placeholders::_1));
		co_await _session->BindRecvDataCallBack(std::bind(&JsonProtocolClient::RecvData, this, std::placeholders::_1, std::placeholders::_2));
		co_await _session->BindRecvRequestCallBack(std::bind(&JsonProtocolClient::RecvRequest, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}
	co_return result;
}

bool JsonProtocolClient::Release()
{
	_callbackjsonmessage = nullptr;
	_callbackjsonrequest = nullptr;
	_callbackcloseclient = nullptr;
	return _session->Release();
}

bool JsonProtocolClient::Send(const json& js)
{
	std::string str_resp = js.dump();
	Buffer buf(str_resp);
	return _session->AsyncSend(buf);
}

Task<bool> JsonProtocolClient::Request(const json& js_request, json& response)
{
	std::string str_resp = js_request.dump();
	Buffer buf_req(str_resp);
	auto result = co_await _session->AwaitSend(buf_req);
	if (result->code != AwaitErrorCode::Success)
		co_return false;

	response = Tool::ParseJson(result->response);
	co_return true;
}

void JsonProtocolClient::BindCallBackJsonMessage(CallBackJsonMessage&& callback)
{
	_callbackjsonmessage = callback;
}

void JsonProtocolClient::BindCallBackJsonRequest(CallBackJsonRequest&& callback)
{
	_callbackjsonrequest = callback;
}

void JsonProtocolClient::BindCallBackCloseClient(CallBackCloseClient&& callback)
{
	_callbackcloseclient = callback;
}

Task<void> JsonProtocolClient::RecvData(BaseNetWorkSession* session, Buffer* recv)
{
	json js = Tool::ParseJson(*recv);
	auto callback = _callbackjsonmessage;
	if (!js.is_null() && callback)
		co_await callback(this, js);
}

Task<void> JsonProtocolClient::RecvRequest(BaseNetWorkSession* session, Buffer* recv, Buffer* resp)
{
	json js = Tool::ParseJson(*recv);
	auto callback = _callbackjsonrequest;
	if (!js.is_null() && callback)
	{
		json js_resp;
		co_await callback(this, js, js_resp);
		std::string str_resp = js_resp.dump();
		resp->Write(str_resp.c_str(), str_resp.length());
	}
}

Task<void> JsonProtocolClient::SessionClose(BaseNetWorkSession* session)
{
	auto callback = _callbackcloseclient;
	if (callback)
		co_await callback(this);
}

Task<bool> JsonProtocolClient::RequestSessionId()
{
	json req, resp;
	req["command"] = 999888;

	if (!co_await Request(req, resp))
		co_return false;

	if (!resp.contains("result") || !resp["result"].is_number_integer())
		co_return false;

	int result = resp.value("result", 0);
	if (result < 0)
		co_return false;

	if (!resp.contains("sessionid") || !resp["sessionid"].is_string())
		co_return false;

	_sessionId = resp.value("sessionid", "");
	co_return true;
}
