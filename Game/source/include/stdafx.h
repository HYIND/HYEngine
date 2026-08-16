#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _DEBUG
#pragma comment(lib, "GamePlay_d.lib")
#pragma comment(lib, "GameRuntime_d.lib")
#pragma comment(lib, "OpenGLRenderEngine_d.lib")
#pragma comment(lib, "Common_d.lib")
#pragma comment(lib, "net_d.lib")
#pragma comment(lib, "public_d.lib")
#pragma comment(lib, "assimp_d.lib")

#pragma comment(lib, "BulletCollision_Debug.lib")
#pragma comment(lib, "BulletDynamics_Debug.lib")
#pragma comment(lib, "BulletExampleBrowserLib_Debug.lib")
#pragma comment(lib, "BulletFileLoader_Debug.lib")
#pragma comment(lib, "BulletInverseDynamics_Debug.lib")
#pragma comment(lib, "BulletInverseDynamicsUtils_Debug.lib")
#pragma comment(lib, "BulletFileLoader_Debug.lib")
#pragma comment(lib, "BulletInverseDynamics_Debug.lib")
#pragma comment(lib, "BulletInverseDynamicsUtils_Debug.lib")
#pragma comment(lib, "BulletRobotics_Debug.lib")
#pragma comment(lib, "BulletRoboticsGUI_Debug.lib")
#pragma comment(lib, "BulletSoftBody_Debug.lib")
#pragma comment(lib, "BulletWorldImporter_Debug.lib")
#pragma comment(lib, "BulletXmlWorldImporter_Debug.lib")
#pragma comment(lib, "BussIK_Debug.lib")
#pragma comment(lib, "clsocket_Debug.lib")
#pragma comment(lib, "ConvexDecomposition_Debug.lib")
#pragma comment(lib, "GIMPACTUtils_Debug.lib")
#pragma comment(lib, "gtest_Debug.lib")
#pragma comment(lib, "gwen_Debug.lib")
#pragma comment(lib, "HACD_Debug.lib")
#pragma comment(lib, "LinearMath_Debug.lib")

#else
#pragma comment(lib, "GamePlay.lib")
#pragma comment(lib, "GameRuntime.lib")
#pragma comment(lib, "OpenGLRenderEngine.lib")
#pragma comment(lib, "Common.lib")
#pragma comment(lib, "net.lib")
#pragma comment(lib, "public.lib")
#pragma comment(lib, "assimp.lib")

#pragma comment(lib, "Bullet2FileLoader.lib")
#pragma comment(lib, "BulletCollision.lib")
#pragma comment(lib, "BulletDynamics.lib")
#pragma comment(lib, "BulletExampleBrowserLib.lib")
#pragma comment(lib, "BulletFileLoader.lib")
#pragma comment(lib, "BulletInverseDynamics.lib")
#pragma comment(lib, "BulletInverseDynamicsUtils.lib")
#pragma comment(lib, "BulletFileLoader.lib")
#pragma comment(lib, "BulletInverseDynamics.lib")
#pragma comment(lib, "BulletInverseDynamicsUtils.lib")
#pragma comment(lib, "BulletRobotics.lib")
#pragma comment(lib, "BulletRoboticsGUI.lib")
#pragma comment(lib, "BulletSoftBody.lib")
#pragma comment(lib, "BulletWorldImporter.lib")
#pragma comment(lib, "BulletXmlWorldImporter.lib")
#pragma comment(lib, "BussIK.lib")
#pragma comment(lib, "clsocket.lib")
#pragma comment(lib, "ConvexDecomposition.lib")
#pragma comment(lib, "GIMPACTUtils.lib")
#pragma comment(lib, "gtest.lib")
#pragma comment(lib, "gwen.lib")
#pragma comment(lib, "HACD.lib")
#pragma comment(lib, "LinearMath.lib")
#endif


#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glew32s.lib")

#include <iostream>
#include <string.h>
#include <chrono>
#include <random>
#include <sstream>
#include <string>

#include "Tank_War.h"
#include "framework.h"

#include <comdef.h>

#include <signal.h>
#include <regex>
#include <thread>

#include <vector>
#include <string>
#include <queue>
#include <mutex> 

#include <set>
#include <map>
#include <unordered_map>
#include <timeapi.h>
#pragma comment(lib,"Winmm.lib")

#include <type_traits>

#define LOGGERMODE_ON
#include "Helper/Log.h"

#include "Helper/Tools.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include "nlohmann/json.hpp"

using json = nlohmann::json;
