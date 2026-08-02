#include "OpenGLRenderEngine/RenderPass/RayTracePass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/BVHBuilder.h"
#include "OpenGLRenderEngine/OpenGLRenderConfig.h"
#include "Manager/ResourceManager.h"

#define work_size_x 16
#define work_size_y 16

struct alignas(16) comp_Vertex {
	alignas(16) glm::vec3 position;
};

struct alignas(16) comp_Triangle {
	comp_Vertex v0;
	comp_Vertex v1;
	comp_Vertex v2;
};

struct alignas(16) comp_Vertex_Extension {
	alignas(16) glm::vec3 normal;
	alignas(16) glm::vec2 texCoords;

	alignas(16) glm::vec3 Tangent;
	alignas(16) glm::vec3 Bitangent;
	alignas(16) int m_BoneIDs[OpenGLRenderConfig::Mesh_Max_Bone_Influence] = { -1 };
	alignas(16) float m_Weights[OpenGLRenderConfig::Mesh_Max_Bone_Influence] = { 1.0f };
};

struct alignas(16) comp_Triangle_Extension {
	comp_Vertex_Extension v0;
	comp_Vertex_Extension v1;
	comp_Vertex_Extension v2;
};

struct comp_MaterialData : public MeshInfo::MaterialData
{
};

struct alignas(16) comp_MeshData
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 invModel;

	glm::vec4 normalMatRow1;
	glm::vec4 normalMatRow2;
	glm::vec4 normalMatRow3;

	int triangleFirst;  //triangles中的首个Triangle位置
	int triangleCount;  //Triangle数量

	int bvhNodeFirst;   //meshBVHNodeBuffer.nodes中的首个bvhnode位置
	int bvhNodeCount;   //bvhnode数量

	//int bvhIndicesFirst;   //meshBVHIndicesBuffer.indices中的首个indices位置
	//int bvhIndicesCount;   //indices数量
};

struct alignas(16) comp_MeshMatData
{
	comp_MeshData mesh;
	comp_MaterialData material;
};

RayTracePass::RayTracePass(
	const std::string& rayTraceComputerShaderPath,
	const std::string& TAAComputerShaderPath,
	const std::string& denoisedComputerShaderPath,
	const std::string& scaleComputerShaderPath
)
	:
	useDenoised(true),
	useTAA(false),
	first_TAAIteration(true),
	curTAAOutPutIndex(0),
	_traiangleBufferManager(1000 * 1024),
	_traiangleExtBufferManager(3000 * 1024),
	_meshBVHNodeBufferManager(1000 * 1024),
	_forceFlushBuffer(false)
	//_meshBVHIndicesBufferManager(100 * 1024),
{
	_rayTraceShader_useGbuffer.AddDefineMacro("useGbuffer", "");
	_rayTraceShader_useGbuffer.AddDefineMacro("Max_Recursive_Depth", std::to_string(OpenGLRenderConfig::RayTrace_Max_Recursive_Depth));
	_rayTraceShader_useGbuffer.AddDefineMacro("Max_Bounce_limit", std::to_string(OpenGLRenderConfig::RayTrace_Max_Bounce_limit));

	_rayTraceShader_pureRayTrace.AddDefineMacro("Max_Recursive_Depth", std::to_string(OpenGLRenderConfig::RayTrace_Max_Recursive_Depth));
	_rayTraceShader_pureRayTrace.AddDefineMacro("Max_Bounce_limit", std::to_string(OpenGLRenderConfig::RayTrace_Max_Bounce_limit));

	_rayTraceShader_useGbuffer.CompileFromFile(rayTraceComputerShaderPath);
	_rayTraceShader_pureRayTrace.CompileFromFile(rayTraceComputerShaderPath);

	_TAAShader.CompileFromFile(TAAComputerShaderPath);
	_denoisedShader.CompileFromFile(denoisedComputerShaderPath);
	_scaleShader.CompileFromFile(scaleComputerShaderPath);
}

bool RayTracePass::ShouldExecute(RenderState& state) const
{
	if (state.objects.sceneItems.empty() || !state.flags.rayTraceOn)
		return false;
	return state.rayTraceParams.maxBounceLimit > 0 || !state.rayTraceParams.useGbuffer;
}

void RayTracePass::Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	if (!ShouldExecute(state))
		return;

	if (_useGBuffer != state.rayTraceParams.useGbuffer)
	{
		_useGBuffer = state.rayTraceParams.useGbuffer;
		_forceFlushBuffer = true;
	}

	bool needDraw = state.rayTraceParams.maxBounceLimit > 0 || !_useGBuffer;
	if (!needDraw)
		return;

	FrameRenderData data;
	data.scrSize = glm::ivec2(state.framebuffer.width, state.framebuffer.height);
	data.drawSize = data.scrSize;
	data.gPosition = ctx.GetInput(0);
	data.gNormal = ctx.GetInput(1);
	data.gAlbedoOpacity = ctx.GetInput(2);
	data.gMetallicRoughness = ctx.GetInput(3);
	data.atlasShadowMap = ctx.GetInput(4);

	data.sceneColorBuffer = ctx.GetExternal(0);
	data.sceneDepthBuffer = ctx.GetExternal(1);

	data.originTexture = ctx.GetTemp(0);
	data.denoisedTexture = ctx.GetTemp(1);

	data.TAA_Texture[0] = ctx.GetPersitent(0);
	data.TAA_Texture[1] = ctx.GetPersitent(1);

	data.outPutTexture = ctx.GetOutput(0);

	SetEnableTAA(state.rayTraceParams.useTAA);
	SetEnableDenoised(state.rayTraceParams.useDenoised);

	if (needDraw && !DrawRayTrace(data, state)) return;

	if (needDraw && useTAA && !DrawTAA(data, state)) return;

	if (needDraw && useDenoised && !DrawDenoised(data, state)) return;

	if (!DrawScale(data, state)) return;

	//RENDERCONTEXMANAGER->WithTempReleaseMainOpenGLBind([&]()->void {
	//	THREADCONTEXT->UnBind();
	//	auto task1 = CoroTask::Run([&]()-> void {DrawTexture(data.outPutTexture, "temp/test_RayTracePass.png"); });
	//	task1.sync_wait();
	//	THREADCONTEXT->Bind();
	//	});
}

struct Ray {
	glm::vec3 origin;
	glm::vec3 direction;
	float tMin;
	float tMax;
};
bool intersectAABB(Ray ray, glm::vec3 min, glm::vec3 max) {
	// 1. 计算每个轴的 t 范围
	glm::vec3 invDir = glm::vec3(1.0) / ray.direction;

	glm::vec3 t1 = (min - ray.origin) * invDir;
	glm::vec3 t2 = (max - ray.origin) * invDir;

	// 2. 每个轴取 min/max
	glm::vec3 tMinVec = glm::min(t1, t2);
	glm::vec3 tMaxVec = glm::max(t1, t2);

	// 3. 取所有轴的交集
	float tNear = std::max(std::max(tMinVec.x, tMinVec.y), tMinVec.z);
	float tFar = std::min(std::min(tMaxVec.x, tMaxVec.y), tMaxVec.z);

	// 4. 判断是否相交
	return tNear <= tFar && tFar > ray.tMin && tNear < ray.tMax;
}

bool RayTracePass::SetupMeshBuffer(Shader& shader, RenderState& state)
{
	static auto copyVertex = [](comp_Vertex& comp_v, comp_Vertex_Extension& comp_v_ext, Vertex& v)-> void {
		memcpy(&comp_v, &v, sizeof(comp_Vertex));
		memcpy(&comp_v_ext, (char*)(&v) + sizeof(comp_Vertex), sizeof(comp_Vertex_Extension));
		};

	static auto writePaddingCount = [](int count, std::shared_ptr<SSBO>& ssbo)-> void {
		glm::ivec4 padding = glm::ivec4(count, 0.f, 0.f, 0.f);
		ssbo->WriteData(&padding, sizeof(padding), 0);
		};

	static auto getTriangleData = [](Mesh& mesh, std::vector<comp_Triangle>& triangles, std::vector<comp_Triangle_Extension>& triangleExts)
		{
			auto& vertices = mesh.GetVertices();
			auto& indices = mesh.GetIndices();
			int tricount = indices.size() / 3;
			triangles.resize(tricount);
			triangleExts.resize(tricount);
			for (int i = 0; i < tricount; i++)
			{
				copyVertex(triangles[i].v0, triangleExts[i].v0, vertices[indices[3 * i + 0]]);
				copyVertex(triangles[i].v1, triangleExts[i].v1, vertices[indices[3 * i + 1]]);
				copyVertex(triangles[i].v2, triangleExts[i].v2, vertices[indices[3 * i + 2]]);
			}
		};

	static auto resortTriangleByBVHData = [](std::shared_ptr<BVHData>& bvhData, std::vector<comp_Triangle>& triangles, std::vector<comp_Triangle_Extension>& triangleExts)
		{
			int tricount = bvhData->indices.size();

			std::vector<int> newIndices;
			newIndices.reserve(tricount);
			std::vector<comp_Triangle> newTriangles;
			newTriangles.reserve(tricount);
			std::vector<comp_Triangle_Extension> newtriangleExts;
			newtriangleExts.reserve(tricount);

			for (int i = 0; i < tricount; i++)
			{
				int index = bvhData->indices[i];
				newIndices.push_back(i);
				newTriangles.push_back(std::move(triangles[index]));
				newtriangleExts.push_back(std::move(triangleExts[index]));
			}

			bvhData->indices = newIndices;
			triangles = std::move(newTriangles);
			triangleExts = std::move(newtriangleExts);
		};

	static auto resortMeshMatByBVHData = [](std::shared_ptr<BVHData>& bvhData, std::vector<comp_MeshMatData>& meshmatDatas)
		{
			int meshmatcount = bvhData->indices.size();

			std::vector<int> newIndices;
			newIndices.reserve(meshmatcount);
			std::vector<comp_MeshMatData> newmeshmatDatas;
			newmeshmatDatas.reserve(meshmatcount);

			for (int i = 0; i < meshmatcount; i++)
			{
				int index = bvhData->indices[i];
				newIndices.push_back(i);
				newmeshmatDatas.push_back(meshmatDatas[index]);
			}

			bvhData->indices = newIndices;
			meshmatDatas = std::move(newmeshmatDatas);
		};

	constexpr float maxDistanceSqrt = 100 * 100;			// 反射最大计算距离
	constexpr float maxCacheClearDistanceSqrt = 150 * 150;	// mesh缓存清除距离

	auto traiangleBuffer = shader.TryGetSSBO("TriangleBuffer");
	auto traiangleExtBuffer = shader.TryGetSSBO("TriangleExtBuffer");

	auto meshmatDataBuffer = shader.TryGetSSBO("MeshMatDataBuffer");

	auto worldBVHNodeBuffer = shader.TryGetSSBO("WorldBVHNodeBuffer");
	//auto worldBVHIndicesBuffer = shader.TryGetSSBO("WorldBVHIndicesBuffer");

	auto meshBVHNodeBuffer = shader.TryGetSSBO("MeshBVHNodeBuffer");
	//auto meshBVHIndicesBuffer = shader.TryGetSSBO("MeshBVHIndicesBuffer");

	if (!traiangleBuffer || !traiangleExtBuffer ||
		!meshmatDataBuffer ||
		!meshBVHNodeBuffer /*|| !meshBVHIndicesBuffer*/ ||
		!worldBVHNodeBuffer /*|| !worldBVHIndicesBuffer*/)
		return false;

	std::vector<AABB> meshAABBData;

	int triangleCount = 0;

	int meshBVHNodeCount = 0;
	int meshBVHIndicesCount = 0;

	std::vector<comp_MeshMatData> meshmatDatas;
	meshmatDatas.reserve(state.objects.sceneItems.size());

	bool isBufferChange = false;
	for (auto& item : state.objects.sceneItems)
	{
		auto& meshInfo = item.meshinfo;
		if (item.isFpsSelfModel) continue;
		if (!meshInfo.mesh) continue;

		AABB aabb = meshInfo.mesh->GetAABB();
		aabb.MakeTransform(item.transform);
		float dis_sqrt = aabb.DistancePointToAABBSqrt(state.camera.position);
		if (dis_sqrt > maxCacheClearDistanceSqrt)
		{
			SegmentBufferManager<std::string>::SegmentData data;
			_traiangleBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
			_traiangleExtBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
			_meshBVHNodeBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
			//_meshBVHIndicesBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
			continue;
		}
		else if (dis_sqrt > maxDistanceSqrt)
			continue;

		comp_MeshMatData meshmatdata;

		meshmatdata.mesh.model = item.transform;
		meshmatdata.mesh.invModel = glm::inverse(item.transform);
		glm::mat4 mat = transpose(meshmatdata.mesh.invModel);
		meshmatdata.mesh.normalMatRow1 = mat[0];
		meshmatdata.mesh.normalMatRow2 = mat[1];
		meshmatdata.mesh.normalMatRow3 = mat[2];
		meshmatdata.mesh.triangleCount = (int)(meshInfo.mesh->GetIndices().size() / 3);

		meshInfo.GetMaterialCompData(meshmatdata.material);

		auto meshbvhdata = meshInfo.mesh->GetBVHData();

		meshmatdata.mesh.bvhNodeCount = meshbvhdata->nodes.size();
		//meshmatdata.mesh.bvhIndicesCount = meshbvhdata->indices.size();

		triangleCount += meshmatdata.mesh.triangleCount;
		meshBVHNodeCount += meshbvhdata->nodes.size();
		meshBVHIndicesCount += meshbvhdata->indices.size();

		auto mesh_viNewVersion = meshInfo.mesh->GetVerticesIndicesVsrsion();
		auto meshuuid = meshInfo.mesh->GetUUID();

		SegmentBufferManager<std::string>::SegmentData triSegData;
		if (!_traiangleBufferManager.FindSegment(meshuuid, triSegData) || (uint32_t)triSegData.userData != mesh_viNewVersion)
		{
			std::vector<comp_Triangle> triangles;
			std::vector<comp_Triangle_Extension> triangleExts;
			getTriangleData(*meshInfo.mesh, triangles, triangleExts);
			resortTriangleByBVHData(meshbvhdata, triangles, triangleExts);
			auto newSegmentTri = _traiangleBufferManager.SetSegment(meshuuid, (void*)mesh_viNewVersion, (const char*)triangles.data(), triangles.size() * sizeof(comp_Triangle));
			auto newSegmentTriExt = _traiangleExtBufferManager.SetSegment(meshuuid, (void*)mesh_viNewVersion, (const char*)triangleExts.data(), triangleExts.size() * sizeof(comp_Triangle_Extension));
			meshmatdata.mesh.triangleFirst = newSegmentTri.first / sizeof(comp_Triangle);

			isBufferChange = true;
			assert(newSegmentTri.first % sizeof(comp_Triangle) == 0);
			assert(newSegmentTriExt.first % sizeof(comp_Triangle_Extension) == 0);
		}
		else
		{
			assert(newSegmentTri.first % sizeof(comp_Triangle) == 0);
			meshmatdata.mesh.triangleFirst = triSegData.first / sizeof(comp_Triangle);
		}


		SegmentBufferManager<std::string>::SegmentData meghBVHNodeSegData;
		if (!_meshBVHNodeBufferManager.FindSegment(meshuuid, meghBVHNodeSegData) || (uint32_t)meghBVHNodeSegData.userData != mesh_viNewVersion)
		{
			auto meshbvhdata = meshInfo.mesh->GetBVHData();
			auto newSegmentBVHNode = _meshBVHNodeBufferManager.SetSegment(meshuuid, (void*)mesh_viNewVersion, (const char*)meshbvhdata->nodes.data(), meshbvhdata->nodes.size() * sizeof(BVHNode));
			meshmatdata.mesh.bvhNodeFirst = newSegmentBVHNode.first / sizeof(BVHNode);

			isBufferChange = true;
			assert(newSegmentBVHNode.first % sizeof(BVHNode) == 0);
		}
		else
		{
			assert(meghBVHNodeSegData.first % sizeof(BVHNode) == 0);
			meshmatdata.mesh.bvhNodeFirst = meghBVHNodeSegData.first / sizeof(BVHNode);
		}

		//SegmentBufferManager<std::string>::SegmentData meghBVHIndicesSegData;
		//if (!_meshBVHIndicesBufferManager.FindSegment(meshuuid, meghBVHIndicesSegData) || (uint32_t)meghBVHIndicesSegData.userData != mesh_viNewVersion)
		//{
		//	auto meshbvhdata = meshInfo.mesh->GetBVHData();
		//	auto newSegmentBVHIndices = _meshBVHIndicesBufferManager.SetSegment(meshuuid, (void*)mesh_viNewVersion, (const char*)meshbvhdata->indices.data(), meshbvhdata->indices.size() * sizeof(int));
		//	meshmatdata.mesh.bvhIndicesFirst = newSegmentBVHIndices.first / sizeof(int);

		//	isBufferChange = true;
		//	assert(newSegmentBVHIndices.first % sizeof(int) == 0);
		//}
		//else
		//{
		//	assert(meghBVHIndicesSegData.first % sizeof(BVHNode) == 0);
		//	meshmatdata.mesh.bvhIndicesFirst = meghBVHIndicesSegData.first / sizeof(int);
		//}

		meshmatDatas.push_back(meshmatdata);

		meshAABBData.push_back(aabb);
	}

	if (isBufferChange || _forceFlushBuffer)
	{
		traiangleBuffer->WriteData(_traiangleBufferManager.GetData(), _traiangleBufferManager.GetUseSpace(), 0);

		traiangleExtBuffer->WriteData(_traiangleExtBufferManager.GetData(), _traiangleExtBufferManager.GetUseSpace(), 0);

		meshBVHNodeBuffer->WriteData(_meshBVHNodeBufferManager.GetData(), _meshBVHNodeBufferManager.GetUseSpace(), 0);

		_forceFlushBuffer = false;
	}

	auto worldBVHData = BVHBuilder::Build(meshAABBData, OpenGLRenderConfig::RayTrace_World_BVH_Leaf_MeshCount, OpenGLRenderConfig::RayTrace_Max_Recursive_Depth);
	resortMeshMatByBVHData(worldBVHData, meshmatDatas);

	meshmatDataBuffer->WriteData(meshmatDatas.data(), meshmatDatas.size() * sizeof(comp_MeshMatData), 0);

	writePaddingCount(worldBVHData->nodes.size(), worldBVHNodeBuffer);
	worldBVHNodeBuffer->WriteData(worldBVHData->nodes.data(), worldBVHData->nodes.size() * sizeof(BVHNode), 16);

	//int indiesCount = worldBVHData->indices.size();
	//worldBVHIndicesBuffer->WriteData(&indiesCount, sizeof(int), 0);
	//worldBVHIndicesBuffer->WriteData(worldBVHData->indices.data(), worldBVHData->indices.size() * sizeof(int), 4);

	return true;
}

bool RayTracePass::DrawRayTrace(FrameRenderData& data, RenderState& state)
{
	auto& target = data.originTexture;

	if (!target || target->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(target->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	auto& rayTraceShader = state.rayTraceParams.useGbuffer ? _rayTraceShader_useGbuffer : _rayTraceShader_pureRayTrace;

	rayTraceShader.Use();

	if (!SetupMeshBuffer(rayTraceShader, state))
		return false;

	RenderHelp::SetupLightingData(rayTraceShader,
		state.lights.dirLightInfos,
		state.lights.pointLightInfos,
		state.lights.spotLightInfos,
		state.lights.shadowAtlas
	);

	rayTraceShader.setIVec2("screenSize", data.drawSize);

	//光追参数
	rayTraceShader.setFloat("tMin", state.rayTraceParams.tMin);
	rayTraceShader.setFloat("tMax", state.rayTraceParams.tMax);
	rayTraceShader.setInt("maxBounce", std::min(state.rayTraceParams.maxBounceLimit, OpenGLRenderConfig::RayTrace_Max_Bounce_limit));
	//rayTraceShader.setInt("maxBounce", std::min(0, OpenGLRenderConfig::RayTrace_Max_Bounce_limit));

	rayTraceShader.setBool("jitter", useTAA);
	rayTraceShader.setInt("frameIndex", state.renderRecord.frameIndex % 100000);

	if (state.rayTraceParams.useGbuffer)
	{
		rayTraceShader.setTexture(data.gPosition, "gPosition", 5);
		rayTraceShader.setTexture(data.gNormal, "gNormal", 6);
		rayTraceShader.setTexture(data.gAlbedoOpacity, "gAlbedoOpacity", 7);
		rayTraceShader.setTexture(data.gMetallicRoughness, "gMetallicRoughness", 8);
		rayTraceShader.setTexture(data.sceneColorBuffer, "colorMap", 9);
		rayTraceShader.setTexture(data.sceneDepthBuffer, "depthMap", 10);
	}

	rayTraceShader.setTexture(data.atlasShadowMap, "atlasShadowMap", 11);

	glBindImageTexture(0, target->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);// 分发计算任务
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool RayTracePass::DrawTAA(FrameRenderData& data, RenderState& state)
{
	auto& TAA_Texture = data.TAA_Texture;
	auto& inputTexture = data.originTexture;

	if (!inputTexture || inputTexture->IsEmpty())
		return false;

	_TAAShader.Use();

	_TAAShader.setIVec2("screenSize", data.drawSize);
	_TAAShader.setBool("isFirstIteration", first_TAAIteration);
	_TAAShader.setFloat("blendFactor", 0.3);

	int historyFrameIndex = curTAAOutPutIndex;
	int nextFrameIndex = curTAAOutPutIndex == 0 ? 1 : 0;

	if (first_TAAIteration)
	{
		glBindImageTexture(0, inputTexture->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, TAA_Texture[historyFrameIndex]->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, TAA_Texture[nextFrameIndex]->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		glBindImageTexture(0, inputTexture->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, TAA_Texture[nextFrameIndex]->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, TAA_Texture[historyFrameIndex]->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		first_TAAIteration = false;
	}
	else
	{
		glBindImageTexture(0, inputTexture->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, TAA_Texture[historyFrameIndex]->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(2, TAA_Texture[nextFrameIndex]->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	curTAAOutPutIndex = nextFrameIndex;
	data.TAA_LastTexture = TAA_Texture[nextFrameIndex];

	return true;
}

bool RayTracePass::DrawDenoised(FrameRenderData& data, RenderState& state)
{
	std::shared_ptr<Texture2D> srcTex;
	std::shared_ptr<Texture2D>& targetTex = data.denoisedTexture;
	if (useTAA && data.TAA_LastTexture && !data.TAA_LastTexture->IsEmpty())
		srcTex = data.TAA_LastTexture;
	else
		srcTex = data.originTexture;

	if (!srcTex || srcTex->IsEmpty())
		return false;

	if (!targetTex || targetTex->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	_denoisedShader.Use();
	_denoisedShader.setIVec2("screenSize", data.drawSize);

	glBindImageTexture(0, srcTex->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
	glBindImageTexture(1, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((data.drawSize.x + work_size_x - 1) / work_size_x, (data.drawSize.y + work_size_y - 1) / work_size_y, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	return true;
}

bool RayTracePass::DrawScale(FrameRenderData& data, RenderState& state)
{
	std::shared_ptr<Texture2D> srcTex;
	std::shared_ptr<Texture2D>& targetTex = data.outPutTexture;

	int srcWidth;
	int srcHeight;

	bool useSceneColorBuffer = state.rayTraceParams.maxBounceLimit <= 0 && state.rayTraceParams.useGbuffer;
	if (!useSceneColorBuffer)
	{
		if (useDenoised && data.denoisedTexture && !data.denoisedTexture->IsEmpty())
			srcTex = data.denoisedTexture;
		else if (useTAA && data.TAA_LastTexture && !data.TAA_LastTexture->IsEmpty())
			srcTex = data.TAA_LastTexture;
		else
			srcTex = data.originTexture;

		if (!srcTex || srcTex->IsEmpty())
			return false;

		srcWidth = srcTex->GetWidth();
		srcHeight = srcTex->GetHeight();
	}
	else
	{
		srcTex = data.sceneColorBuffer;
		srcWidth = data.scrSize.x;
		srcHeight = data.scrSize.y;
	}


	if (!targetTex || targetTex->IsEmpty())
		return false;

	GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(targetTex->GetID(), 0, GL_RGBA, GL_FLOAT, clearColor);

	int destWidth = targetTex->GetWidth();
	int destHeight = targetTex->GetHeight();

	if (srcWidth == destWidth && srcHeight == destHeight)
	{
		Texture2D::CopyTexture(srcTex, targetTex);
	}
	else
	{
		_scaleShader.Use();

		_scaleShader.setIVec2("srcScreenSize", glm::ivec2(srcWidth, srcHeight));
		_scaleShader.setIVec2("destScreenSize", glm::ivec2(destWidth, destHeight));

		glBindImageTexture(0, srcTex->GetID(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
		glBindImageTexture(1, targetTex->GetID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute((destWidth + work_size_x - 1) / work_size_x, (destHeight + work_size_y - 1) / work_size_y, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	return true;
}

void RayTracePass::SetEnableTAA(bool enable)
{
	if (useTAA == enable)
		return;

	useTAA = enable;
	if (useTAA) first_TAAIteration = true;
}

void RayTracePass::SetEnableDenoised(bool enable)
{
	if (useDenoised == enable)
		return;

	useDenoised = enable;
}