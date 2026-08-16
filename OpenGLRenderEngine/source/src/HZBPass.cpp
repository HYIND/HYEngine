#include "OpenGLRenderEngine/RenderPass/HZBPass.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/GPUTimer.h"
#include <execution>


void RadixSortByMaterial(std::vector<uint32_t>& indices, const std::vector<OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem>& meshes)
{
	size_t n = indices.size();
	if (n <= 1) return;

	const int BITS = 16;
	const int RADIX = 1 << BITS;
	const int MASK = RADIX - 1;

	std::vector<uint32_t> temp(n);
	std::vector<uintptr_t> keys(n);
	std::vector<uint32_t> count(RADIX);

	// 提取材质指针值
	for (size_t i = 0; i < n; i++) {
		keys[i] = reinterpret_cast<uintptr_t>(meshes[indices[i]].meshinfo.material.get());
	}

	// 64位指针，4轮16位基数排序
	for (int shift = 0; shift < 64; shift += BITS) {
		std::fill(count.begin(), count.end(), 0);

		for (size_t i = 0; i < n; i++) {
			count[(keys[i] >> shift) & MASK]++;
		}

		uint32_t sum = 0;
		for (int i = 0; i < RADIX; i++) {
			uint32_t t = count[i];
			count[i] = sum;
			sum += t;
		}

		for (size_t i = 0; i < n; i++) {
			uint32_t key = (keys[i] >> shift) & MASK;
			temp[count[key]++] = indices[i];
		}

		indices.swap(temp);
	}
}

#define work_size_x 16
#define work_size_y 16

#define occ_work_size_x 256

HZBPass::HZBPass(
	const std::string& sceneVertexShaderPath,
	const std::string& scenefragmentShaderPath,
	const std::string& HZBComputerShaderPath,
	const std::string& OcclusionCullingComputerShaderPath
)
	:_Fbo(0)
{
	_depthShader.CompileFromFile(sceneVertexShaderPath, scenefragmentShaderPath);

	_HZBShader.AddDefineMacro("work_size_x", work_size_x);
	_HZBShader.AddDefineMacro("work_size_y", work_size_y);
	_HZBShader.CompileFromFile(HZBComputerShaderPath);

	_occlusionCullShader.AddDefineMacro("work_size_x", occ_work_size_x);
	_occlusionCullShader.AddDefineMacro("work_size_y", 1);
	_occlusionCullShader.CompileFromFile(OcclusionCullingComputerShaderPath);
}

HZBPass::~HZBPass() {
	if (_Fbo != 0)
		glDeleteFramebuffers(1, &_Fbo);
}

void HZBPass::Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto depthMap = ctx.GetTemp(0);
	auto HZBMap = ctx.GetOutput(0);

	DrawDepthMap(depthMap, state);
	DrawHZB(depthMap, HZBMap, state);

	if (auto aabb_ssbo = _occlusionCullShader.TryGetSSBO("MeshAABB"), result_ssbo = _occlusionCullShader.TryGetSSBO("OcclusionResults");
		state.option.flags.calculateOcclusionCulling && aabb_ssbo && result_ssbo)
	{

		if (uint64_t size = _frustumObjectMeshaabbs.size() * sizeof(AABB); aabb_ssbo->GetSize() < size)
			aabb_ssbo->SetSize(size * 1.2);
		if (uint64_t size = _frustumOcclusionCullResult.size() * sizeof(int); result_ssbo->GetSize() < size)
			result_ssbo->SetSize(size * 1.2);
		aabb_ssbo->WriteData(_frustumObjectMeshaabbs.data(), _frustumObjectMeshaabbs.size() * sizeof(AABB));

		GetOcclusionCulling(HZBMap, state);
	}
	else
	{
		auto& items = state.objects.sceneRenderData.opaqueMesh;
		auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_cullRenderIndex;
		auto& oneSideIndex = renderIndex.oneSideIndex;
		auto& twoSideIndex = renderIndex.twoSideIndex;

		for (size_t i = 0; i < _frustumObjectIndex.size(); i++)
		{
			auto meshIndex = _frustumObjectIndex[i];
			if (items[meshIndex].meshinfo.material->GetTwoSided())
				twoSideIndex.push_back(meshIndex);
			else
				oneSideIndex.push_back(meshIndex);
		}
	}
}

void HZBPass::FrameBegin(RenderState& state)
{
	Frustum& frustum = state.camera.frustum;
	auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;

	_frustumObjectMeshaabbs;
	if (!opaqueMeshes.empty())
	{
		_frustumCullResult.resize(opaqueMeshes.size(), false);
		_frustumObjectMeshaabbs.resize(opaqueMeshes.size());

		std::for_each(std::execution::par, opaqueMeshes.begin(), opaqueMeshes.end(),
			[&](OpenGLRenderObjectData::SceneRenderData::OpaqueMeshItem& item)
			{
				uint32_t meshIndex = &item - opaqueMeshes.data();

				AABB aabbworld = item.meshinfo.mesh->GetAABB();
				aabbworld.MakeTransform(item.transform);

				_frustumObjectMeshaabbs[meshIndex] = aabbworld;
				_frustumCullResult[meshIndex] = !frustum.IsAABBOnFrustum(aabbworld);
			});
	}


	size_t writePos = 0;
	for (int i = 0; i < opaqueMeshes.size(); i++)
	{
		if (_frustumCullResult[i]) continue;

		if (writePos != i)
			_frustumObjectMeshaabbs[writePos] = _frustumObjectMeshaabbs[i];

		_frustumObjectIndex.push_back(i);
		_frustumObjectTransforms.push_back(opaqueMeshes[i].transform);
		writePos++;
	}

	_frustumOcclusionCullResult.resize(writePos, 0);
	_frustumObjectMeshaabbs.resize(writePos);

	auto indirectManager = IndirectDrawManager::Instance();
	_commands.resize(_frustumObjectIndex.size());
	std::for_each(std::execution::par, _frustumObjectIndex.begin(), _frustumObjectIndex.end(),
		[&](uint32_t& meshIndex)-> void
		{
			size_t inedx = &meshIndex - _frustumObjectIndex.data();
			IndirectDrawCommand& command = _commands[inedx];

			auto& item = opaqueMeshes[meshIndex];

			IndirectDrawMeta meta;
			if (!indirectManager->GetIndirectDrawMeta(*(item.meshinfo.mesh), meta))
			{
				command.instanceCount = 0;
				return;
			}

			command.indexCount = meta.indexCount;
			command.indexfirst = meta.indexfirst;
			command.vertexFirst = meta.vertexFirst;
			command.instanceCount = 1;
			command.baseInstanceIDFirst = inedx;
		});
}

void HZBPass::FrameEnd(RenderState& state)
{
	_frustumCullResult.clear();
	_frustumObjectIndex.clear();
	_frustumOcclusionCullResult.clear();
	_frustumObjectMeshaabbs.clear();
	_commands.clear();
	_frustumObjectTransforms.clear();
}

uint32_t HZBPass::GetMaxLevel()
{
	return _maxLevel;
}

void HZBPass::DrawDepthMap(std::shared_ptr<Texture2D>& depthMap, RenderState& state)
{
	if (_Fbo == 0)
	{
		glGenFramebuffers(1, &_Fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, _Fbo);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, _Fbo);
	glViewport(0, 0, depthMap->GetWidth(), depthMap->GetHeight());

	depthMap->Bind(0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap->GetID(), 0);

	glClear(GL_DEPTH_BUFFER_BIT);

	_depthShader.Use();

	auto transform_ssbo = _depthShader.TryGetSSBO("Transforms");
	if (transform_ssbo)
	{
		transform_ssbo->WriteData(_frustumObjectTransforms.data(), _frustumObjectTransforms.size() * sizeof(glm::mat4));

		glBindVertexArray(state.indirectCommands.indirectVAO);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, state.indirectCommands.indirectCommandBuffer);
		glBufferData(GL_DRAW_INDIRECT_BUFFER, _commands.size() * sizeof(IndirectDrawCommand), _commands.data(), GL_DYNAMIC_DRAW);

		glMultiDrawElementsIndirect(
			GL_TRIANGLES,            // 图元类型
			GL_UNSIGNED_INT,         // 索引类型
			(void*)0,                // 起始偏移
			_commands.size(),        // 命令数量
			0);                      // 步长（0=连续存储）
	}
	else
	{
		auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;
		auto& shader = _depthShader;

		glm::mat4 cur_Model = glm::mat4(1.0f);
		shader.setMat4("model", cur_Model);

		for (size_t i = 0; i < _frustumObjectIndex.size(); i++)
		{
			int index = _frustumObjectIndex[i];
			auto& mesh = opaqueMeshes[index];

			if (cur_Model != mesh.transform)
			{
				shader.setMat4("model", mesh.transform);
				cur_Model = mesh.transform;
			}
			mesh.meshinfo.DrawGeometry(shader);
		}
	}
}

void HZBPass::DrawHZB(std::shared_ptr<Texture2D>& depthMap, std::shared_ptr<Texture2D>& HZBMap, RenderState& state)
{
	Texture2D::CopyTexture(depthMap, HZBMap);

	auto width = depthMap->GetWidth();
	auto height = depthMap->GetHeight();

	_HZBShader.Use();

	for (uint32_t level = 1; level < _maxLevel; level++) {
		uint32_t prevW = std::max(1u, width >> (level - 1));
		uint32_t prevH = std::max(1u, height >> (level - 1));
		uint32_t currW = std::max(1u, width >> level);
		uint32_t currH = std::max(1u, height >> level);

		glBindImageTexture(0, HZBMap->GetID(), level - 1, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		glBindImageTexture(1, HZBMap->GetID(), level, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		glDispatchCompute((currW + work_size_x - 1) / work_size_x, (currH + work_size_y - 1) / work_size_y, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	//for (uint32_t level = 0; level < _maxLevel; level++)
		//DrawTexture(HZBMap, std::format("temp/HZBMap{}.png", level), level);
}

void HZBPass::GetOcclusionCulling(std::shared_ptr<Texture2D>& HZBMap, RenderState& state)
{
	_occlusionCullShader.Use();

	_occlusionCullShader.Use();
	_occlusionCullShader.setInt("count", _frustumObjectIndex.size());
	_occlusionCullShader.setIVec2("depthMapSize", HZBMap->GetSize());
	_occlusionCullShader.setInt("maxLevel", HZBMap->GetMaxLevel());
	_occlusionCullShader.setMat4("viewProj", state.camera.projection * state.camera.view);
	_occlusionCullShader.setTexture(HZBMap, "hzbDepthMap", 0);

	glDispatchCompute((_frustumObjectIndex.size() + occ_work_size_x - 1) / occ_work_size_x, 1, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	if (auto result_ssbo = _occlusionCullShader.TryGetSSBO("OcclusionResults"))
	{
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, result_ssbo->GetID());
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, _frustumOcclusionCullResult.size() * sizeof(int), _frustumOcclusionCullResult.data());
	}

	auto& items = state.objects.sceneRenderData.opaqueMesh;
	auto& renderIndex = state.objects.sceneRenderData.opaqueMesh_cullRenderIndex;
	auto& oneSideIndex = renderIndex.oneSideIndex;
	auto& twoSideIndex = renderIndex.twoSideIndex;

	for (size_t i = 0; i < _frustumObjectIndex.size(); i++)
	{
		if (_frustumOcclusionCullResult[i] > 0) continue;

		auto meshIndex = _frustumObjectIndex[i];
		if (items[meshIndex].meshinfo.material->GetTwoSided())
			twoSideIndex.push_back(meshIndex);
		else
			oneSideIndex.push_back(meshIndex);
	}
	//std::cout << std::format("frustumObjectSize = {}, renderObjectSize = {}\n", _frustumObjectIndex.size(), oneSideIndex.size() + twoSideIndex.size());
}
