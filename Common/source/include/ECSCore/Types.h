#pragma once

#include <stdint.h>
#include <bitset>

using EntityID = uint32_t;

using ComponentTypeID = uint32_t;

constexpr EntityID MAX_ENTITIES = 3000;
constexpr uint32_t MAX_COMPONENTS = 500;

using ComponentMask = std::bitset<MAX_COMPONENTS>;

using SystemTypeID = uint32_t;
