#include "OpenGLRenderEngine/RenderPass/RayTraceGeneralPass.h"

static void WaitFence(GLsync& fence)
{
	if (fence && glIsSync(fence))
	{
		glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
		glDeleteSync(fence);
		fence = NULL;
	}
}

RayTraceGeneralBuffer::RayTraceGeneralBuffer()
	:
	_traiangleBufferManager(1000 * 1024),
	_traiangleExtBufferManager(3000 * 1024),
	_meshBVHNodeBufferManager(1000 * 1024)
{
	_SSBO_WorldBVHNode = std::make_shared<SSBO>();
	_SSBO_MeshmatData = std::make_shared<SSBO>();
}

std::shared_ptr<SSBO> RayTraceGeneralBuffer::GetTraiangles()
{
	return _traiangleBufferManager.GetBuffer()->GetSSBO();
}

std::shared_ptr<SSBO> RayTraceGeneralBuffer::GetTraiangleExt()
{
	return _traiangleExtBufferManager.GetBuffer()->GetSSBO();
}

std::shared_ptr<SSBO> RayTraceGeneralBuffer::GetMeshBVHNode()
{
	return _meshBVHNodeBufferManager.GetBuffer()->GetSSBO();
}

std::shared_ptr<SSBO> RayTraceGeneralBuffer::GetMeshmatData()
{
	return _SSBO_MeshmatData;
}

std::shared_ptr<SSBO> RayTraceGeneralBuffer::GetWorldBVHNode()
{
	return _SSBO_WorldBVHNode;
}

RayTraceGeneralPass::RayTraceGeneralPass()
	:
	_setupfence(nullptr)
{
	_buffers = std::make_shared<RayTraceGeneralBuffer>();
}

RayTraceGeneralPass::~RayTraceGeneralPass()
{
	if (_setupfence != nullptr)
	{
		auto guard = THREADCONTEXT->GetBindGuard();
		glDeleteSync(_setupfence);
	}
}

bool RayTraceGeneralPass::ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	return state.option.flags.rayTraceGIOn || state.option.flags.rayTraceReflectOn;
}

void RayTraceGeneralPass::Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state)
{
	WaitFence(_setupfence);
}

void RayTraceGeneralPass::FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state)
{

	if (!ShouldExecute(registry, state))
		return;

	auto guard = THREADCONTEXT->GetBindGuard();
	SetupGeneralBuffer(state);
	_setupfence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	glFlush();
}

std::shared_ptr<RayTraceGeneralBuffer> RayTraceGeneralPass::GetGeneralBuffer() {
	return _buffers;
}

bool RayTraceGeneralPass::SetupGeneralBuffer(RenderState& state)
{
	static auto copyVertex = [](comp_Vertex& comp_v, comp_Vertex_Extension& comp_v_ext, const Vertex& v)-> void {
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

	// 根据bvh中的indices重排triangles，让triangle直接对齐indices中的序号
	// 使原来通过indices查找三角形的查询路径，从triangles[indices[i]]直接变为triangles[i]
	static auto resortTriangleByBVHData = [](std::shared_ptr<const BVHData>& bvhData, std::vector<comp_Triangle>& triangles, std::vector<comp_Triangle_Extension>& triangleExts)
		{
			int tricount = bvhData->indices.size();

			std::vector<comp_Triangle> newTriangles;
			newTriangles.reserve(tricount);
			std::vector<comp_Triangle_Extension> newtriangleExts;
			newtriangleExts.reserve(tricount);

			for (int i = 0; i < tricount; i++)
			{
				int index = bvhData->indices[i];
				newTriangles.push_back(std::move(triangles[index]));
				newtriangleExts.push_back(std::move(triangleExts[index]));
			}

			triangles = std::move(newTriangles);
			triangleExts = std::move(newtriangleExts);
		};

	static auto resortMeshMatByBVHData = [](const std::shared_ptr<BVHData>& bvhData, std::vector<comp_MeshMatData>& meshmatDatas)
		{
			int meshmatcount = bvhData->indices.size();

			std::vector<comp_MeshMatData> newmeshmatDatas;
			newmeshmatDatas.reserve(meshmatcount);

			for (int i = 0; i < meshmatcount; i++)
			{
				int index = bvhData->indices[i];
				newmeshmatDatas.push_back(std::move(meshmatDatas[index]));
			}

			meshmatDatas = std::move(newmeshmatDatas);
		};

	float maxDistanceSqrt = state.option.rayTraceGeneralParams.maxDistance;
	float maxCacheClearDistanceSqrt = state.option.rayTraceGeneralParams.maxCacheClearDistance;
	maxDistanceSqrt *= maxDistanceSqrt;
	maxCacheClearDistanceSqrt *= maxCacheClearDistanceSqrt;

	auto& traiangleBufferManager = _buffers->_traiangleBufferManager;
	auto& traiangleExtBufferManager = _buffers->_traiangleExtBufferManager;
	auto& meshBVHNodeBufferManager = _buffers->_meshBVHNodeBufferManager;
	auto& meshmatDataBuffer = _buffers->_SSBO_MeshmatData;
	auto& worldBVHNodeBuffer = _buffers->_SSBO_WorldBVHNode;


	auto& opaqueMesh = state.objects.sceneRenderData.opaqueMesh;

	int triangleCount = 0;
	int meshBVHNodeCount = 0;
	int meshBVHIndicesCount = 0;

	std::vector<comp_MeshMatData> meshmatDatas;
	std::vector<AABB> meshAABBData;
	meshmatDatas.reserve(opaqueMesh.size());
	meshAABBData.reserve(opaqueMesh.size());


	for (auto& item : opaqueMesh)
	{
		auto& meshInfo = item.meshinfo;
		if (!meshInfo.mesh) continue;

		AABB aabb = meshInfo.mesh->GetAABB();
		aabb.MakeTransform(item.transform);
		float dis_sqrt = aabb.DistancePointToAABBSqrt(state.camera.position);
		if (dis_sqrt > maxCacheClearDistanceSqrt)
		{
			SegmentData data;
			traiangleBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
			traiangleExtBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
			meshBVHNodeBufferManager.RemoveSegment(meshInfo.mesh->GetUUID(), data);
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

		triangleCount += meshmatdata.mesh.triangleCount;
		meshBVHNodeCount += meshbvhdata->nodes.size();
		meshBVHIndicesCount += meshbvhdata->indices.size();

		auto mesh_viNewVersion = meshInfo.mesh->GetVerticesIndicesVsrsion();
		auto meshuuid = meshInfo.mesh->GetUUID();

		SegmentData triSegData;
		if (!traiangleBufferManager.FindSegment(meshuuid, triSegData) || (uint32_t)triSegData.userData != mesh_viNewVersion)
		{
			std::vector<comp_Triangle> triangles;
			std::vector<comp_Triangle_Extension> triangleExts;
			getTriangleData(*meshInfo.mesh, triangles, triangleExts);
			resortTriangleByBVHData(meshbvhdata, triangles, triangleExts);
			auto newSegmentTri = traiangleBufferManager.SetSegment(meshuuid, (void*)mesh_viNewVersion, (const char*)triangles.data(), triangles.size() * sizeof(comp_Triangle));
			auto newSegmentTriExt = traiangleExtBufferManager.SetSegment(meshuuid, (void*)mesh_viNewVersion, (const char*)triangleExts.data(), triangleExts.size() * sizeof(comp_Triangle_Extension));
			meshmatdata.mesh.triangleFirst = newSegmentTri.first / sizeof(comp_Triangle);

			assert(newSegmentTri.first % sizeof(comp_Triangle) == 0);
			assert(newSegmentTriExt.first % sizeof(comp_Triangle_Extension) == 0);
		}
		else
		{
			assert(triSegData.first % sizeof(comp_Triangle) == 0);
			meshmatdata.mesh.triangleFirst = triSegData.first / sizeof(comp_Triangle);
		}


		SegmentData meghBVHNodeSegData;
		if (!meshBVHNodeBufferManager.FindSegment(meshuuid, meghBVHNodeSegData) || (uint32_t)meghBVHNodeSegData.userData != mesh_viNewVersion)
		{
			auto newSegmentBVHNode = meshBVHNodeBufferManager.SetSegment(meshuuid, (void*)mesh_viNewVersion, (const char*)meshbvhdata->nodes.data(), meshbvhdata->nodes.size() * sizeof(BVHNode));
			meshmatdata.mesh.bvhNodeFirst = newSegmentBVHNode.first / sizeof(BVHNode);

			assert(newSegmentBVHNode.first % sizeof(BVHNode) == 0);
		}
		else
		{
			assert(meghBVHNodeSegData.first % sizeof(BVHNode) == 0);
			meshmatdata.mesh.bvhNodeFirst = meghBVHNodeSegData.first / sizeof(BVHNode);
		}

		meshmatDatas.push_back(meshmatdata);
		meshAABBData.push_back(aabb);
	}

	auto worldBVHData = BVHBuilder::Build(meshAABBData, OpenGLRenderConfig::RayTrace_World_BVH_Leaf_MeshCount, OpenGLRenderConfig::RayTrace_Max_Recursive_Depth);
	resortMeshMatByBVHData(worldBVHData, meshmatDatas);

	meshmatDataBuffer->WriteData(meshmatDatas.data(), meshmatDatas.size() * sizeof(comp_MeshMatData), 0);

	writePaddingCount(worldBVHData->nodes.size(), worldBVHNodeBuffer);
	worldBVHNodeBuffer->WriteData(worldBVHData->nodes.data(), worldBVHData->nodes.size() * sizeof(BVHNode), 16);

	return true;
}
