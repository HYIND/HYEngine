#pragma once

#include "RenderGraphContext.h"
#include "PassNode.h"
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <climits>
#include <iostream>

namespace OpenGLRenderGraph
{

	struct ResourceUsage
	{
		int firstPass = INT_MAX;
		int lastPass = -1;
		bool isWritten = false;
		bool isRead = false;
	};

	class DependencySolver
	{
	public:
		static std::vector<PassNode*> SortPasses(std::vector<PassNode*>& passes);
		static std::unordered_map<RenderGraphResource, ResourceUsage> CalculateLifetimes(const std::vector<PassNode*>& sortedPasses);

		static void PrintLifecycles(const std::string& name, const std::unordered_map<RenderGraphResource, ResourceUsage>& usageMap);
		static void PrintPasses(const std::string& name, const std::vector<PassNode*>& sortedPasses);
	};
}