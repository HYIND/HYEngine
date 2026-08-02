#pragma once

#include <vector>

// 组映射，对于数组数据，可以使用分组器进行逻辑分组
class GroupMapper
{
private:
	std::vector<int> groupEnds;				// 每个分组的结束下标（不包含）

public:
	GroupMapper() = default;

	int addGroup(int len);					// 添加一个分组，长度为 len
	int getGroup(int index) const;			// 查询下标属于哪个分组，返回分组索引（从0开始）
	int totalLength() const;				// 获取总长度
	int groupCount() const;					// 获取分组数量
	int groupStart(int groupId) const;		// 获取某个分组的起始下标
	int groupEnd(int groupId) const;		// 获取某个分组的结束下标（不包含）
	int groupLength(int groupId) const;		// 获取某个分组的长度
};