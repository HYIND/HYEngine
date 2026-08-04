#include "OpenGLRenderEngine/RenderPass/HZBPass.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/GPUTimer.h"
#include <execution>

static void WaitFence(GLsync& fence)
{
	if (fence && glIsSync(fence))
	{
		glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
		glDeleteSync(fence);
		fence = NULL;
	}
}

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

HZBPass::HZBPass(
	const std::string& sceneVertexShaderPath,
	const std::string& scenefragmentShaderPath,
	const std::string& HZBComputerShaderPath,
	const std::string& OcclusionCullingComputerShaderPath
)
	:_Fbo(0), _renderOcclusionCull(false), _setupfence(nullptr)
{
	_depthShader.CompileFromFile(sceneVertexShaderPath, scenefragmentShaderPath);

	_HZBShader.AddDefineMacro("work_size_x", work_size_x);
	_HZBShader.AddDefineMacro("work_size_y", work_size_y);
	_HZBShader.CompileFromFile(HZBComputerShaderPath);

	_occlusionCullShader.AddDefineMacro("work_size_x", work_size_x);
	_occlusionCullShader.AddDefineMacro("work_size_y", 1);
	_occlusionCullShader.CompileFromFile(OcclusionCullingComputerShaderPath);
}

void HZBPass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	auto depthMap = ctx.GetTemp(0);
	auto HZBMap = ctx.GetOutput(0);

	//GPUTimer timer;
	DrawDepthMap(depthMap, state);
	//timer.EndWithPrintAndBeginNext("DrawDepthMap cost {} ms\n");
	DrawHZB(depthMap, HZBMap, state);
	//timer.EndWithPrintAndBeginNext("DrawHZB cost {} ms\n");
	GetOcclusionCulling(HZBMap, state);
	//timer.EndWithPrintAndBeginNext("GetOcclusionCulling cost {} ms\n");
}

void HZBPass::FrameBegin(RenderState& state)
{

	Frustum& frustum = state.camera.frustum;
	auto& opaqueMeshes = state.objects.sceneRenderData.opaqueMesh;

	std::vector<uint32_t> sortByMaterial;
	sortByMaterial.resize(opaqueMeshes.size());
	std::iota(sortByMaterial.begin(), sortByMaterial.end(), 0);

	if (sortByMaterial.size() > 1)
	{
		//std::sort(std::execution::par_unseq, sortByMaterial.begin(), sortByMaterial.end(),
		//	[&](uint32_t a, uint32_t b)-> bool
		//	{
		//		return opaqueMeshes[a].meshinfo.material < opaqueMeshes[b].meshinfo.material;
		//	}
		//);

		RadixSortByMaterial(sortByMaterial, opaqueMeshes);
	}

	std::vector<AABB> meshaabbs;
	if (!opaqueMeshes.empty())
	{
		_frustumCullResult.resize(opaqueMeshes.size(), false);
		meshaabbs.resize(opaqueMeshes.size());

		std::for_each(std::execution::par, sortByMaterial.begin(), sortByMaterial.end(),
			[&](uint32_t& meshIndex)
			{
				uint32_t index = &meshIndex - sortByMaterial.data();

				AABB aabbworld = opaqueMeshes[meshIndex].meshinfo.mesh->GetAABB();
				aabbworld.MakeTransform(opaqueMeshes[meshIndex].transform);

				meshaabbs[index] = aabbworld;
				_frustumCullResult[index] = !frustum.IsAABBOnFrustum(aabbworld);
			});
	}

	auto guard = THREADCONTEXT->GetBindGuard();
	auto aabb_ssbo = _occlusionCullShader.TryGetSSBO("MeshAABB");
	auto result_ssbo = _occlusionCullShader.TryGetSSBO("OcclusionResults");

	if (aabb_ssbo && result_ssbo)
		//if (false)
	{

		size_t writePos = 0;
		for (size_t readPos = 0; readPos < meshaabbs.size(); readPos++) {
			bool isValid = !_frustumCullResult[readPos];
			if (isValid) {
				if (writePos != readPos)
				{
					//std::swap(meshaabbs[writePos], meshaabbs[readPos]);
					meshaabbs[writePos] = meshaabbs[readPos];
				}
				_frustumObjectIndex.push_back(sortByMaterial[readPos]);
				writePos++;
			}
		}

		size_t validCount = writePos;
		_frustumOcclusionCullResult.resize(validCount, 0);
		aabb_ssbo->WriteData(meshaabbs.data(), validCount * sizeof(AABB));
		result_ssbo->WriteData(_frustumOcclusionCullResult.data(), validCount * sizeof(int));

		_renderOcclusionCull = true;

		_setupfence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glFlush();
	}
	else
	{
		for (int i = 0; i < opaqueMeshes.size(); i++)
		{
			if (_frustumCullResult[i]) continue;
			_frustumObjectIndex.push_back(sortByMaterial[i]);
		}
		_frustumOcclusionCullResult.resize(_frustumObjectIndex.size(), 0);
		_renderOcclusionCull = false;
	}
}

void HZBPass::FrameEnd(RenderState& state)
{
	_frustumCullResult.clear();
	_frustumObjectIndex.clear();
	_frustumOcclusionCullResult.clear();
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

		//float white = 1.0f;
		//glClearTexImage(HZBMap->GetID(), level, GL_RED, GL_FLOAT, &white);

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
	if (_renderOcclusionCull)
	{
		_occlusionCullShader.Use();

		_occlusionCullShader.Use();
		_occlusionCullShader.setInt("count", _frustumObjectIndex.size());
		_occlusionCullShader.setIVec2("depthMapSize", HZBMap->GetSize());
		_occlusionCullShader.setInt("maxLevel", HZBMap->GetMaxLevel());
		_occlusionCullShader.setMat4("viewProj", state.camera.projection * state.camera.view);
		_occlusionCullShader.setTexture(HZBMap, "hzbDepthMap", 0);

		WaitFence(_setupfence);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		glDispatchCompute((_frustumObjectIndex.size() + work_size_x - 1) / work_size_x, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		if (auto result_ssbo = _occlusionCullShader.TryGetSSBO("OcclusionResults"))
		{
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, result_ssbo->GetID());
			void* mappedPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, _frustumOcclusionCullResult.size() * sizeof(int), GL_MAP_READ_BIT);
			if (mappedPtr)
			{
				memcpy(_frustumOcclusionCullResult.data(), mappedPtr, _frustumOcclusionCullResult.size() * sizeof(int));
				glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
			}
		}
	}

	auto& sort = state.objects.sceneRenderData.opaqueMesh_SortIndex;
	sort.reserve(_frustumObjectIndex.size());
	for (size_t i = 0; i < _frustumObjectIndex.size(); i++)
	{
		if (_frustumOcclusionCullResult[i] > 0) continue;
		sort.push_back(_frustumObjectIndex[i]);
	}
	//std::cout << std::format("frustumObjectSize = {}, renderObjectSize = {}\n", _frustumObjectIndex.size(), sort.size());
}
