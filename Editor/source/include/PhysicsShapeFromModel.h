#pragma once

#include "ECSCore/Entity.h"
#include "Components/Physics.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class World;

// 从实体的静态模型（RenderStaticModel）生成碰撞形状。
// 坐标：顶点乘上 RenderStaticModel::trans，落在实体局部空间（Transform 之前）；形状的偏移为 0、旋转为单位，
// Transform 的缩放由 PhysicsSystem 对复合形状 setLocalScaling 加上去，和渲染用的矩阵（Transform × trans）一致。
// 模型换了 / 改了不会自动更新，需要重新生成。只支持静态模型（骨骼模型动起来后形状对不上）
namespace PhysicsShapeFromModel
{
	enum class Kind
	{
		BoundingBox,			// 模型包围盒的 Box（最便宜，任何物体类型都能用）
		TriangleMesh,			// 整个模型一个三角网格（焊接重复顶点、去掉退化三角形）。只能用在 Static 物体上
		ConvexHull,				// 整个模型一个凸包（顶点太多时简化）
		ConvexHullPerMesh,		// 每个子网格一个凸包（比一个凸包更贴合凹形模型）
	};

	struct Result
	{
		bool ok = false;
		std::string message;		// 成功时是统计信息，失败时是原因
		std::vector<std::shared_ptr<CollisionShape::ChildShape>> shapes;
	};

	// 实体有没有可用的静态模型（界面线程用来决定按钮是否可点，只看组件和指针）
	bool HasStaticModel(Entity entity);

	// 生成形状。必须在场景线程调用（读模型的顶点）
	Result Build(World& world, Entity entity, Kind kind);

	// 模型在实体局部空间里的包围盒（乘上 RenderStaticModel::trans 之后）。必须在场景线程调用
	bool GetModelBounds(World& world, Entity entity, glm::vec3& outMin, glm::vec3& outMax, std::string& error);

	// 让基本形状（Box / Sphere / Capsule / Cylinder，竖直方向为 y）包住包围盒 [min, max]：
	// 输出形状的 size（同 CollisionShape::ChildShape::size 的约定）和中心（作为形状的偏移）
	void FitPrimitive(ShapeType type, const glm::vec3& min, const glm::vec3& max, glm::vec3& outSize, glm::vec3& outCenter);

	// ───────────── 凸分解（V-HACD 4，ThirdParty/vhacd/VHACD.h） ─────────────
	// 把凹形模型拆成多个凸包：Dynamic / Kinematic 物体也能贴合凹形模型。
	// 计算要几百毫秒到几十秒，在后台线程上跑；完成后在界面线程的 Update 里把结果提交到场景线程，写进 Physics 组件

	struct DecompositionParams
	{
		uint32_t maxHulls = 16;				// 最多几个凸包
		uint32_t resolution = 100000;		// 体素数量（越大越精细、越慢；V-HACD 默认 40 万）
		uint32_t maxVerticesPerHull = 64;	// 每个凸包最多几个顶点
		double volumeErrorPercent = 1.0;	// 凸包和体素体积相差小于这个百分比就不再细分
		int fillMode = 0;					// 0 洪水填充（默认）、1 光线投射（模型不封闭时用）、2 只取表面
	};

	// 焊接后的整个模型（顶点在实体局部空间，同 Build）
	struct MeshData
	{
		std::vector<glm::vec3> vertices;
		std::vector<uint32_t> indices;
	};

	// 读出焊接后的模型三角形，给凸分解用。必须在场景线程调用
	bool ReadWeldedMesh(World& world, Entity entity, MeshData& out, std::string& error);

	namespace Decomposition
	{
		struct Status
		{
			bool valid = false;			// 开始过一次才有
			bool running = false;
			Entity entity;
			float progress = 0.0f;		// 0 ~ 1
			std::string stage;
			bool ok = false;
			std::string message;		// 完成后：统计信息或失败原因
		};

		// 开始分解（同一时间只能有一个）。replace：替换现有形状还是追加
		bool Start(World* world, Entity entity, MeshData mesh, const DecompositionParams& params, bool replace);
		void Cancel();
		bool IsRunning();
		Status GetStatus();

		// 每帧在界面线程调用一次：分解完成时把结果提交到场景线程（写进 Physics 组件、重建物理体）。
		// currentWorld 和开始时的不一样（换了场景）就丢掉结果
		void Update(World* currentWorld);
	}
}
