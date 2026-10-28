#include "UI/UiTypes.h"

namespace UI
{
	namespace
	{
		void SeedNode(const UiNode& node, UiInstanceState& instance)
		{
			UiNodeState& state = instance.Get(node.id);

			// 只播"有初始值概念"的节点类型。其余节点的 value 没有意义，保持 0。
			if (node.type == "toggle")
			{
				state.value = node.value;
				state.toggled = node.value > 0.0f;
			}
			else if (node.type == "slider" || node.type == "progress" || node.type == "combo")
			{
				state.value = node.value;
			}
			else if (node.type == "scrolllist")
			{
				state.value = -1.0f;
			}

			for (const auto& child : node.children)
				SeedNode(child, instance);
		}
	}

	void SeedInstanceState(const UiDocument& document, UiInstanceState& instance)
	{
		SeedNode(document.root, instance);
	}
}
