#include "OpenGLRenderEngine/General/GroupMapper.h"

int GroupMapper::addGroup(int len) {
	int prevEnd = groupEnds.empty() ? 0 : groupEnds.back();
	groupEnds.push_back(prevEnd + len);
	return groupEnds.size() - 1;
}

int GroupMapper::getGroup(int index) const {
	if (groupEnds.empty() || index < 0) return -1;

	// 找到第一个 >= index 的结束位置
	auto it = std::upper_bound(groupEnds.begin(), groupEnds.end(), index);
	if (it == groupEnds.end()) return -1;  // 超出总长度
	return std::distance(groupEnds.begin(), it);
}

int GroupMapper::totalLength() const {
	return groupEnds.empty() ? 0 : groupEnds.back();
}

int GroupMapper::groupCount() const {
	return (int)groupEnds.size();
}

int GroupMapper::groupStart(int groupId) const {
	if (groupId < 0 || groupId >= groupCount()) return -1;
	return groupId == 0 ? 0 : groupEnds[groupId - 1];
}

int GroupMapper::groupEnd(int groupId) const {
	if (groupId < 0 || groupId >= groupCount()) return -1;
	return groupEnds[groupId];
}

int GroupMapper::groupLength(int groupId) const
{
	if (groupId < 0 || groupId >= groupCount()) return -1;
	int start = groupId == 0 ? 0 : groupEnds[groupId - 1];
	return groupEnds[groupId] - start;
}
