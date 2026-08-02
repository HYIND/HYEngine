#pragma once

#include "ECS/Core/System.h"

class AudioSystem :public System
{
public:
	virtual void onAttach(World& world) override;
	virtual void onDetach() override;

private:
	void spawnAudio(const std::string& resname);
};