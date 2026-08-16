#pragma once

#include "ECSCore/IComponent.h"
#include "CommonData/ModelProxyData.h"
#include <memory>

struct ModelProxy : public IComponent
{
	std::shared_ptr<ModelProxyData> data;

	ModelProxy(std::shared_ptr<ModelProxyData> proxy = nullptr) :data(proxy) {}
};