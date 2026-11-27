// V-HACD 4 的实现（单头文件库：整个工程只能有一个 cpp 定义 ENABLE_VHACD_IMPLEMENTATION）
// 单独放一个文件，避免和 Windows.h 的 min / max 等宏混在一起
#define ENABLE_VHACD_IMPLEMENTATION 1
#include "vhacd/VHACD.h"
