#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/EffectPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/Base/Particle.h"
#include "VulkanRenderEngine/Base/Effect.h"
#include "VulkanRenderEngine/Base/Material.h"

#include <algorithm>
#include <execution>
#include <tuple>


namespace
{

	void AddParticleVertexInput(GraphicsPipelineConfig& config)
	{
		vk::VertexInputAttributeDescription positionAttr;
		positionAttr.setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32B32Sfloat).setOffset(offsetof(Vertex, Position));

		vk::VertexInputAttributeDescription uvAttr;
		uvAttr.setLocation(1).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(offsetof(Vertex, TexCoords));

		vk::VertexInputBindingDescription bindingDescription;
		bindingDescription.setBinding(0).setStride(sizeof(Vertex)).setInputRate(vk::VertexInputRate::eVertex);

		config.AddVertexInputAttributeDescription(positionAttr);
		config.AddVertexInputAttributeDescription(uvAttr);
		config.AddVertexInputBindingDescription(bindingDescription);
	}

	std::shared_ptr<StaticModel> MakeUnitQuadModel()
	{
		std::vector<Vertex> vertices(4);
		vertices[0].Position = glm::vec3(-0.5f, -0.5f, 0.0f);
		vertices[0].TexCoords = glm::vec2(0.0f, 0.0f);
		vertices[1].Position = glm::vec3(0.5f, -0.5f, 0.0f);
		vertices[1].TexCoords = glm::vec2(1.0f, 0.0f);
		vertices[2].Position = glm::vec3(0.5f, 0.5f, 0.0f);
		vertices[2].TexCoords = glm::vec2(1.0f, 1.0f);
		vertices[3].Position = glm::vec3(-0.5f, 0.5f, 0.0f);
		vertices[3].TexCoords = glm::vec2(0.0f, 1.0f);

		for (auto& vertex : vertices)
		{
			vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
			vertex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
			vertex.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
		}

		std::vector<unsigned int> indices = { 0, 1, 2, 2, 3, 0 };

		std::shared_ptr<StaticMesh> mesh = std::make_shared<StaticMesh>(vertices, indices);
		std::shared_ptr<Material> material = std::make_shared<Material>();

		auto model = std::make_shared<StaticModel>();
		model->AddMesh(mesh, material);
		return model;
	}

	// 加色混合：srcAlpha / one，alpha 也按 one 累加（顺序无关）
	vk::PipelineColorBlendAttachmentState MakeAdditiveBlend()
	{
		vk::PipelineColorBlendAttachmentState blend;
		blend
			.setBlendEnable(true)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
			.setDstColorBlendFactor(vk::BlendFactor::eOne)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eSrcAlpha)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOne)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		return blend;
	}

	// OIT 累积缓冲
	vk::PipelineColorBlendAttachmentState MakeOitAccumBlend()
	{
		vk::PipelineColorBlendAttachmentState blend;
		blend
			.setBlendEnable(true)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setSrcColorBlendFactor(vk::BlendFactor::eOne)
			.setDstColorBlendFactor(vk::BlendFactor::eOne)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOne)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		return blend;
	}

	// OIT 透射率缓冲
	vk::PipelineColorBlendAttachmentState MakeOitRevealBlend()
	{
		vk::PipelineColorBlendAttachmentState blend;
		blend
			.setBlendEnable(true)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setAlphaBlendOp(vk::BlendOp::eAdd)
			.setSrcColorBlendFactor(vk::BlendFactor::eZero)
			.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcColor)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eZero)
			.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcColor)
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		return blend;
	}

}

constexpr uint32_t PARTICLE_FLAG_MATERIAL = 1u;
constexpr uint32_t PARTICLE_FLAG_MESH = 2u;

EffectPass::EffectPass(
	const std::string& vertexShaderPath,
	const std::string& colorParticleFragmentShaderPath,
	const std::string& materialParticleFragmentShaderPath,
	const std::string& oitColorParticleFragmentShaderPath,
	const std::string& oitMaterialParticleFragmentShaderPath,
	const std::string& oitCompositeVertexShaderPath,
	const std::string& oitCompositeFragmentShaderPath
)
{
	_instanceBuffer = std::make_shared<StorageBlock>();

	// ---------------- 加色管线（顺序无关，直接叠到场景色） ----------------
	{
		GraphicsPipelineConfig config;
		config.vertexPath = vertexShaderPath;
		config.fragmentPath = colorParticleFragmentShaderPath;

		AddParticleVertexInput(config);

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, MakeAdditiveBlend());
		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(true, false, vk::CompareOp::eLess);		// 测深度但不写深度
		config.SetCullMode(vk::CullModeFlagBits::eNone);			// 粒子双面可见
		config.SetTopology(vk::PrimitiveTopology::eTriangleList);
		config.AddCameraUnifromDataBinding();
		config.AddStorageBuffer(0);

		if (config.Validate())
			_additiveColorShader.Create(config);
	}

	{
		GraphicsPipelineConfig config;
		config.vertexPath = vertexShaderPath;
		config.fragmentPath = materialParticleFragmentShaderPath;

		AddParticleVertexInput(config);

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, MakeAdditiveBlend());
		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(true, false, vk::CompareOp::eLess);
		config.SetCullMode(vk::CullModeFlagBits::eNone);
		config.SetTopology(vk::PrimitiveTopology::eTriangleList);
		config.AddCameraUnifromDataBinding();
		config.AddStorageBuffer(0);
		config.AddBindlessMaterialTextureBinding();					// set 2：材质 SSBO + 非 uniform 贴图数组

		if (config.Validate())
			_additiveMaterialShader.Create(config);
	}

	// ---------------- OIT 累积管线（两张附件：accum + reveal） ----------------
	{
		GraphicsPipelineConfig config;
		config.vertexPath = vertexShaderPath;
		config.fragmentPath = oitColorParticleFragmentShaderPath;

		AddParticleVertexInput(config);

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, MakeOitAccumBlend());
		config.AddColorAttachment(vk::Format::eR16Sfloat, MakeOitRevealBlend());
		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(true, false, vk::CompareOp::eLess);
		config.SetCullMode(vk::CullModeFlagBits::eNone);
		config.SetTopology(vk::PrimitiveTopology::eTriangleList);
		config.AddCameraUnifromDataBinding();
		config.AddStorageBuffer(0);

		if (config.Validate())
			_oitColorShader.Create(config);
	}

	{
		GraphicsPipelineConfig config;
		config.vertexPath = vertexShaderPath;
		config.fragmentPath = oitMaterialParticleFragmentShaderPath;

		AddParticleVertexInput(config);

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, MakeOitAccumBlend());
		config.AddColorAttachment(vk::Format::eR16Sfloat, MakeOitRevealBlend());
		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(true, false, vk::CompareOp::eLess);
		config.SetCullMode(vk::CullModeFlagBits::eNone);
		config.SetTopology(vk::PrimitiveTopology::eTriangleList);
		config.AddCameraUnifromDataBinding();
		config.AddStorageBuffer(0);
		config.AddBindlessMaterialTextureBinding();

		if (config.Validate())
			_oitMaterialShader.Create(config);
	}

	// ---------------- OIT 合成管线（全屏三角形，无顶点输入） ----------------
	{
		GraphicsPipelineConfig config;
		config.vertexPath = oitCompositeVertexShaderPath;
		config.fragmentPath = oitCompositeFragmentShaderPath;

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, GraphicsPipelineConfig::DefaultColorAttachmentTransparentBlend);
		config.SetDepthStencilAttachmentFormat(vk::Format::eD24UnormS8Uint);
		config.SetDepthTest(false, false, vk::CompareOp::eAlways);
		config.SetCullMode(vk::CullModeFlagBits::eNone);
		config.SetTopology(vk::PrimitiveTopology::eTriangleList);
		config.AddUnifromTexture(1, 0);		// accum
		config.AddUnifromTexture(2, 0);		// reveal

		if (config.Validate())
			_oitCompositeShader.Create(config);
	}

	_quadModel = MakeUnitQuadModel();
	_sphereModel = RenderHelper::GetSphereModel(0.5f, 24, 12);
	_cubeModel = RenderHelper::GetCubeModel(glm::vec3(0.5f), 1.0f);
}

bool EffectPass::ShouldExecute(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	for (auto& frame : state.objects.sceneStaticRenderData.particleFrames)
	{
		if (frame && !frame->batches.empty())
			return true;
	}
	return false;
}

namespace
{
	// 网格在局部空间的包围半径（所有子网格 AABB 角点到原点的最大距离）
	float MeshBoundRadius(const std::shared_ptr<StaticModel>& model)
	{
		float radius = 0.0f;
		if (!model)
			return radius;
		for (auto& info : model->GetMeshInfos())
		{
			if (!info.mesh)
				continue;
			AABB aabb = info.mesh->GetAABB();
			radius = std::max(radius, glm::length(glm::max(glm::abs(aabb.min), glm::abs(aabb.max))));
		}
		return radius;
	}
}

const EffectPass::ModelGeometry* EffectPass::GetModelGeometry(const std::shared_ptr<StaticModel>& model)
{
	if (!model)
		return nullptr;

	auto indirectManager = IndirectDrawManager::Instance();

	// 几何区间只在第一次用到这个模型（或它某个子网格的版本变了）时取一次
	uint32_t modelVersion = 0;
	for (auto& info : model->GetMeshInfos())
	{
		if (info.mesh)
			modelVersion += info.mesh->GetVerticesIndicesVsrsion();
	}

	// owner 失效：原来的模型已经释放、这个地址被新模型复用了，不能用旧的区间
	auto cached = _modelGeometries.find(model.get());
	if (cached == _modelGeometries.end() || cached->second.owner.expired() || cached->second.version != modelVersion)
	{
		ModelGeometry geometry;
		geometry.owner = model;
		geometry.version = modelVersion;
		geometry.localRadius = MeshBoundRadius(model);

		auto& meshInfos = model->GetMeshInfos();
		for (uint32_t infoIndex = 0; infoIndex < meshInfos.size(); ++infoIndex)
		{
			auto& info = meshInfos[infoIndex];
			if (!info.mesh)
				continue;

			IndirectDrawMeta meta;
			if (!indirectManager->GetIndirectDrawMeta(*info.mesh, meta) || meta.indexCount == 0)
				continue;

			ModelSubMeshGeometry subMesh;
			subMesh.indexCount = meta.indexCount;
			subMesh.firstIndex = meta.firstIndex;
			subMesh.vertexOffset = meta.vertexOffset;
			subMesh.meshInfoIndex = infoIndex;
			geometry.subMeshes.push_back(std::move(subMesh));
		}

		cached = _modelGeometries.insert_or_assign(model.get(), std::move(geometry)).first;
	}
	return &cached->second;
}

void EffectPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto& drawGroups = registry.Get<std::vector<DrawGroup>>("Groups");

	auto indirectManager = IndirectDrawManager::Instance();

	// 模型已经释放的缓存项清掉（切场景、切工程后不再用的自定义粒子模型）
	std::erase_if(_modelGeometries, [](const auto& item) { return item.second.owner.expired(); });

	if (!_unitMeshRegistered)
	{
		auto setupUnitMesh = [&](const std::shared_ptr<StaticModel>& model, uint32_t& indexCount, uint32_t& firstIndex, int32_t& vertexOffset)
			{
				indexCount = 0;
				firstIndex = 0;
				vertexOffset = 0;

				if (!model)
					return;

				for (auto& info : model->GetMeshInfos())
				{
					if (!info.mesh)
						continue;

					indirectManager->SetupMesh(*info.mesh);

					IndirectDrawMeta meta;
					if (indirectManager->GetIndirectDrawMeta(*info.mesh, meta))
					{
						indexCount = meta.indexCount;
						firstIndex = meta.firstIndex;
						vertexOffset = meta.vertexOffset;
					}
					break;
				}
			};

		setupUnitMesh(_quadModel, _quadIndexCount, _quadFirstIndex, _quadVertexOffset);
		setupUnitMesh(_sphereModel, _sphereIndexCount, _sphereFirstIndex, _sphereVertexOffset);
		setupUnitMesh(_cubeModel, _cubeIndexCount, _cubeFirstIndex, _cubeVertexOffset);

		_quadRadius = MeshBoundRadius(_quadModel);
		_sphereRadius = MeshBoundRadius(_sphereModel);
		_cubeRadius = MeshBoundRadius(_cubeModel);

		_unitMeshRegistered = (_quadIndexCount > 0 && _sphereIndexCount > 0 && _cubeIndexCount > 0);
	}

	std::unordered_map<const Material*, uint32_t> materialIndexCache;
	auto resolveMaterialIndex = [&](const std::shared_ptr<Material>& material, uint32_t& materialIndex) -> bool
		{
			if (!material)
				return false;

			auto cached = materialIndexCache.find(material.get());
			if (cached != materialIndexCache.end())
			{
				materialIndex = cached->second;
				return true;
			}

			if (!indirectManager->GetMaterialIndex(*material, materialIndex))
				return false;

			materialIndexCache.emplace(material.get(), materialIndex);
			return true;
		};

	// 一个批次 = 渲染参数相同的一段连续粒子。网格区间 / 材质下标 / 剔除都按批次做，不再逐粒子分类
	struct SubDraw
	{
		uint32_t indexCount = 0;
		uint32_t firstIndex = 0;
		int32_t vertexOffset = 0;
		const std::shared_ptr<Material>* material = nullptr;
		float localRadius = 0.0f;
	};
	struct PendingGroup
	{
		const ParticleRenderParticle* source = nullptr;
		uint32_t count = 0;
		uint32_t mask = 0;
		uint32_t flags = 0;
		uint32_t materialIndex = 0xffffffffu;
		DrawGroup group;
	};

	std::vector<PendingGroup> pending;
	std::vector<SubDraw> subDraws;
	for (auto& frame : state.objects.sceneStaticRenderData.particleFrames)
	{
		if (!frame)
			continue;

		for (const ParticleRenderBatch& batch : frame->batches)
		{
			if (batch.particleCount == 0 || batch.firstParticle + batch.particleCount > frame->particles.size())
				continue;

			const ParticleRenderInfo& renderInfo = batch.renderInfo;
			subDraws.clear();
			if (!renderInfo.mesh)
			{
				if (_quadIndexCount > 0)
					subDraws.push_back(SubDraw{ _quadIndexCount, _quadFirstIndex, _quadVertexOffset, &renderInfo.material, _quadRadius });
			}
			else
			{
				switch (renderInfo.meshType)
				{
				case ParticleMeshType::Cube:
					if (_cubeIndexCount > 0)
						subDraws.push_back(SubDraw{ _cubeIndexCount, _cubeFirstIndex, _cubeVertexOffset, &renderInfo.material, _cubeRadius });
					break;
				case ParticleMeshType::Sphere:
					if (_sphereIndexCount > 0)
						subDraws.push_back(SubDraw{ _sphereIndexCount, _sphereFirstIndex, _sphereVertexOffset, &renderInfo.material, _sphereRadius });
					break;
				case ParticleMeshType::Custom:
					if (const ModelGeometry* geometry = GetModelGeometry(renderInfo.model))
					{
						// 材质从模型现取：这一帧的 renderInfo 持有模型，指针在本帧内有效
						auto& meshInfos = renderInfo.model->GetMeshInfos();
						for (auto& subMesh : geometry->subMeshes)
						{
							const std::shared_ptr<Material>* material = subMesh.meshInfoIndex < meshInfos.size() ? &meshInfos[subMesh.meshInfoIndex].material : nullptr;
							subDraws.push_back(SubDraw{ subMesh.indexCount, subMesh.firstIndex, subMesh.vertexOffset, material, geometry->localRadius });
						}
					}
					break;
				default:
					break;
				}
			}
			if (subDraws.empty())
				continue;

			// 视锥剔除：粒子中心包围盒按「最大尺寸 × 网格包围半径」外扩
			float localRadius = 0.0f;
			for (auto& subDraw : subDraws)
				localRadius = std::max(localRadius, subDraw.localRadius);
			const glm::vec3 inflate(batch.maxExtent * localRadius);
			AABB bounds(batch.centerMin - inflate, batch.centerMax + inflate);
			if (!state.camera.frustum.IsAABBOnFrustum(bounds))
				continue;

			const bool alphaBlend = batch.blendMode == ParticleBlendMode::AlphaBlend;
			for (auto& subDraw : subDraws)
			{
				PendingGroup group;
				group.source = frame->particles.data() + batch.firstParticle;
				group.count = batch.particleCount;
				group.mask = static_cast<uint32_t>(renderInfo.mask);
				group.flags = renderInfo.mesh ? PARTICLE_FLAG_MESH : 0u;

				uint32_t materialIndex = 0;
				const bool hasMaterial = subDraw.material && resolveMaterialIndex(*subDraw.material, materialIndex);
				if (hasMaterial)
				{
					group.materialIndex = materialIndex;
					group.flags |= PARTICLE_FLAG_MATERIAL;
				}

				group.group.material = hasMaterial;
				group.group.alphaBlend = alphaBlend;
				group.group.instanceCount = batch.particleCount;
				group.group.indexCount = subDraw.indexCount;
				group.group.firstIndex = subDraw.firstIndex;
				group.group.vertexOffset = subDraw.vertexOffset;
				pending.push_back(group);
			}
		}
	}

	if (pending.empty())
		return;

	// 网格区间 / 有无材质 / 混合模式相同的批次排到一起，实例连续，合并成一次 drawIndexed
	//（材质下标是逐实例的，不同材质可以在同一次绘制里）
	auto drawKey = [](const PendingGroup& g)
		{
			return std::make_tuple(g.group.alphaBlend, g.group.material, g.group.indexCount, g.group.firstIndex, g.group.vertexOffset);
		};
	std::sort(pending.begin(), pending.end(), [&](const PendingGroup& a, const PendingGroup& b) { return drawKey(a) < drawKey(b); });

	uint32_t totalInstances = 0;
	for (auto& group : pending)
	{
		group.group.firstInstance = totalInstances;
		totalInstances += group.count;

		if (!drawGroups.empty())
		{
			DrawGroup& last = drawGroups.back();
			const bool sameDraw = last.alphaBlend == group.group.alphaBlend && last.material == group.group.material
				&& last.indexCount == group.group.indexCount && last.firstIndex == group.group.firstIndex && last.vertexOffset == group.group.vertexOffset
				&& last.firstInstance + last.instanceCount == group.group.firstInstance;
			if (sameDraw)
			{
				last.instanceCount += group.count;
				continue;
			}
		}
		drawGroups.push_back(group.group);
	}

	// 逐实例填 GPU 布局，各组写自己那段，互不重叠
	_instanceScratch.resize(totalInstances);
	std::for_each(std::execution::par, pending.begin(), pending.end(),
		[this](const PendingGroup& group)
		{
			ParticleInstance* dst = _instanceScratch.data() + group.group.firstInstance;
			for (uint32_t i = 0; i < group.count; ++i)
			{
				const ParticleRenderParticle& particle = group.source[i];
				ParticleInstance& instance = dst[i];
				instance.position = particle.position;
				instance.extentX = particle.scale.x;
				instance.extentY = particle.scale.y;
				instance.roll = 0.0f;
				instance.mask = group.mask;
				instance.flags = group.flags;
				instance.color = glm::vec4(particle.color, particle.opacity);
				instance.scale = particle.scale;
				instance.materialIndex = group.materialIndex;
				instance.rotation = glm::vec4(particle.rotation.x, particle.rotation.y, particle.rotation.z, particle.rotation.w);
			}
		});

	auto cmd = VKCONTEXT->GetCommandBuffer();
	// 容量按 1.5 倍增长：粒子数上涨期间不至于每帧都按精确大小重建 buffer。
	// remainData = false：旧内容马上被这一帧整体覆盖，不需要先拷过去（旧 buffer 走 Retire 延迟销毁）
	const uint64_t requiredBytes = uint64_t(totalInstances) * sizeof(ParticleInstance);
	if (requiredBytes > _instanceBuffer->GetSize())
	{
		const uint64_t grownBytes = std::max<uint64_t>(requiredBytes, _instanceBuffer->GetSize() + _instanceBuffer->GetSize() / 2);
		_instanceBuffer->SetSizeAsync(cmd, grownBytes, false);
	}
	_instanceBuffer->WriteDataAsync(cmd, _instanceScratch.data(), requiredBytes);
	cmd->SubmitNowAndWait();
}

void EffectPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto& drawGroups = registry.Get<std::vector<DrawGroup>>("Groups");
	if (drawGroups.empty())
		return;

	auto targetColorBuffer = ctx.GetExternal(0);
	auto targetDepthBuffer = ctx.GetFrameLocal(0);
	if (!targetColorBuffer || !targetDepthBuffer)
		return;

	const bool hasAlphaBlend = std::any_of(drawGroups.begin(), drawGroups.end(), [](const DrawGroup& group) { return group.alphaBlend; });

	auto accumTexture = hasAlphaBlend ? ctx.GetTemp(0) : nullptr;
	auto revealTexture = hasAlphaBlend ? ctx.GetTemp(1) : nullptr;
	if (hasAlphaBlend && (!accumTexture || !revealTexture))
		return;

	auto cmd = cmdCtx.GetCmd();

	targetColorBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	targetDepthBuffer->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	const auto colorView = targetColorBuffer->GetImageView(vk::ImageAspectFlagBits::eColor);
	const auto depthView = targetDepthBuffer->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
	DynamicViewport viewport(state.framebuffer.width, state.framebuffer.height);

	// 粒子几何全部来自 IndirectDrawManager 的全局顶点/索引缓冲
	auto indirectManager = IndirectDrawManager::Instance();
	auto vertexBlock = indirectManager->GetVertexBlock();
	auto indexBlock = indirectManager->GetIndexBlock();

	// ---------------- AlphaBlend 粒子累积进 accum / reveal ----------------
	if (hasAlphaBlend)
	{
		accumTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
		revealTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

		DynamicRenderInfo oitInfo;
		oitInfo
			.AddColorAttachment(accumTexture->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eClear, vk::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f })
			.AddColorAttachment(revealTexture->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eClear, vk::ClearColorValue{ 1.0f, 1.0f, 1.0f, 1.0f })
			.AddDepthStencilAttachment(depthView, vk::AttachmentLoadOp::eLoad)
			.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

		cmd->beginRendering(oitInfo);
		cmd->setDynamicViewport(viewport);
		cmd->bindVertexBuffers(vertexBlock);
		cmd->bindIndexBuffer(indexBlock);

		for (auto& group : drawGroups)
		{
			if (!group.alphaBlend)
				continue;

			GraphicsBindingRecord binding;
			binding.SetStorageBlock(_instanceBuffer, 0);
			binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
			if (group.material)
				binding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), BindlessTextureManager::Instance());

			if (group.material)
				_oitMaterialShader.Bind(cmd, binding);
			else
				_oitColorShader.Bind(cmd, binding);

			cmd->drawIndexed(group.indexCount, group.instanceCount, group.firstIndex, group.vertexOffset, group.firstInstance);
		}

		cmd->endRendering();

		accumTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
		revealTexture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);
	}

	// ---------------- 先画加色粒子，再把 OIT 合成叠上来 ----------------
	DynamicRenderInfo info;
	info.AddColorAttachment(colorView, vk::AttachmentLoadOp::eLoad)
		.AddDepthStencilAttachment(depthView, vk::AttachmentLoadOp::eLoad)
		.SetRenderArea(state.framebuffer.width, state.framebuffer.height);

	cmd->beginRendering(info);
	cmd->setDynamicViewport(viewport);

	if (hasAlphaBlend)
	{
		GraphicsBindingRecord binding;
		binding.SetUniformTexture(accumTexture, vk::ImageAspectFlagBits::eColor, 1);
		binding.SetUniformTexture(revealTexture, vk::ImageAspectFlagBits::eColor, 2);

		_oitCompositeShader.Bind(cmd, binding);
		cmd->draw(3, 1, 0, 0);
	}

	cmd->bindVertexBuffers(vertexBlock);
	cmd->bindIndexBuffer(indexBlock);

	for (auto& group : drawGroups)
	{
		if (group.alphaBlend)
			continue;

		GraphicsBindingRecord binding;
		binding.SetStorageBlock(_instanceBuffer, 0);
		binding.SetCameraUnifromData(state.camera.curUBO, state.camera.prevUBO);
		if (group.material)
			binding.SetBindlessMaterialTexture(indirectManager->GetMaterialSSBO(), BindlessTextureManager::Instance());

		if (group.material)
			_additiveMaterialShader.Bind(cmd, binding);
		else
			_additiveColorShader.Bind(cmd, binding);

		cmd->drawIndexed(group.indexCount, group.instanceCount, group.firstIndex, group.vertexOffset, group.firstInstance);
	}

	cmd->endRendering();

	cmd->SubmitToQueue();
}
