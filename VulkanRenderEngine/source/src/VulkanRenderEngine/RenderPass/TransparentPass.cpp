#include "OpenGLRenderEngine/RenderPass/TransparentPass.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "OpenGLRenderEngine/General/IndirectDrawManager.h"
#include "glm/gtc/matrix_transform.hpp"
#include <execution>

TransparentPass::TransparentPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
	:_shader(vertexShaderPath, fragmentShaderPath)
{
}

void TransparentPass::EarlyExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto GetDistanceToCamera = [&](std::shared_ptr<Mesh>& mesh, const glm::mat4& transform)-> float {
		auto aabb = mesh->GetAABB();
		aabb.MakeTransform(transform);
		auto cernter = aabb.min + (aabb.max - aabb.min) / 2.f;
		return glm::length2(state.camera.position - cernter);
		};

	using TransparentMeshItem = OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem;
	using TransparentSkinnedMeshItem = OpenGLRenderObjectData::SceneRenderData::TransparentSkinnedMeshItem;

	auto& meshItem = state.objects.sceneRenderData.transparentMesh;
	if (meshItem.size() > 1)
	{
		std::sort(std::execution::par_unseq, meshItem.begin(), meshItem.end(),
			[&](const TransparentMeshItem& item1, const TransparentMeshItem& item2)-> bool
			{
				return GetDistanceToCamera(item1.meshinfo.mesh, item1.transform) > GetDistanceToCamera(item2.meshinfo.mesh, item2.transform);
			}
		);
	}

	auto& skinnedItem = state.objects.sceneRenderData.transparentSkinnedMesh;
	if (skinnedItem.size() > 1)
	{
		std::sort(std::execution::par_unseq, skinnedItem.begin(), skinnedItem.end(),
			[&](const TransparentSkinnedMeshItem& item1, const TransparentSkinnedMeshItem& item2)-> bool
			{
				return GetDistanceToCamera(item1.meshinfo.mesh, item1.transform) > GetDistanceToCamera(item2.meshinfo.mesh, item2.transform);
			}
		);
	}
}

bool TransparentPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	bool empty = state.objects.sceneRenderData.transparentMesh.empty() && state.objects.sceneRenderData.transparentSkinnedMesh.empty();
	return state.option.flags.drawTransparent && !empty;
}

void TransparentPass::Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state)
{
	auto atlasShadowMap = ctx.GetInput(0);
	if (!atlasShadowMap)
		return;

	if (state.objects.sceneRenderData.transparentMesh.empty() && state.objects.sceneRenderData.transparentSkinnedMesh.empty())
		return;

	SetupIndirecDrawMaterial(state);

	glBindFramebuffer(GL_FRAMEBUFFER, ctx.renderTargetFBO);
	glViewport(0, 0, state.framebuffer.width, state.framebuffer.height);

	_shader.Use();

	_shader.setTexture(atlasShadowMap, "atlasShadowMap", 9);

	RenderHelp::SetupLightingData(_shader,
		state.lights.dirLightInfos,
		state.lights.pointLightInfos,
		state.lights.spotLightInfos,
		state.lights.shadowAtlas
	);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	RenderHelp::renderSceneTransparent(state, _shader, state.objects.sceneRenderData.transparentMesh, state.objects.sceneRenderData.transparentSkinnedMesh);
	glDisable(GL_BLEND);
}

void TransparentPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
}

void TransparentPass::SetupIndirecDrawMaterial(RenderState& state)
{
	auto indirectManager = IndirectDrawManager::Instance();

	for (auto& item : state.objects.sceneRenderData.transparentMesh)
	{
		auto& material = item.meshinfo.material;
		if (material->GetNeedUpdateIndirectDraw())
		{
			indirectManager->setupMaterial(*material);
			material->SetNeedUpdateIndirectDraw(false);
		}
	}
	for (auto& item : state.objects.sceneRenderData.transparentSkinnedMesh)
	{
		auto& material = item.meshinfo.material;
		if (material->GetNeedUpdateIndirectDraw())
		{
			indirectManager->setupMaterial(*material);
			material->SetNeedUpdateIndirectDraw(false);
		}
	}
}

void RenderHelp::renderSceneTransparent(
	RenderState& state, std::shared_ptr<VKWrapper::VKCommandBuffer> cmd,
	std::vector<OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem>& transMeshes,
	std::vector<OpenGLRenderObjectData::SceneRenderData::TransparentSkinnedMeshItem>& transSkinnedMeshes
)
{
	Frustum& frustum = state.camera.frustum;

	std::vector<bool> FrustumCullResult;
	FrustumCullResult.resize(transMeshes.size(), false);

	std::for_each(std::execution::par, transMeshes.begin(), transMeshes.end(),
		[&](OpenGLRenderObjectData::SceneRenderData::TransparentMeshItem& mesh)
		{
			size_t index = &mesh - transMeshes.data();
			AABB aabbworld = mesh.meshinfo.mesh->GetAABB();
			aabbworld.MakeTransform(mesh.transform);
			FrustumCullResult[index] = !frustum.IsAABBOnFrustum(aabbworld);
		});

	SetupAnimatorGroupData(shader, {});
	std::shared_ptr<Material> prevMaterial;
	for (size_t i = 0; i < transMeshes.size(); i++)
	{
		auto& mesh = transMeshes[i];
		if (FrustumCullResult[i])
			continue;

		auto& material = mesh.meshinfo.material;

		shader.setMat4("model", mesh.transform);

		if (prevMaterial != mesh.meshinfo.material)
		{
			mesh.meshinfo.ApplyMaterialWithSideOption();
			prevMaterial = mesh.meshinfo.material;
		}
		mesh.meshinfo.DrawGeometry(cmd);
	}

	prevMaterial = nullptr;
	for (size_t i = 0; i < transSkinnedMeshes.size(); i++)
	{
		auto& mesh = transSkinnedMeshes[i];
		if (FrustumCullResult[i])
			continue;

		auto& material = mesh.meshinfo.material;
		shader.setMat4("model", mesh.transform);
		SetupAnimatorGroupData(shader, *mesh.animators);

		if (prevMaterial != mesh.meshinfo.material)
		{
			mesh.meshinfo.ApplyMaterialWithSideOption();
			prevMaterial = mesh.meshinfo.material;
		}
		mesh.meshinfo.DrawGeometry(shader);
	}
}