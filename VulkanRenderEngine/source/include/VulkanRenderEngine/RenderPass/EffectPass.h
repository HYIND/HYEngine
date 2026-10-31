#pragma once

#include <vector>
#include <memory>
#include <unordered_map>

#include "glm/glm.hpp"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/General/RenderState.h"
#include "./RenderPassBase.h"


class EffectPass : public RenderPassBase
{
public:
	EffectPass(
		const std::string& vertexShaderPath,
		const std::string& colorParticleFragmentShaderPath,
		const std::string& materialParticleFragmentShaderPath = "shader/effect/particle_material.fs",
		const std::string& oitColorParticleFragmentShaderPath = "shader/effect/particle_oit_color.fs",
		const std::string& oitMaterialParticleFragmentShaderPath = "shader/effect/particle_oit_material.fs",
		const std::string& oitCompositeVertexShaderPath = "shader/effect/particle_oit_composite.vs",
		const std::string& oitCompositeFragmentShaderPath = "shader/effect/particle_oit_composite.fs"
	);
	virtual ~EffectPass() = default;

	virtual bool ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state) override;
	virtual void Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state) override;

private:
	struct DrawGroup
	{
		bool material = false;						// true = 用采样材质的片元着色器
		bool alphaBlend = false;					// true = 走 OIT 累积，false = 直接加色
		uint32_t firstInstance = 0;					// 本组实例在 SSBO 里的起始下标
		uint32_t instanceCount = 0;					// 本组实例数量
		uint32_t indexCount = 0;					// 本组网格的索引数量
		uint32_t firstIndex = 0;					// 本组网格索引在全局索引缓冲里的起点
		int32_t vertexOffset = 0;					// 本组网格顶点在全局顶点缓冲里的起点
	};

private:
	GraphicsPipeline _additiveColorShader;		// 加色 + 无材质
	GraphicsPipeline _additiveMaterialShader;	// 加色 + 材质
	GraphicsPipeline _oitColorShader;			// OIT 累积 + 无材质（MRT）
	GraphicsPipeline _oitMaterialShader;		// OIT 累积 + 材质（MRT）
	GraphicsPipeline _oitCompositeShader;		// OIT 合成（全屏三角形）

	std::shared_ptr<StorageBlock> _instanceBuffer;

	std::shared_ptr<StaticModel> _quadModel;
	std::shared_ptr<StaticModel> _sphereModel;
	std::shared_ptr<StaticModel> _cubeModel;
	bool _unitMeshRegistered = false;

	struct ModelSubMeshGeometry
	{
		uint32_t indexCount = 0;
		uint32_t firstIndex = 0;
		int32_t vertexOffset = 0;
		std::shared_ptr<Material> material;
	};

	struct ModelGeometry
	{
		uint32_t version = 0;
		std::vector<ModelSubMeshGeometry> subMeshes;
	};

	uint32_t _quadIndexCount = 0;
	uint32_t _quadFirstIndex = 0;
	int32_t _quadVertexOffset = 0;

	uint32_t _sphereIndexCount = 0;
	uint32_t _sphereFirstIndex = 0;
	int32_t _sphereVertexOffset = 0;

	uint32_t _cubeIndexCount = 0;
	uint32_t _cubeFirstIndex = 0;
	int32_t _cubeVertexOffset = 0;

	std::vector<std::shared_ptr<StaticModel>> _frameModels;
};
