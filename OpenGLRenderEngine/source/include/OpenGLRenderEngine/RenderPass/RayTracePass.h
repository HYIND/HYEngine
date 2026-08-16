#pragma once

#define GLEW_STATIC    
#include "GL\glew.h"
#include "glm\glm.hpp"
#include "OpenGLRenderEngine/Base/Shader.h"
#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/Base/AtlasMap.h"
#include "OpenGLRenderEngine/General/RenderItem.h"
#include "OpenGLRenderEngine/General/RenderState.h"
#include "OpenGLRenderEngine/General/SegmentBufferManager.h"
#include "OpenGLRenderEngine/General/MemorySegmentBuffer.h"
#include "RenderPassBase.h"


class RayTracePass :public RenderPassBase
{
public:
	RayTracePass(
		const std::string& rayTraceComputerShaderPath,
		const std::string& TAAComputerShaderPath,
		const std::string& denoisedComputerShaderPath,
		const std::string& scaleComputerShaderPath
	);
	~RayTracePass();

	virtual bool ShouldExecute(RenderState& state) const;
	virtual void Execute(const OpenGLRenderGraph::PassContext& ctx, RenderState& state);

	virtual void FrameBegin(RenderState& state);

private:
	struct FrameRenderData
	{
		glm::ivec2 drawSize;
		glm::ivec2 scrSize;

		std::shared_ptr<Texture2D> gPosition;
		std::shared_ptr<Texture2D> gNormal;
		std::shared_ptr<Texture2D> gAlbedoOpacity;
		std::shared_ptr<Texture2D> gMetallicRoughness;
		std::shared_ptr<Texture2D> sceneColorBuffer;
		std::shared_ptr<Texture2D> sceneDepthBuffer;
		std::shared_ptr<Texture2D> atlasShadowMap;

		std::shared_ptr<Texture2D> originTexture;
		std::shared_ptr<Texture2D> denoisedTexture;
		std::shared_ptr<Texture2D> TAA_Texture[2];
		std::shared_ptr<Texture2D> TAA_LastTexture;
		std::shared_ptr<Texture2D> outPutTexture;
	};

	bool DrawRayTrace(FrameRenderData& data, RenderState& state);
	bool DrawTAA(FrameRenderData& data, RenderState& state);
	bool DrawDenoised(FrameRenderData& data, RenderState& state);
	bool DrawScale(FrameRenderData& data, RenderState& state);

	void SetEnableTAA(bool enable);
	void SetEnableDenoised(bool enable);

	bool SetupMeshBuffer(Shader& shader, RenderState& state);

private:
	Shader _rayTraceShader_pureRayTrace;
	Shader _rayTraceShader_useGbuffer;

	Shader _TAAShader;
	Shader _denoisedShader;
	Shader _scaleShader;

	bool useDenoised;
	bool useTAA;
	bool first_TAAIteration;
	int curTAAOutPutIndex;

	SegmentBufferManager<MemorySegmentBuffer, std::string> _traiangleBufferManager;
	SegmentBufferManager<MemorySegmentBuffer, std::string> _traiangleExtBufferManager;
	SegmentBufferManager<MemorySegmentBuffer, std::string> _meshBVHNodeBufferManager;
	bool _useGBuffer;
	bool _forceFlushBuffer;

	GLsync _setupfence;
};
