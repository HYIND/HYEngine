#pragma once

#include "RenderBase.h"
#include "Manager/ResourceManager.h"
#include "ECS/Core/IComponent.h"

struct Sprite :public RenderBase2D
{
	int width = 0;
	int height = 0;
	float opacity = 1.0f;
	ID2D1Bitmap* bitmap = nullptr;

	Sprite() {}
	Sprite(int width, int height, ID2D1Bitmap* bitmap) :
		bitmap(bitmap), opacity(1.0f) {
		this->width = std::max(0, width);
		this->height = std::max(0, height);
	}
};