#pragma once

#include "vkstdafx.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/RenderOption.h"
#include "VulkanRenderEngine/Base/TextureCube.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"

struct Plane {
	glm::vec3 normal;
	float     distance;

	Plane() = default;
	Plane(const glm::vec3& pointOnPlane, const glm::vec3& n);
	Plane(const glm::vec3& n, float d);
	float getSignedDistanceToPlane(const glm::vec3& point) const;
};

struct Frustum
{
	Plane nearFace;
	Plane farFace;
	Plane rightFace;
	Plane leftFace;
	Plane topFace;
	Plane bottomFace;

	std::array<glm::vec3, 8> corners;

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
		std::shared_ptr<StorageBlock> ssbo_pointLightMeta;
		std::shared_ptr<StorageBlock> ssbo_spotLightMeta;
		std::shared_ptr<AtlasMap> shadowAtlas;
	} lights;

	// 渲染对象
	struct RenderObjects {
		VKRenderObjectData::SceneRenderData sceneRenderData;
		VKRenderObjectData::FirstPersonRenderData firstPersonRenderData;
	} objects;

	struct IndirectCommands {
		std::vector<IndirectDrawCommand> staticMesh_OneSideCommand;
		std::vector<IndirectDrawCommand> staticMesh_TwoSideCommand;
		std::shared_ptr<StorageBlock> ssbo_StaticMesh_TransformAndMaterialIndices;
	} indirectCommands;

	struct SkyBoxParams {
		std::shared_ptr<TextureCube> cube;
	}skybox;

	// 渲染记录，由renderer填入
	struct RenderRecord {
		uint32_t frameIndex = 0;
		float prevEV100 = 0.0f;
		int64_t frameStartMicroTimeStamp = 0;		//帧启动时间
		int64_t frameEndMicroTimeStamp = 0;			//帧完成时间
	} renderRecord;

	RenderOption option;
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