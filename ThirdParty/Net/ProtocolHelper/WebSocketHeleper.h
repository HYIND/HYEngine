#pragma once

#include "Helper/strHelper.h"
#include "Helper/Buffer.h"
#include <vector>
#include <map>

#define WS_ERROR_INVALID_HANDSHAKE_PARAMS 10070
#define WS_ERROR_INVALID_HANDSHAKE_FRAME 10071

// 4bytes maskkey
union MaskKey
{
    uint8_t uint8keys[4];
    uint32_t uint32key;
    int32_t int32Key;

    MaskKey &operator=(const MaskKey &other)
    {
        uint32key = other.uint32key;
        return *this;
    }
    MaskKey(const MaskKey &other)
    {
        uint32key = other.uint32key;
    }
    MaskKey(const uint32_t key)
    {
        uint32key = key;
    }
};

// 表示单个WebSocket数据帧
struct WebSocketDataframe
{
    uint8_t fin;
    uint8_t rsv1;
    uint8_t rsv2;
    uint8_t rsv3;
    uint8_t opcode;
    uint8_t mask;
    uint8_t length_type;
    uint8_t masking_key[4];
    uint64_t payload_length;
    Buffer payload;
};

class NET_API WebSocketAnalysisHelp
{
public:
    // 生成握手请求
    static bool GenerateHandshakeRequest(
        /* out */ std::string &hs_req,
        /* out */ std::string &SecWSKey,
        /* optIn */ const std::string &uri = "",
        /* optIn */ const std::string &host = "",
        /* optIn */ const std::string &origin = "",
        /* optIn */ const std::vector<std::string> &protocol = {});

    // -1包不完整，1握手内容正确，其他表示错误的握手请求
    static int AnalysisHandshakeRequest(
        /* in  */ Buffer &input,
        /* out */ std::string &SecWSKey,
        /* out */ std::map<std::string, std::string> &params,
        /* out */ int &handShakeBufferLength);

    // 生成握手回复
    static bool GenerateHandshakeResponse(
        /* in  */ std::map<std::string, std::string> &params,
        /* in  */ const std::string &SecWSKey,
        /* out */ std::string &hs_rsp);

    // -1包不完整，1回复内容正确，其他表示错误的握手回复
    static int AnalysisHandshakeResponse(
        /* in  */ Buffer &input,
        /* in  */ const std::string &SecWSKey,
        /* out */ std::map<std::string, std::string> &params,
        /* out */ int &handShakeBufferLength);

    // 解析单个数据帧信息
    // -1包不完整，1解析完成，其他解析失败
    static int AnalysisDataframe(
        /* in  */ Buffer &input,
        /* out */ WebSocketDataframe &info);

    // 将输入流封装成WebSocket格式的数据流，该流中包含一个或多个数据帧（如需要分片）
    // true-生成成功，false-opcode消息类型错误
    static bool GenerateDataFrameBuffer(
        /* in  */ Buffer &input,
        /* in  */ uint8_t opcode, // 消息类型，填0x1(Text)或0x2(Binary)，否则不处理
        /* in  */ uint8_t mask,   // 如果mask填1，否则0
        /* out */ Buffer &output,
        /* optIn  */ MaskKey masking_key = 0,
        /* optIn  */ uint8_t rsv1 = 0,
        /* optIn  */ uint8_t rsv2 = 0,
        /* optIn  */ uint8_t rsv3 = 0);

    // 生成特殊Websocket格式数据流，用于Ping、Pong和Close类型的数据流
    static bool GenerateSpecialDataFrameBuffer(
        /* in  */ uint8_t opcode, // 消息类型，填0x8(Close)、0x9(Ping)或0xA(Pong)，否则返回false
        /* out */ Buffer &output,
        /* optIn  */ Buffer *input = nullptr,
        /* optIn  */ uint8_t mask = 0, // 如果mask填1，否则0
        /* optIn  */ MaskKey masking_key = 0,
        /* optIn  */ uint8_t rsv1 = 0,
        /* optIn  */ uint8_t rsv2 = 0,
        /* optIn  */ uint8_t rsv3 = 0);
};
