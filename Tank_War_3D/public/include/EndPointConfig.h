#pragma once

#include <iostream>
#include "Config.h"

const std::string ServiceDiscoveryIP = Config::Instance()->Read(std::string("ServiceDiscoveryIP"), std::string(""));
const int ServiceDiscoveryPort = Config::Instance()->Read(std::string("ServiceDiscoveryPort"), 0);
