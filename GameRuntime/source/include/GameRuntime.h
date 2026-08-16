#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _DEBUG
#pragma comment(lib, "Common_d.lib")

#else
#pragma comment(lib, "Common.lib")

#endif