#pragma once

#include "Buffer.h"

#include "NetExportMarco.h"

class NET_API CRC32Helper {
private:
    static uint32_t crc_table[256];
    static bool table_initialized;
    
    static void init_table();
    
public:
    static uint32_t calculate(const uint8_t* data, size_t len, 
                             uint32_t crc = 0xFFFFFFFF);
    
    // 增量计算（适用于流式数据）
    static uint32_t update(uint32_t crc, const uint8_t* data, size_t len);
};