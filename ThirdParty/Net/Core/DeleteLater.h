#pragma once

#include "NetExportMarco.h"

class NET_API DeleteLaterImpl {
public:
	virtual ~DeleteLaterImpl() = default;
};
NET_API void DeleteLater(DeleteLaterImpl* ptr);