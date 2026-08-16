#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/AtlasMap.h"
#include "OpenGLRenderEngine/General/OpenGLRenderContext.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "OpenGLRenderEngine/General/GroupMapper.h"

class BaseParticleProperties;

class RenderHelp
{
public:
	static void renderSceneTransparent(
		RenderState& state, Shader& shader,
		std::vector<OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem>& meshes,
		std::vector<OpenGLRenderObjectData::SceneRenderData::TransparentSkinnedMeshItem>& skinned
	);

	static void renderFirstPerson(
		RenderState& state, Shader& shader,
		std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueMeshItem>& opaqueMeshes,
		std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem>& opaqueSkinned,
		std::vector<OpenGLRenderObjectData::FirstPersonRenderData::TransparentMeshItem>& transparentMeshes,
		std::vector<OpenGLRenderObjectData::FirstPersonRenderData::OpaqueSkinnedModelItem>& transparentModels
	);

	static void renderScreenQuad();
	static void renderBillboardQuad(Shader& shader);
	static void renderCube(Shader& shader);
	static void renderSphere(Shader& shader);
	static void renderCylinder(Shader& shader);

	static void renderLightCube();
	static void renderLightSphere();

public:
	static void SetupAnimatorGroupData(Shader& shader, const std::vector<OpenGLRenderContext::AnimatorView>& animatorViews);
	static void SetupLightingData(Shader& shader,
		const std::vector<std::shared_ptr<DirLightInfo>>& dirLights,
		const std::vector<std::shared_ptr<PointLightInfo>>& pointLights,
		const std::vector<std::shared_ptr<SpotLightInfo>>& spotLights,
		const std::shared_ptr<AtlasMap>& atlasShadowMap
	);
};

std::shared_ptr<Model> GetFloorModel(const glm::vec2& scale = glm::vec2(1.0f), float textureScale = 1.0f);
std::shared_ptr<Model> GetSphereModel(float radius = 0.5f, int sectors = 36, int stacks = 18);
std::shared_ptr<Model> GetCubeModel(const glm::vec3& scale = glm::vec3(1.0f), float textureScale = 1.0f);
std::shared_ptr<Model> GetCylinderModel(float radius = 0.5f, float height = 1.f, int segments = 36);

bool DrawTexture(std::shared_ptr<Texture2D> texture, std::string path, uint32_t level = 0);