#include "OpenGLRenderEngine/RenderGraph/DependencySolver.h"

using namespace OpenGLRenderGraph;

// ==================== 拓扑排序 ====================
std::vector<PassNode*> DependencySolver::SortPasses(std::vector<PassNode*>& passes)
{
	// 1、资源依赖：如果 passB 的输入依赖于passA的输出，则 passA → passB
	// 2、时序依赖：如果 passB 显式声明顺序依赖 passA ，也 passA → passB
	static auto CheckDependency = [](PassNode* passA, PassNode* passB)-> bool
		{
			// 检查 passA 的输出是否被 passB 读取
			for (const auto& output : passA->GetOutputs()) {
				for (const auto& input : passB->GetInputs()) {
					if (output == input){
						passB->After(passA);
						return true;
					}
				}
			}
			// 检查 passA 的输出是否被 passB 作为可选读取
			for (const auto& output : passA->GetOutputs()) {
				for (const auto& input : passB->GetInputOptions()) {
					if (output == input) {
						passB->After(passA);
						return true;
					}
				}
			}

			// 检查时序依赖
			auto& afters = passB->GetAfters();
			if (afters.find(passA) != afters.end())
				return true;

			// 检查时序依赖
			auto& before = passA->GetBefores();
			if (before.find(passB) != before.end())
			{
				passB->After(passA);
				return true;
			}

			return false;
		};

	// 构建依赖图
	std::unordered_map<PassNode*, std::vector<PassNode*>> graph;
	std::unordered_map<PassNode*, int> indegree;
	std::unordered_map<PassNode*, int> outdegree;

	// 初始化
	for (auto* pass : passes) {
		graph[pass] = {};
		indegree[pass] = 0;
		outdegree[pass] = 0;
	}

	// 构建边
	for (auto* passA : passes) {
		for (auto* passB : passes) {
			if (passA == passB) continue;

			bool hasDependency = CheckDependency(passA, passB);
			if (hasDependency) {
				graph[passA].push_back(passB);
				indegree[passB]++;
				outdegree[passA]++;
			}
		}
	}

	// 拓扑排序（Kahn算法）
	std::queue<PassNode*> q;
	for (auto& [pass, degree] : indegree) {
		if (degree == 0) {
			q.push(pass);
		}
	}

	int index = 0, batch = 0;
	std::vector<PassNode*> result;
	while (!q.empty())
	{
		std::vector<PassNode*> batchPass;
		while (!q.empty())
		{
			auto pass = q.front();
			q.pop();

			pass->SetBatch(batch);
			pass->SetIndex(index);
			batchPass.push_back(pass);
			result.push_back(pass);

			index++;
		}

		batch++;

		for (auto pass : batchPass) {
			for (auto next : graph[pass]) {
				indegree[next]--;
				if (indegree[next] == 0) {
					q.push(next);
				}
			}
		}
	}

	// 检查循环依赖
	if (result.size() != passes.size()) {
		std::cerr << "DependencySolver::SortPasses RenderGraph Ring Detected!!!\n";

		// 有循环依赖，图存在环
		// 降级方案：对已排序pass保持不动，保留有序的子图，未排序pass按照原顺序排在后面
		for (auto pass : passes)
		{
			bool isSorted = false;
			for (auto sortedpass : result)
			{
				if (sortedpass == pass)
				{
					isSorted = true;
					break;
				}
			}

			if (!isSorted)
			{
				pass->SetBatch(-1);
				pass->SetIndex(-1);
				result.push_back(pass);
			}
		}
	}

	return result;
}

// ==================== 生命周期计算 ====================
std::unordered_map<RenderGraphResource, ResourceUsage> DependencySolver::CalculateLifetimes(const std::vector<PassNode*>& sortedPasses)
{
	// 收集所有资源的使用情况
	std::unordered_map<RenderGraphResource, ResourceUsage> usageMap;

	for (auto* pass : sortedPasses)
	{
		int passBatch = pass->GetBatch();

		// 处理输入
		for (const auto& input : pass->GetInputs()) {
			auto& usage = usageMap[input];
			usage.firstBatch = std::min(usage.firstBatch, passBatch);
			usage.lastBatch = std::max(usage.lastBatch, passBatch);
			usage.isRead = true;
		}

		// 处理可选输入
		for (const auto& input : pass->GetInputOptions()) {
			auto& usage = usageMap[input];
			usage.firstBatch = std::min(usage.firstBatch, passBatch);
			usage.lastBatch = std::max(usage.lastBatch, passBatch);
			usage.isRead = true;
		}

		// 处理输出
		for (const auto& output : pass->GetOutputs()) {
			auto& usage = usageMap[output];
			usage.firstBatch = std::min(usage.firstBatch, passBatch);
			usage.lastBatch = std::max(usage.lastBatch, passBatch);
			usage.isWritten = true;
		}

		// 处理临时资源
		for (const auto& temp : pass->GetTemps()) {
			auto& usage = usageMap[temp];
			// 临时资源只在当前Batch使用
			usage.firstBatch = passBatch;
			usage.lastBatch = passBatch;
			usage.isWritten = true;
			usage.isRead = true;
		}

		//// 不处理持久化资源，持久化资源不需要回收（Persistent）
		//for (const auto& persistent : pass->GetPersistents()) {
		//	auto& usage = usageMap[persistent];
		//	usage.firstBatch = std::min(usage.firstBatch, passIndex);
		//	usage.lastBatch = std::max(usage.lastBatch, passIndex);
		//	usage.isRead = true;
		//}
	}

	return usageMap;
}

void DependencySolver::PrintLifecycles(const std::string& name, const std::unordered_map<RenderGraphResource, ResourceUsage>& usageMap) {
	std::cout << std::format("=== [{}] Resource Lifecycles ===\n", name);
	for (auto& [res, usage] : usageMap) {
		if (usage.firstBatch != INT_MAX && usage.lastBatch != -1) {
			std::cout <<
				std::format("Resource [{}]: Pass {} → Pass {} | {} {}\n",
					res.name,
					usage.firstBatch,
					usage.lastBatch,
					usage.isWritten ? "[Written]" : "",
					usage.isRead ? "[Read]" : "");
		}
	}
	std::cout << "===========================\n";
}

void DependencySolver::PrintPasses(const std::string& name, const std::vector<PassNode*>& sortedPasses) {
	std::cout << std::format("=== [{}] Passes PipeLine ===\n", name);
	if (sortedPasses.empty()) {
		std::cout << "[]\n";
		return;
	}

	int lastBatch = sortedPasses[0]->GetBatch();
	std::cout << "[";

	for (size_t i = 0; i < sortedPasses.size(); ++i) {
		auto* pass = sortedPasses[i];
		int currentBatch = pass->GetBatch();

		if (currentBatch != lastBatch || lastBatch == -1)
		{
			if (i > 0)
			{
				std::cout << "] -> [";
				lastBatch = currentBatch;
			}
		}
		else {
			if (i > 0)
				std::cout << ", ";
		}

		std::cout << pass->GetName();
	}

	std::cout << "]\n";
	std::cout << "===========================\n";
}
