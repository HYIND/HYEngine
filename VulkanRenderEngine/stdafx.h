#pragma once

#ifdef _DEBUG
#pragma comment(lib, "assimp_d.lib")
#pragma comment(lib, "shaderc_combinedd.lib")
#pragma comment(lib, "DirectXTex_d.lib")
#else
#pragma comment(lib, "assimp.lib")
#pragma comment(lib, "shaderc_combined.lib")
#pragma comment(lib, "DirectXTex.lib")
#endif

