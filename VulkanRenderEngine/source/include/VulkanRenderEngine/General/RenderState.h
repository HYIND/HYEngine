#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/RenderOption.h"
#include "VulkanRenderEngine/Base/TextureCube.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

struct alignas(16) Plane {
	glm::vec3 normal;
	float distance;

	Plane() = default;
	Plane(const glm::vec3& pointOnPlane, const glm::vec3& n);
	Plane(const glm::vec3& n, float d);
	float getSignedDistanceToPlane(const glm::vec3& point) const;
};

struct alignas(16) Frustum
{
	Plane nearFace;
	Plane farFace;
	Plane rightFace;
	Plane leftFace;
	Plane topFace;
	Plane bottomFace;

	std::array<glm::vec4, 8> corners;

	Frustum() = default;
	Frustum(const glm::mat4& viewProj);

	bool IsAABBOnFrustum(AABB& aabb);
	bool IsSphereOnFrustum(const glm::vec3& center, float radius);
	bool IsPointOnFrustum(const glm::vec3& pos);
	bool IsIntersectsFrustum(Frustum& other);
};

class DirLight;
class PointLight;
class SpotLight;
class AtlasMap;

struct DirLightInfo
{
	std::shared_ptr<DirLight> light;
	bool renderCube;

	struct cascadeData {
		uint32_t id;
		float cascadePlaneDistance = 0;
		glm::mat4 lightSpaceMatrix;
		float radius = 0;
		glm::vec3 center;
	};
	std::vector<cascadeData> cascades;
};

struct SpotLightInfo
{
	std::shared_ptr<SpotLight> light;
	bool renderCube;

	struct Atlas
	{
		uint32_t id;
	};
	std::shared_ptr<Atlas> atlas;
};

struct PointLightInfo
{
	std::shared_ptr<PointLight> light;
	bool renderCube;

	struct Atlas
	{
		uint32_t ids[6];
		bool enable[6] = { false };
	};
	std::shared_ptr<Atlas> atlas;
};

struct alignas(16) TransAndMaterialIndex
{
	glm::mat4 model;
	uint32_t materialIndex = 0;
};

struct alignas(16) GeometryRenderData
{
	alignas(16) glm::mat4 curTransform = glm::mat4(1.0f);
	alignas(16) glm::mat4 prevTransform = glm::mat4(1.0f);
	uint32_t materialIndex = 0;
};

struct alignas(16) SkinnedMotionData
{
	uint32_t enabled = 0;			// 1 = 上一帧的蒙皮顶点可用，做重投影；0 = 没有上一帧数据
	uint32_t vertexOffset = 0;		// 本帧顶点区间在本帧蒙皮顶点缓冲里的起始顶点
	uint32_t prevVertexOffset = 0;	// 上一帧顶点区间在上一帧蒙皮顶点缓冲里的起始顶点
	uint32_t vertexCount = 0;		// 顶点数量（本帧与上一帧必须一致才算有效）
};

struct alignas(16) ParticleInstance
{
	alignas(16) glm::vec3 position = glm::vec3(0.0f);						// 世界空间中心
	float extentX = 1.0f;													// billboard 全宽（= transform 的 scale.x）
	float extentY = 1.0f;													// billboard 全高（= transform 的 scale.y）
	float roll = 0.0f;														// billboard 绕视轴旋转（弧度，0 = 与相机对齐）
	uint32_t mask = 0;														// billboard 的片元裁剪：0 None / 1 Disc / 2 SoftDisc
	uint32_t flags = 0;														// bit0 = 有材质，bit1 = mesh 几何
	alignas(16) glm::vec4 color = glm::vec4(1.0f);							// rgb = tint（乘材质 albedo），a = alpha（乘材质 opacity）
	alignas(16) glm::vec3 scale = glm::vec3(1.0f);							// mesh 几何（内置球 / 模型）的缩放
	uint32_t materialIndex = 0xffffffffu;									// 材质 SSBO 下标，0xffffffff = 没有材质
	alignas(16) glm::vec4 rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);		// quat(x, y, z, w)，mesh 几何的朝向
};
static_assert(sizeof(ParticleInstance) == 80, "ParticleInstance 必须与 shader 的 std430 布局一致");

struct SkinnedVertexBufferState
{
	std::shared_ptr<VertexBufferBlock> buffer;
};

struct FrameHistoryData
{
	struct SkinnedMeshRange
	{
		uint32_t vertexStart = 0;
		uint32_t vertexCount = 0;
	};

	struct PoseInfo
	{
		uint32_t globalTransformCount = 0;
		std::unordered_map<std::string, SkinnedMeshRange> meshes;
	};

	std::shared_ptr<SkinnedVertexBufferState> skinnedMesh_VertexBuffer = std::make_shared<SkinnedVertexBufferState>();
	std::unordered_map<const VKRenderContext::SkinnedPoseHistory*, PoseInfo> poses;
};

struct RenderState
{
	// 相机参数
	struct CameraParams
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec3 position;
		glm::vec3 direction;
		glm::vec3 directionUp;
		glm::vec3 directionRight;
		float nearPlane = 0.1f;
		float farPlane = 1000.f;
		float fov = 80.f;
		Frustum frustum;

		std::shared_ptr<UniformBlock> curUBO;
		std::shared_ptr<UniformBlock> prevUBO;
	} camera;

	// 帧缓冲参数
	struct FramebufferParams {
		uint32_t width;
		uint32_t height;
	} framebuffer;

	// 光照参数
	struct LightParams {
		std::vector<std::shared_ptr<DirLightInfo>> dirLightInfos;
		std::vector<std::shared_ptr<PointLightInfo>> pointLightInfos;
		std::vector<std::shared_ptr<SpotLightInfo>> spotLightInfos;

		std::shared_ptr<StorageBlock> ssbo_dirLightMeta;
		std::shared_ptr<StorageBlock> ssbo_dirLightCascade;
		std::shared_ptr<StorageBlock> ssbo_dirLightCascadeDistances;
		std::shared_ptr<StorageBlock> ssbo_pointLightMeta;
		std::shared_ptr<StorageBlock> ssbo_spotLightMeta;
		std::shared_ptr<AtlasMap> shadowAtlas;
	} lights;

	// 渲染对象
	struct RenderObjects {
		VKRenderObjectData::SceneStaticRenderData sceneStaticRenderData;
		VKRenderObjectData::SceneSkinnedRenderData sceneSkinnedRenderData;
	} objects;

	struct IndirectCommands {
		std::vector<IndirectDrawCommand> staticMesh_OneSideCommand;
		std::vector<IndirectDrawCommand> staticMesh_TwoSideCommand;
		std::shared_ptr<StorageBlock> ssbo_StaticMesh_TransformAndMaterialIndices;
		std::shared_ptr<StorageBlock> ssbo_StaticMesh_WorldAABB;

		std::vector<IndirectDrawCommand> skinnedMesh_OneSideCommand;
		std::vector<IndirectDrawCommand> skinnedMesh_TwoSideCommand;
		std::vector<IndirectDrawCommand> transparentMesh_Command;
		std::shared_ptr<StorageBlock> skinnedMesh_PaletteBuffer;
		std::shared_ptr<StorageBlock> ssbo_SkinnedMesh_TransformAndMaterialIndices;
		std::shared_ptr<StorageBlock> ssbo_SkinnedMesh_GeometryRenderData;
		std::shared_ptr<StorageBlock> ssbo_SkinnedMesh_MotionData;
	} indirectCommands;   

	// 蒙皮重投影的两帧关系数据：和 FilghtSync 的 curframeTimeLine/preFrameTimeLine 一样，
	// 本帧这份在构建 FrameData 时 make_shared，上一帧那份直接取上一帧 state 里的那份
	std::shared_ptr<FrameHistoryData> curFrameSkinnedData;
	std::shared_ptr<FrameHistoryData> preFrameSkinnedData;

	struct SkyBoxParams {
		std::shared_ptr<TextureCube> skyCube;
		bool hasSkyBoxPreData = false;
		std::shared_ptr<TextureCube> skyCubeDiffuse;
		std::shared_ptr<TextureCube> skyCubePrefilter;
		std::shared_ptr<Texture2D> brdfLUT;
	} skyboxParams;

	struct SkyAtmosphereParams {
		bool hasSkyAtmospherePreData = false;
		std::shared_ptr<Texture2D> transmittanceLut;
		std::shared_ptr<Texture2D> skyViewLut;
		std::shared_ptr<TextureCube> skyCube;
		std::shared_ptr<TextureCube> skyCubeDiffuse;
		std::shared_ptr<TextureCube> skyCubePrefilter;
		std::shared_ptr<Texture2D> brdfLUT;
	} skyAtmosphereParams;

	// 渲染记录，由renderer填入
	struct RenderRecord {
		uint32_t frameIndex = 0;
		float prevEV100 = 0.0f;
		int64_t frameStartMicroTimeStamp = 0;		//帧启动时间
		int64_t frameEndMicroTimeStamp = 0;			//帧完成时间
	} renderRecord;

	Scene3DRenderOption option;
};

class RenderStateBuilder
{
public:
	RenderStateBuilder& SetCamera(const glm::mat4& proj, const glm::mat4& view,
		const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& dirUp, const glm::vec3& dirRight,
		float nearP, float farP, float fov);
	std::shared_ptr<RenderState> Build();

private:
	std::shared_ptr<RenderState> context;
};