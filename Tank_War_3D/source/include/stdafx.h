#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _DEBUG
#pragma comment(lib, "public_d.lib")
#pragma comment(lib, "net_d.lib")
#pragma comment(lib, "box2d_d.lib")
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
#pragma comment(lib, "public.lib")
#pragma comment(lib, "net.lib")
#pragma comment(lib, "box2d.lib")
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
#include "Helper/Tools.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;
