#pragma once

#include "glm\glm.hpp"
#include "VulkanRenderEngine/Base/Shader.h"
#include "VulkanRenderEngine/General/RenderItem.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "VulkanRenderEngine/Base/Particle.h"
#include "VulkanRenderEngine/Base/LaserBeam.h"
#include "./RenderPassBase.h"

class EffectPass : public RenderPassBase
{
public:
	EffectPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~EffectPass() = default;
	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassContext& ctx, RenderState& state);

private:
	void DrawParticle(std::shared_ptr<BaseParticleProperties> baseProperties, RenderState& state);
	void DrawLaserBeam(std::shared_ptr<LaserBeamProperties> properties, RenderState& state);

private:
	Shader _shader;
};