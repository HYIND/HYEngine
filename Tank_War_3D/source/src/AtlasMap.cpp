#include "OpenGLRenderEngine/Base/AtlasMap.h"
#include "Manager/RenderContextManager.h"

AtlasMap::AtlasMap(
	uint32_t width,
	uint32_t height,
	uint32_t max_side)
	:
	_packer(width, height, max_side)
{
}

AtlasMap::~AtlasMap()
{
}

bool AtlasMap::AllocateSpace(uint32_t width, uint32_t height, uint32_t& id)
{
	auto result = _packer.AddRect(width, height);
	if (!result.success)
		return false;

	id = result.id;
	return true;
}

bool AtlasMap::GetSpace(uint32_t id, AtlasRect& rect) const
{
	DynamicPacker::RectData drect;
	if (!_packer.SerachRect(id, drect))
		return false;

	rect.x = drect.x;
	rect.y = drect.y;
	rect.width = drect.w;
	rect.height = drect.h;

	return true;
}

bool AtlasMap::RemoveSpace(uint32_t id)
{
	bool result = _packer.RemoveRect(id);
	return result;
}

void AtlasMap::ReleaseSpace()
{
	_packer.ReleaseRect();
}

glm::u32vec2 AtlasMap::GetSize() const
{
	auto size = _packer.GetSize();
	return glm::u32vec2(size.first, size.second);
}

void AtlasMap::ShrinkToFit()
{
	_packer.ShrinkToFit();
}

