#include "Helper/DynamicPacker.h"
#include <iostream>

DynamicPacker::DynamicPacker(uint32_t initial_width, uint32_t initial_height, uint32_t maxside)
	: _currentID(0)
{
	_width = std::max((uint32_t)1, initial_width);
	_height = std::max((uint32_t)1, initial_height);
	_maxSide = std::max(maxside, std::max(_width, _height));
	_root = new spaces_type({ (int)_width, (int)_height });
	_root->flipping_mode = flipping_option::DISABLED;
}

DynamicPacker::~DynamicPacker()
{
	if (_root)
		delete _root;
}

// 添加矩形，返回是否成功（以及插入后的位置信息）
DynamicPacker::InsertResult DynamicPacker::AddRect(uint32_t width, uint32_t height)
{
	if (width > _maxSide || height > _maxSide)
		return DynamicPacker::InsertResult{ false };

	// 尝试插入，失败则扩容重试
	while (true)
	{
		if (auto inserted = _root->insert({ (int)width, (int)height }))
		{
			InsertID id = _currentID++;
			_placedRects.insert({ id, *inserted });
			return DynamicPacker::InsertResult{ true, id, *inserted };
		}

		// 如果已达到最大尺寸限制，失败
		if (_width >= _maxSide && _height >= _maxSide)
			return DynamicPacker::InsertResult{ false };

		// 扩容后重试
		ExpandCapacity();
	}
}

bool DynamicPacker::SerachRect(InsertID id, DynamicPacker::RectData& rect) const
{
	auto it = _placedRects.find(id);
	if (it == _placedRects.end())
		return false;
	rect = it->second;
	return true;
}

// 移除矩形（通过ID或索引版本，如果你绑定了自定义数据）
bool DynamicPacker::RemoveRect(InsertID id)
{
	auto it = _placedRects.find(id);
	if (it == _placedRects.end())
		return false;
	_placedRects.erase(it);
	RebuildPacker(_width, _height);
	return true;
}

// 缩容：找到能容纳所有矩形的最小尺寸
void DynamicPacker::ShrinkToFit()
{
	if (_placedRects.empty())
	{
		if (_width > 256 || _height > 256)
			ReSize(256, 256);
		return;
	}

	// 使用 find_best_packing 的变体来寻找最优尺寸
	// 注意：这里需要复制数据，因为 find_best_packing 会修改位置
	std::vector<RectData> temp_rects;
	temp_rects.reserve(_placedRects.size());
	for (const auto& [id, rect] : _placedRects)
		temp_rects.push_back(rect);

	// 调用库函数寻找最优尺寸
	auto best_size = find_best_packing<spaces_type>(
		temp_rects,
		make_finder_input(
			_maxSide,			// 最大边界
			1,					// 最高精度
			[](rect_type&) { return callback_result::CONTINUE_PACKING; },
			[](rect_type&) { return callback_result::ABORT_PACKING; },
			flipping_option::DISABLED
		)
	);

	// 应用新尺寸
	ReSize(best_size.w, best_size.h);

	// 注意：find_best_packing 已经对 temp_rects 做了最优排序和定位，
	// 但我们的 rebuild_packer 会重新放置，结果应该一致
	//std::cout << "[缩容] 新尺寸: " << best_size.w << "x" << best_size.h << std::endl;
}

std::pair<uint32_t, uint32_t> DynamicPacker::GetSize() const
{
	return { _width, _height };
}

const std::map<InsertID, DynamicPacker::RectData>& DynamicPacker::GetRects() const
{
	return _placedRects;
}

float DynamicPacker::GetUsage() const
{
	if (_width == 0 || _height == 0) return 0.0f;

	uint32_t total_area = _width * _height;
	uint32_t used_area = 0;
	for (const auto& [id, rect] : _placedRects)
		used_area += rect.w * rect.h;

	return static_cast<float>(used_area) / total_area;
}

void DynamicPacker::ReleaseRect()
{
	_placedRects.clear();
	_width = 256;
	_height = 256;
	_currentID = 0;
	auto new_root = new spaces_type({ (int)_width, (int)_height });
	new_root->flipping_mode = flipping_option::DISABLED;
	auto ori_root = _root;
	_root = new_root;
	if (ori_root)
		delete ori_root;
}

// 扩容策略：每次扩大为原来的1.5倍 + 步进
void DynamicPacker::ExpandCapacity() {
	if (_width >= _maxSide && _height >= _maxSide)
		return;

	uint32_t new_width = _width;
	uint32_t new_height = _height;

	// 优先扩大短边，保持近似正方形
	if (_width <= _height) {
		new_width = std::min(_maxSide, static_cast<uint32_t>(_width * 1.5) + 64);
	}
	else {
		new_height = std::min(_maxSide, static_cast<uint32_t>(_height * 1.5) + 64);
	}

	// 防止死循环：至少增加1像素
	if (new_width == _width && new_height == _height) {
		new_width = std::min(_maxSide, _width + 64);
		new_height = std::min(_maxSide, _height + 64);
	}

	//std::cout << "[扩容] " << _width << "x" << _height
	//	<< " → " << new_width << "x" << new_height << std::endl;

	ReSize(new_width, new_height);
}

// 重置容器并重新放置所有矩形
void DynamicPacker::RebuildPacker(uint32_t width, uint32_t height)
{
	auto new_root = new spaces_type({ (int)width, (int)height });
	new_root->flipping_mode = flipping_option::DISABLED;

	// 重新插入所有已放置的矩形
	std::map<InsertID, RectData> newdata;

	for (auto& [id, rect] : _placedRects)
	{
		if (auto inserted = new_root->insert(rect.get_wh()))
			newdata.insert({ id,*inserted });
		else
		{
			std::cerr << "错误：重插失败！" << std::endl;
			// 理论上不应该发生，因为尺寸是我们计算好的
			// 可以在这里做回退处理
		}
	}
	_placedRects.swap(newdata);

	auto ori_root = _root;
	_root = new_root;
	if (ori_root)
		delete ori_root;
}

void DynamicPacker::ReSize(uint32_t new_width, uint32_t new_height) {
	_width = new_width;
	_height = new_height;
	RebuildPacker(_width, _height);
}
