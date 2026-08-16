#pragma once

#include "rectpack2D/rect_structs.h"
#include "rectpack2D/best_bin_finder.h"
#include "rectpack2D/finders_interface.h"
#include <map>
#include <vector>
#include <optional>

using namespace rectpack2D;
using InsertID = uint32_t;


class DynamicPacker
{
public:
	using spaces_type = empty_spaces<false, default_empty_spaces>;
	using rect_type = output_rect_t<spaces_type>;
	using RectData = rect_type;  // 存储实际矩形数据

	struct InsertResult
	{
		bool success;
		InsertID id;
		RectData rect;
	};


public:
	DynamicPacker(uint32_t initial_width = 256, uint32_t initial_height = 256, uint32_t max_side = 16384);
	~DynamicPacker();

	InsertResult AddRect(uint32_t width, uint32_t height);	// 添加矩形，返回是否成功（以及插入后的位置信息）
	bool SerachRect(InsertID id, RectData& rect) const;
	bool RemoveRect(InsertID id);
	void ShrinkToFit();										// 缩容：找到能容纳所有矩形的最小尺寸
	std::pair<uint32_t, uint32_t> GetSize() const;			// 获取当前容器尺寸
	const std::map<InsertID, RectData>& GetRects() const;	// 获取所有已放置的矩形（用于渲染）
	float GetUsage() const;// 获取使用率

	void ReleaseRect();
private:
	void ExpandCapacity();									// 扩容策略：每次扩大为原来的1.5倍 + 步进
	void RebuildPacker(uint32_t width, uint32_t height);	// 重置容器并重新放置所有矩形
	void ReSize(uint32_t new_width, uint32_t new_height);


private:
	spaces_type* _root;

	uint32_t _width;
	uint32_t _height;
	uint32_t _maxSide;								// 防止无限扩容

	InsertID _currentID;
	std::map<InsertID, RectData> _placedRects;	// 存储所有已放置的矩形
};
