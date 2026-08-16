#pragma once

#include "stdafx.h"
#include "Net/Helper/Buffer.h"
#include "Coroutine.h"

class MsgManager
{
public:
	static MsgManager* Instance();

public:
	Task<void> ProcessMsg(const json& js);

private:
	MsgManager();
	~MsgManager();
};

#define MSGMANAGER MsgManager::Instance()