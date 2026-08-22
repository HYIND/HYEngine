#pragma once

#include "OpenGLRenderEngine/RenderPass/RayTraceGeneralPass.h"
#include "RenderPassBase.h"
#include "OpenGLRenderEngine/Base/Shader.h"

class RayTraceGIPass :public RenderPassBase
{
public:
	RayTraceGIPass(
		const std::string& rayTraceComputerShaderPath,
		const std::string& spatialDenoisingComputerShaderPath,
		const std::string& temporalDenoisingComputerShaderPath, 
		const std::string& scaleComputerShaderPath
	);
	~RayTraceGIPass();

	virtual bool ShouldExecute(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);
	virtual void Execute(OpenGLRenderGraph::FrameDataRegistry& registry, const OpenGLRenderGraph::PassContext& ctx, RenderState& state);
	virtual void FrameBegin(OpenGLRenderGraph::FrameDataRegistry& registry, RenderState& state);

	void SetGeneralBuffer(std::shared_ptr<RayTraceGeneralBuffer> buffer);

private:
	struct FrameRenderData
	{
		glm::ivec2 drawSize;
		glm::ivec2 scrSize;

		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;
		std::shared_ptr<Texture2D> sceneDepthBuffer;
		std::shared_ptr<Texture2D> atlasShadowMap;
		std::shared_ptr<Texture2D> ssaoMap;
		std::shared_ptr<Texture2D> gMotionVector;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> spatialDenoisingTexture;
		std::shared_ptr<Texture2D> outPutTexture;

		std::shared_ptr<Texture2D> historyColorTexture;
	};

	bool DrawRayTraceGI(FrameRenderData& data, RenderState& state);
	bool DrawSpatialDenoising(FrameRenderData& data, RenderState& state);
	bool DrawTemporalDenoising(FrameRenderData& data, RenderState& state);
	bool DrawScale(FrameRenderData& data, RenderState& state);

	void SetEnable(bool enable) const;

	bool BindGeneralData(Shader& shader);

private:
	Shader _rayTraceShader_useGbuffer;
	Shader _spatialDenoisingShader;
	Shader _temporalDenoisingShader;
	Shader _scaleShader;

	mutable bool _firstDrawTemporal;
	mutable bool _enable;

	std::shared_ptr<RayTraceGeneralBuffer> _buffers;
};
