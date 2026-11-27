#include "PhysicsShapeFromModel.h"
#include "ECSCore/World.h"
#include "Render/Components/RenderStaticModel.h"
#include "VulkanRenderEngine/Base/StaticModel.h"

#include "bullet/LinearMath/btConvexHullComputer.h"
#include "bullet/BulletCollision/CollisionShapes/btShapeHull.h"

#include "vhacd/VHACD.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace
{
	// 凸包顶点超过这个数就简化（Bullet 建议凸包顶点在 100 以内，太多会拖慢碰撞检测）
	constexpr int kMaxHullVertices = 100;
	// 三角形超过这个数时在结果里提醒（场景文件里每个顶点都要存一份，碰撞检测也更慢）
	constexpr size_t kLargeTriangleCount = 200000;

	// 按位置焊接顶点：同一位置（逐位相同）的顶点合成一个。模型里按法线 / UV 拆开的顶点位置是完全一样的
	struct PositionKey
	{
		uint32_t x, y, z;
		bool operator==(const PositionKey&) const = default;
	};
	struct PositionKeyHash
	{
		size_t operator()(const PositionKey& k) const noexcept
		{
			size_t h = k.x;
			h = h * 0x9E3779B1u ^ k.y;
			h = h * 0x9E3779B1u ^ k.z;
			return h;
		}
	};
	PositionKey MakeKey(const glm::vec3& p)
	{
		// -0 和 +0 当成同一个
		const glm::vec3 q(p.x == 0.0f ? 0.0f : p.x, p.y == 0.0f ? 0.0f : p.y, p.z == 0.0f ? 0.0f : p.z);
		PositionKey key{};
		std::memcpy(&key.x, &q.x, sizeof(float));
		std::memcpy(&key.y, &q.y, sizeof(float));
		std::memcpy(&key.z, &q.z, sizeof(float));
		return key;
	}

	bool IsFinite(const glm::vec3& p)
	{
		return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
	}

	struct ModelSource
	{
		std::shared_ptr<StaticModel> model;
		glm::mat4 trans{ 1.0f };
	};

	bool GetModel(World& world, Entity entity, ModelSource& out, std::string& error)
	{
		if (!entity || !world.IsValidWorldEntity(entity))
		{
			error = "实体已经不存在";
			return false;
		}
		const auto* render = entity.tryGetComponent<RenderStaticModel>();
		if (!render || !render->model)
		{
			error = "实体没有静态模型（RenderStaticModel），或者模型还没加载";
			return false;
		}
		out.model = render->model;
		out.trans = render->trans;
		return true;
	}

	// 一个子网格的顶点位置（乘上 trans）和三角形索引；没有索引的网格按顺序每 3 个顶点一个三角形
	void ReadMesh(const StaticMesh& mesh, const glm::mat4& trans, std::vector<glm::vec3>& positions, std::vector<unsigned int>& indices)
	{
		const auto& vertices = mesh.GetVertices();
		positions.resize(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i)
			positions[i] = glm::vec3(trans * glm::vec4(vertices[i].Position, 1.0f));
		indices = mesh.GetIndices();
		if (indices.empty())
		{
			indices.resize(vertices.size() - vertices.size() % 3);
			for (size_t i = 0; i < indices.size(); ++i)
				indices[i] = static_cast<unsigned int>(i);
		}
	}

	// 一组点的凸包顶点：先精确计算，超过 kMaxHullVertices 再用 btShapeHull 按 42 个方向的支撑点简化
	bool BuildHull(const std::vector<glm::vec3>& points, std::vector<glm::vec3>& out, int& exactCount)
	{
		out.clear();
		exactCount = 0;
		if (points.size() < 3)
			return false;

		btConvexHullComputer computer;
		computer.compute(&points[0].x, static_cast<int>(sizeof(glm::vec3)), static_cast<int>(points.size()), 0.0f, 0.0f);
		exactCount = computer.vertices.size();
		if (exactCount < 3)
			return false;

		if (exactCount <= kMaxHullVertices)
		{
			out.reserve(exactCount);
			for (int i = 0; i < exactCount; ++i)
				out.emplace_back(computer.vertices[i].x(), computer.vertices[i].y(), computer.vertices[i].z());
			return true;
		}

		btConvexHullShape exact;
		for (int i = 0; i < exactCount; ++i)
			exact.addPoint(computer.vertices[i], false);
		exact.recalcLocalAabb();
		exact.setMargin(0.0f);		// 支撑点不带外扩，否则简化后的凸包整体大一圈
		btShapeHull simplified(&exact);
		if (!simplified.buildHull(0.0f) || simplified.numVertices() < 3)
			return false;
		out.reserve(simplified.numVertices());
		const btVector3* v = simplified.getVertexPointer();
		for (int i = 0; i < simplified.numVertices(); ++i)
			out.emplace_back(v[i].x(), v[i].y(), v[i].z());
		return true;
	}

	std::shared_ptr<CollisionShape::ChildShape> MakeHullShape(std::vector<glm::vec3> vertices)
	{
		auto shape = std::make_shared<CollisionShape::ChildShape>();
		shape->shapeType = ShapeType::ConvexHull;
		shape->vertices = std::move(vertices);
		return shape;
	}

	// 整个模型焊接成一个三角网格：按位置合并重复顶点，去掉退化三角形（三角网格碰撞体、凸分解共用）
	struct WeldStats
	{
		size_t sourceTriangles = 0;
		size_t sourceVertices = 0;
		size_t skipped = 0;
	};

	void CollectWeldedTriangles(const ModelSource& source, std::vector<glm::vec3>& outVertices, std::vector<unsigned int>& outIndices, WeldStats& stats)
	{
		outVertices.clear();
		outIndices.clear();
		stats = {};
		std::unordered_map<PositionKey, unsigned int, PositionKeyHash> welded;
		std::vector<glm::vec3> positions;
		std::vector<unsigned int> indices;
		for (auto& info : source.model->GetMeshInfos())
		{
			if (!info.mesh)
				continue;
			ReadMesh(*info.mesh, source.trans, positions, indices);
			stats.sourceVertices += positions.size();

			std::vector<unsigned int> remap(positions.size(), UINT32_MAX);
			auto weld = [&](unsigned int index) -> unsigned int
				{
					if (remap[index] != UINT32_MAX)
						return remap[index];
					const auto [it, inserted] = welded.try_emplace(MakeKey(positions[index]), static_cast<unsigned int>(outVertices.size()));
					if (inserted)
						outVertices.push_back(positions[index]);
					return remap[index] = it->second;
				};

			for (size_t t = 0; t + 2 < indices.size(); t += 3)
			{
				++stats.sourceTriangles;
				const unsigned int i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
				if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()
					|| !IsFinite(positions[i0]) || !IsFinite(positions[i1]) || !IsFinite(positions[i2]))
				{
					++stats.skipped;
					continue;
				}
				// 退化三角形（面积为 0）对碰撞没用，还会让 Bullet 算出奇怪的法线
				const glm::vec3 n = glm::cross(positions[i1] - positions[i0], positions[i2] - positions[i0]);
				if (glm::dot(n, n) <= 1e-12f)
				{
					++stats.skipped;
					continue;
				}
				const unsigned int w0 = weld(i0), w1 = weld(i1), w2 = weld(i2);
				if (w0 == w1 || w1 == w2 || w0 == w2)
				{
					++stats.skipped;
					continue;
				}
				outIndices.push_back(w0);
				outIndices.push_back(w1);
				outIndices.push_back(w2);
			}
		}
	}

	PhysicsShapeFromModel::Result BuildTriangleMesh(const ModelSource& source)
	{
		PhysicsShapeFromModel::Result result;
		auto shape = std::make_shared<CollisionShape::ChildShape>();
		shape->shapeType = ShapeType::TriangleMesh;

		WeldStats stats;
		CollectWeldedTriangles(source, shape->vertices, shape->indices, stats);
		const size_t triangles = shape->indices.size() / 3;
		if (triangles == 0)
		{
			result.message = "模型里没有可用的三角形";
			return result;
		}
		result.ok = true;
		result.message = std::format("三角网格：{} 个三角形、{} 个顶点（原 {} 个三角形、{} 个顶点{}）",
			triangles, shape->vertices.size(), stats.sourceTriangles, stats.sourceVertices,
			stats.skipped ? std::format("，跳过 {} 个退化三角形", stats.skipped) : std::string());
		if (triangles > kLargeTriangleCount)
			result.message += "。三角形很多：场景文件会变大、碰撞检测变慢，建议用简化过的碰撞模型";
		result.shapes.push_back(std::move(shape));
		return result;
	}

	PhysicsShapeFromModel::Result BuildConvexHull(const ModelSource& source, bool perMesh)
	{
		PhysicsShapeFromModel::Result result;
		std::vector<glm::vec3> positions;
		std::vector<unsigned int> indices;
		std::vector<glm::vec3> all;
		size_t meshCount = 0, failed = 0, simplifiedCount = 0;
		std::string detail;

		// 只取被三角形用到的顶点，去掉重复位置，减少凸包计算量
		auto collect = [&](std::vector<glm::vec3>& points, std::unordered_map<PositionKey, int, PositionKeyHash>& seen)
			{
				for (unsigned int index : indices)
				{
					if (index >= positions.size() || !IsFinite(positions[index]))
						continue;
					if (seen.try_emplace(MakeKey(positions[index]), 0).second)
						points.push_back(positions[index]);
				}
			};

		std::unordered_map<PositionKey, int, PositionKeyHash> seenAll;
		for (auto& info : source.model->GetMeshInfos())
		{
			if (!info.mesh)
				continue;
			ReadMesh(*info.mesh, source.trans, positions, indices);
			if (!perMesh)
			{
				collect(all, seenAll);
				continue;
			}

			++meshCount;
			std::vector<glm::vec3> points;
			std::unordered_map<PositionKey, int, PositionKeyHash> seen;
			collect(points, seen);
			std::vector<glm::vec3> hull;
			int exact = 0;
			if (!BuildHull(points, hull, exact))
			{
				++failed;
				continue;
			}
			if (exact > kMaxHullVertices)
				++simplifiedCount;
			result.shapes.push_back(MakeHullShape(std::move(hull)));
		}

		if (!perMesh)
		{
			std::vector<glm::vec3> hull;
			int exact = 0;
			if (!BuildHull(all, hull, exact))
			{
				result.message = "算不出凸包（模型的顶点太少，或者都在一条直线上）";
				return result;
			}
			result.ok = true;
			result.message = exact > kMaxHullVertices
				? std::format("凸包：{} 个顶点（精确凸包 {} 个顶点，超过 {} 个已简化）", hull.size(), exact, kMaxHullVertices)
				: std::format("凸包：{} 个顶点", hull.size());
			result.shapes.push_back(MakeHullShape(std::move(hull)));
			return result;
		}

		if (result.shapes.empty())
		{
			result.message = "所有子网格都算不出凸包（顶点太少，或者都在一条直线上）";
			return result;
		}
		result.ok = true;
		result.message = std::format("凸包：{} 个（共 {} 个子网格{}{}）", result.shapes.size(), meshCount,
			failed ? std::format("，{} 个算不出凸包已跳过", failed) : std::string(),
			simplifiedCount ? std::format("，{} 个顶点太多已简化", simplifiedCount) : std::string());
		return result;
	}
}

namespace PhysicsShapeFromModel
{
	bool GetModelBounds(World& world, Entity entity, glm::vec3& outMin, glm::vec3& outMax, std::string& error)
	{
		ModelSource source;
		if (!GetModel(world, entity, source, error))
			return false;
		const AABB aabb = source.model->GetAABB();
		if (!aabb.isValid())
		{
			error = "模型没有顶点，算不出包围盒";
			return false;
		}
		// 包围盒的 8 个角乘上 trans（可能带旋转），再取轴对齐的范围
		outMin = glm::vec3(std::numeric_limits<float>::max());
		outMax = glm::vec3(std::numeric_limits<float>::lowest());
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 corner((i & 1) ? aabb.max.x : aabb.min.x, (i & 2) ? aabb.max.y : aabb.min.y, (i & 4) ? aabb.max.z : aabb.min.z);
			const glm::vec3 p(source.trans * glm::vec4(corner, 1.0f));
			outMin = glm::min(outMin, p);
			outMax = glm::max(outMax, p);
		}
		if (!IsFinite(outMin) || !IsFinite(outMax))
		{
			error = "模型的包围盒无效";
			return false;
		}
		return true;
	}

	void FitPrimitive(ShapeType type, const glm::vec3& min, const glm::vec3& max, glm::vec3& outSize, glm::vec3& outCenter)
	{
		constexpr float kMinSize = 0.001f;
		outCenter = (min + max) * 0.5f;
		const glm::vec3 half = glm::max((max - min) * 0.5f, glm::vec3(kMinSize));
		const float radiusXZ = std::max(half.x, half.z);
		switch (type)
		{
		case ShapeType::Sphere:
			outSize = glm::vec3(std::max({ half.x, half.y, half.z }));
			break;
		case ShapeType::Capsule:
		{
			// size.y 是两个半球球心之间的距离：总高 = size.y + 2r
			const float radius = std::min(radiusXZ, half.y);
			outSize = glm::vec3(radius, std::max(0.0f, half.y * 2.0f - radius * 2.0f), radius);
			break;
		}
		case ShapeType::Cylinder:
			outSize = glm::vec3(radiusXZ, half.y, radiusXZ);
			break;
		case ShapeType::Box:
		default:
			outSize = half;
			break;
		}
	}

	bool HasStaticModel(Entity entity)
	{
		if (!entity || !entity.isValid())
			return false;
		const auto* render = entity.tryGetComponent<RenderStaticModel>();
		return render && render->model;
	}

	Result Build(World& world, Entity entity, Kind kind)
	{
		Result result;
		ModelSource source;
		if (!GetModel(world, entity, source, result.message))
			return result;

		switch (kind)
		{
		case Kind::BoundingBox:
		{
			glm::vec3 mn, mx;
			if (!GetModelBounds(world, entity, mn, mx, result.message))
				return result;
			auto shape = std::make_shared<CollisionShape::ChildShape>();
			shape->shapeType = ShapeType::Box;
			FitPrimitive(ShapeType::Box, mn, mx, shape->size, shape->position);
			result.ok = true;
			result.message = std::format("包围盒：{:.2f} × {:.2f} × {:.2f}", shape->size.x * 2.0f, shape->size.y * 2.0f, shape->size.z * 2.0f);
			result.shapes.push_back(std::move(shape));
			return result;
		}
		case Kind::TriangleMesh: return BuildTriangleMesh(source);
		case Kind::ConvexHull: return BuildConvexHull(source, false);
		case Kind::ConvexHullPerMesh: return BuildConvexHull(source, true);
		}
		result.message = "未知的生成方式";
		return result;
	}
}

// ======================== 凸分解（V-HACD，后台线程） ========================

namespace
{
	static_assert(sizeof(glm::vec3) == sizeof(float) * 3, "V-HACD 按 x, y, z 紧密排列读顶点");

	// 同一时间只有一个分解任务。界面线程：Start / Cancel / GetStatus / Update；后台线程：跑 V-HACD
	struct DecompositionJob
	{
		std::mutex mutex;
		std::thread thread;
		std::atomic<bool> cancelRequested{ false };
		std::atomic<bool> done{ false };
		std::atomic<float> progress{ 0.0f };		// 0 ~ 1

		// 以下由 mutex 保护
		std::string stage;
		World* world = nullptr;
		Entity entity;
		bool replace = true;
		bool ok = false;
		std::string message;
		std::vector<std::shared_ptr<CollisionShape::ChildShape>> shapes;
		bool hasStatus = false;					// 有没有可以显示的状态（开始过一次）

		~DecompositionJob()
		{
			// 退出编辑器时还在算：通知取消并等它结束（V-HACD 很快就会停）
			cancelRequested = true;
			if (thread.joinable())
				thread.join();
		}
	};
	DecompositionJob s_job;

	// 进度回调：V-HACD 在计算线程上调用。顺便检查取消（Cancel 可能在 Compute 开始之前被调用，
	// 而 Compute 一开始会把内部的取消标记清掉，所以在回调里再转发一次）
	class DecompositionCallback : public VHACD::IVHACD::IUserCallback
	{
	public:
		explicit DecompositionCallback(VHACD::IVHACD* vhacd) : _vhacd(vhacd) {}

		void Update(const double overallProgress, const double stageProgress, const char* const stage, const char* operation) override
		{
			(void)stageProgress;
			(void)operation;
			s_job.progress = static_cast<float>(std::clamp(overallProgress / 100.0, 0.0, 1.0));
			{
				std::lock_guard lock(s_job.mutex);
				s_job.stage = stage ? stage : "";
			}
			if (s_job.cancelRequested)
				_vhacd->Cancel();
		}

	private:
		VHACD::IVHACD* _vhacd;
	};

	void RunDecomposition(PhysicsShapeFromModel::MeshData mesh, PhysicsShapeFromModel::DecompositionParams params)
	{
		const auto startTime = std::chrono::steady_clock::now();
		std::vector<std::shared_ptr<CollisionShape::ChildShape>> shapes;
		std::string message;
		bool ok = false;

		VHACD::IVHACD* vhacd = VHACD::CreateVHACD();
		DecompositionCallback callback(vhacd);
		VHACD::IVHACD::Parameters p;
		p.m_callback = &callback;
		p.m_maxConvexHulls = std::max(1u, params.maxHulls);
		p.m_resolution = std::clamp(params.resolution, 10000u, 10000000u);
		p.m_maxNumVerticesPerCH = std::clamp(params.maxVerticesPerHull, 8u, 255u);
		p.m_minimumVolumePercentErrorAllowed = std::clamp(params.volumeErrorPercent, 0.01, 50.0);
		p.m_fillMode = params.fillMode == 1 ? VHACD::FillMode::RAYCAST_FILL
			: params.fillMode == 2 ? VHACD::FillMode::SURFACE_ONLY
			: VHACD::FillMode::FLOOD_FILL;
		p.m_asyncACD = true;		// V-HACD 自己用线程池并行分解（这里整个调用已经在后台线程上）
		p.m_shrinkWrap = true;

		const bool computed = !s_job.cancelRequested && vhacd->Compute(&mesh.vertices[0].x, static_cast<uint32_t>(mesh.vertices.size()),
			mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size() / 3), p);

		if (s_job.cancelRequested)
			message = "已取消";
		else if (!computed || vhacd->GetNConvexHulls() == 0)
			message = "V-HACD 没有算出结果（模型可能不封闭：可以把填充方式改成「光线投射」再试）";
		else
		{
			size_t totalVertices = 0;
			for (uint32_t i = 0; i < vhacd->GetNConvexHulls(); ++i)
			{
				VHACD::IVHACD::ConvexHull hull;
				if (!vhacd->GetConvexHull(i, hull) || hull.m_points.size() < 4)
					continue;
				auto shape = std::make_shared<CollisionShape::ChildShape>();
				shape->shapeType = ShapeType::ConvexHull;
				shape->vertices.reserve(hull.m_points.size());
				for (const auto& point : hull.m_points)
					shape->vertices.emplace_back(static_cast<float>(point.mX), static_cast<float>(point.mY), static_cast<float>(point.mZ));
				totalVertices += shape->vertices.size();
				shapes.push_back(std::move(shape));
			}
			if (shapes.empty())
				message = "V-HACD 算出的凸包都太小（少于 4 个顶点），已全部丢弃";
			else
			{
				ok = true;
				const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
				message = std::format("凸分解：{} 个凸包，共 {} 个顶点（输入 {} 个三角形，用时 {:.1f} 秒）",
					shapes.size(), totalVertices, mesh.indices.size() / 3, seconds);
			}
		}
		vhacd->Release();

		{
			std::lock_guard lock(s_job.mutex);
			s_job.ok = ok;
			s_job.message = std::move(message);
			s_job.shapes = std::move(shapes);
		}
		s_job.progress = 1.0f;
		s_job.done = true;
	}
}

namespace PhysicsShapeFromModel
{
	bool ReadWeldedMesh(World& world, Entity entity, MeshData& out, std::string& error)
	{
		ModelSource source;
		if (!GetModel(world, entity, source, error))
			return false;
		WeldStats stats;
		CollectWeldedTriangles(source, out.vertices, out.indices, stats);
		if (out.indices.size() < 3 || out.vertices.size() < 4)
		{
			error = "模型里没有可用的三角形";
			return false;
		}
		return true;
	}

	namespace Decomposition
	{
		bool Start(World* world, Entity entity, MeshData mesh, const DecompositionParams& params, bool replace)
		{
			if (IsRunning() || !world || mesh.indices.size() < 3)
				return false;
			if (s_job.thread.joinable())
				s_job.thread.join();

			s_job.cancelRequested = false;
			s_job.done = false;
			s_job.progress = 0.0f;
			{
				std::lock_guard lock(s_job.mutex);
				s_job.stage = "准备";
				s_job.world = world;
				s_job.entity = entity;
				s_job.replace = replace;
				s_job.ok = false;
				s_job.message.clear();
				s_job.shapes.clear();
				s_job.hasStatus = true;
			}
			s_job.thread = std::thread(RunDecomposition, std::move(mesh), params);
			return true;
		}

		void Cancel()
		{
			if (IsRunning())
				s_job.cancelRequested = true;
		}

		bool IsRunning()
		{
			return s_job.thread.joinable() && !s_job.done;
		}

		Status GetStatus()
		{
			Status status;
			std::lock_guard lock(s_job.mutex);
			status.valid = s_job.hasStatus;
			status.running = s_job.thread.joinable() && !s_job.done;
			status.entity = s_job.entity;
			status.progress = s_job.progress;
			status.stage = s_job.stage;
			status.ok = s_job.ok;
			status.message = s_job.message;
			return status;
		}

		void Update(World* currentWorld)
		{
			if (!s_job.thread.joinable() || !s_job.done)
				return;
			s_job.thread.join();

			std::lock_guard lock(s_job.mutex);
			if (!s_job.ok)
				return;
			// 分解期间换了场景：结果作废（World 指针只比较，不解引用）
			if (!currentWorld || currentWorld != s_job.world)
			{
				s_job.ok = false;
				s_job.message = "场景已经切换，分解结果没有应用";
				s_job.shapes.clear();
				return;
			}

			// 写进 Physics 组件，和面板改形状一样删掉 PhysicsRuntime，下一次物理更新按新形状重建
			currentWorld->SubmitCommand([entity = s_job.entity, shapes = std::move(s_job.shapes), replace = s_job.replace](World& w) mutable
				{
					if (!w.IsValidWorldEntity(entity))
						return;
					auto* physics = entity.tryGetComponent<Physics>();
					if (!physics)
						return;
					auto children = replace ? decltype(physics->collisionShape.children){} : physics->collisionShape.children;
					for (auto& shape : shapes)
						children.push_back(std::move(shape));
					physics->collisionShape.children = std::move(children);
					if (entity.hasComponent<PhysicsRuntime>())
						entity.removeComponent<PhysicsRuntime>();
				});
			s_job.shapes.clear();
		}
	}
}
