#pragma once

#include "OpenGLRenderEngine/RenderPass/RayTraceGeneralPass.h"
#include "RenderPassBase.h"

class RayTraceReflectPass :public RenderPassBase
{
public:
	RayTraceReflectPass(
		const std::string& rayTraceComputerShaderPath,
		const std::string& denoisedComputerShaderPath,
		const std::string& scaleComputerShaderPath
	);
	~RayTraceReflectPass();

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

		std::shared_ptr<Texture2D> historyColorTexture;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> denoisedTexture;
		std::shared_ptr<Texture2D> outPutTexture;
	};

	bool DrawRayTrace(FrameRenderData& data, RenderState& state);
	bool DrawDenoised(FrameRenderData& data, RenderState& state);
	bool DrawScale(FrameRenderData& data, RenderState& state);

	void SetEnableDenoised(bool enable);

	bool BindGeneralData(Shader& shader);

private:
	Shader _rayTraceShader_useGbuffer;
	Shader _denoisedShader;
	Shader _scaleShader;

	bool useDenoised;

	std::shared_ptr<RayTraceGeneralBuffer> _buffers;
};
