#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "OpenGLRenderEngine/Base/Particle.h"
#include "OpenGLRenderEngine/Base/LaserBeam.h"
#include "./RenderPassBase.h"

class EffectPass : public RenderPassBase
{
public:
	EffectPass(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	virtual ~EffectPass() = default;
	virtual bool ShouldExecute(RenderState& state) const;
	virtual void Excute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

private:
	void DrawParticle(std::shared_ptr<BaseParticleProperties> baseProperties, RenderState& state);
	void DrawLaserBeam(std::shared_ptr<LaserBeamProperties> properties, RenderState& state);

private:
	Shader _shader;
};