#pragma once

#include "Helper/DynamicPacker.h"
#include "glm/glm.hpp"

class AtlasMap
{
public:
	struct AtlasRect
	{
		uint32_t x;
		uint32_t y;
		uint32_t width;
		uint32_t height;
	};

public:
	AtlasMap(uint32_t initial_width = 256, uint32_t initial_height = 256, uint32_t max_side = 16384);
	~AtlasMap();

	bool AllocateSpace(uint32_t width, uint32_t height, uint32_t& id);
	bool GetSpace(uint32_t id, AtlasRect& rect) const;
	bool RemoveSpace(uint32_t id);

	void ReleaseSpace();
	void ShrinkToFit();

	glm::u32vec2 GetSize() const;

private:
	DynamicPacker _packer;
};