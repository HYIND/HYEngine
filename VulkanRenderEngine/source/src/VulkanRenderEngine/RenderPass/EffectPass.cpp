#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/EffectPass.h"
#include "VulkanRenderEngine/General/RenderHelper.h"
#include "VulkanRenderEngine/General/IndirectDrawManager.h"
#include "VulkanRenderEngine/Base/Particle.h"
#include "VulkanRenderEngine/Base/Effect.h"
#include "VulkanRenderEngine/Base/Material.h"


namespace
{
	std::shared_ptr<Material> GetParticleMaterial(const BaseParticleProperties& properties)
	{
		return properties.renderInfo.HasMaterial() ? properties.renderInfo.material : nullptr;
	}

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

	// OIT 累积缓冲：累加 color*α*w 与 α*w
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

	// OIT 透射率缓冲：dst = dst * (1 - src)（R16F 没有 alpha 通道，只能用颜色的写法）
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

		config.AddColorAttachment(vk::Format::eR16G16B16A16Sfloat, GraphicsPipelineConfig::DefaultColorAttachmentBlend);
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
	return !state.objects.sceneStaticRenderData.effectItems.empty();
}

void EffectPass::FrameBegin(RenderGraph::FrameDataRegistry& registry, RenderState& state)
{
	auto& drawGroups = *registry.Get<std::vector<DrawGroup>>("Groups");
	auto& modelGeometries = *registry.Get<std::unordered_map<const StaticModel*, ModelGeometry>>("ModelGeometries");;

	auto indirectManager = IndirectDrawManager::Instance();

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

		_unitMeshRegistered = (_quadIndexCount > 0 && _sphereIndexCount > 0 && _cubeIndexCount > 0);
	}

	// 按「网格区间 + 有没有材质 + 混合模式」分组：同一组粒子在 SSBO 里连续，一次 drawIndexed 就能画完。
	// 材质下标是逐实例的，所以同一组里可以有多个不同材质的粒子。
	using GroupKey = std::tuple<uint32_t /*indexCount*/, uint32_t /*firstIndex*/, int32_t /*vertexOffset*/, bool /*hasMaterial*/, bool /*alphaBlend*/>;
	std::map<GroupKey, std::vector<ParticleInstance>> groupedInstances;

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

	for (auto& item : state.objects.sceneStaticRenderData.effectItems)
	{
		if (!item)
			continue;
		if (item->effectType != EffectType::Particle)
			continue;	// 激光束等其它特效暂不处理

		auto properties = std::static_pointer_cast<BaseParticleProperties>(item);
		if (!properties)
			continue;

		const auto& renderInfo = properties->renderInfo;
		const bool alphaBlend = properties->blendMode == ParticleBlendMode::AlphaBlend;

		ParticleInstance instance;
		instance.position = properties->position;
		instance.extentX = properties->scale.x;
		instance.extentY = properties->scale.y;
		instance.roll = 0.0f;
		instance.mask = static_cast<uint32_t>(renderInfo.mask);
		instance.flags = 0u;
		instance.color = glm::vec4(properties->baseColor, properties->opacity);
		instance.scale = properties->scale;
		instance.materialIndex = 0xffffffffu;
		instance.rotation = glm::vec4(properties->rotation.x, properties->rotation.y, properties->rotation.z, properties->rotation.w);

		if (renderInfo.mesh)
		{
			switch (renderInfo.meshType)
			{
			case ParticleMeshType::Custom:
			{
				if (!renderInfo.model)
					continue;

				const StaticModel* modelKey = renderInfo.model.get();

				// 几何区间只在第一次用到这个模型（或它某个子网格的版本变了）时取一次
				uint32_t modelVersion = 0;
				for (auto& info : renderInfo.model->GetMeshInfos())
				{
					if (info.mesh)
						modelVersion += info.mesh->GetVerticesIndicesVsrsion();
				}

				auto cachedModel = modelGeometries.find(modelKey);
				if (cachedModel == modelGeometries.end() || cachedModel->second.version != modelVersion)
				{
					ModelGeometry geometry;
					geometry.version = modelVersion;

					auto& meshInfos = renderInfo.model->GetMeshInfos();
					for (auto& info : meshInfos)
					{
						if (!info.mesh)
							continue;

						IndirectDrawMeta meta;
						if (!indirectManager->GetIndirectDrawMeta(*info.mesh, meta) || meta.indexCount == 0)
							continue;

						ModelSubMeshGeometry subMesh;
						subMesh.indexCount = meta.indexCount;
						subMesh.firstIndex = meta.firstIndex;
						subMesh.vertexOffset = meta.vertexOffset;
						subMesh.material = info.material;
						geometry.subMeshes.push_back(std::move(subMesh));
					}

					cachedModel = modelGeometries.insert_or_assign(modelKey, std::move(geometry)).first;
				}

				for (auto& subMesh : cachedModel->second.subMeshes)
				{
					ParticleInstance meshInstance = instance;
					meshInstance.flags |= PARTICLE_FLAG_MESH;

					bool hasMaterial = false;
					uint32_t materialIndex = 0;
					if (resolveMaterialIndex(subMesh.material, materialIndex))
					{
						meshInstance.materialIndex = materialIndex;
						meshInstance.flags |= PARTICLE_FLAG_MATERIAL;
						hasMaterial = true;
					}

					groupedInstances[GroupKey{ subMesh.indexCount, subMesh.firstIndex, subMesh.vertexOffset, hasMaterial, alphaBlend }].push_back(meshInstance);
				}

				continue;
			}
			case ParticleMeshType::Cube:
			{
				if (_cubeIndexCount == 0)
					continue;

				instance.flags |= PARTICLE_FLAG_MESH;

				bool hasMaterial = false;
				if (std::shared_ptr<Material> material = GetParticleMaterial(*properties))
				{
					uint32_t materialIndex = 0;
					if (resolveMaterialIndex(material, materialIndex))
					{
						instance.materialIndex = materialIndex;
						instance.flags |= PARTICLE_FLAG_MATERIAL;
						hasMaterial = true;
					}
				}

				groupedInstances[GroupKey{ _cubeIndexCount, _cubeFirstIndex, _cubeVertexOffset, hasMaterial, alphaBlend }].push_back(instance);
				break;
			}

			case ParticleMeshType::Sphere:
			{
				if (_sphereIndexCount == 0)
					continue;

				instance.flags |= PARTICLE_FLAG_MESH;

				bool hasMaterial = false;
				if (std::shared_ptr<Material> material = GetParticleMaterial(*properties))
				{
					uint32_t materialIndex = 0;
					if (resolveMaterialIndex(material, materialIndex))
					{
						instance.materialIndex = materialIndex;
						instance.flags |= PARTICLE_FLAG_MATERIAL;
						hasMaterial = true;
					}
				}

				groupedInstances[GroupKey{ _sphereIndexCount, _sphereFirstIndex, _sphereVertexOffset, hasMaterial, alphaBlend }].push_back(instance);
				break;
			}

			default:
				continue;
			}

			continue;
		}
		else
		{
			if (_quadIndexCount == 0)
				continue;

			bool hasMaterial = false;
			if (std::shared_ptr<Material> material = GetParticleMaterial(*properties))
			{
				uint32_t materialIndex = 0;
				if (resolveMaterialIndex(material, materialIndex))
				{
					instance.materialIndex = materialIndex;
					instance.flags |= PARTICLE_FLAG_MATERIAL;
					hasMaterial = true;
				}
			}

			groupedInstances[GroupKey{ _quadIndexCount, _quadFirstIndex, _quadVertexOffset, hasMaterial, alphaBlend }].push_back(instance);
		}

	}

	if (groupedInstances.empty())
		return;

	std::vector<ParticleInstance> instances;
	instances.reserve(1024);
	for (auto& [key, group] : groupedInstances)
		instances.insert(instances.end(), group.begin(), group.end());

	uint32_t firstInstance = 0;
	for (auto& [key, group] : groupedInstances)
	{
		const auto [indexCount, firstIndex, vertexOffset, hasMaterial, alphaBlend] = key;

		DrawGroup drawGroup;
		drawGroup.material = hasMaterial;
		drawGroup.alphaBlend = alphaBlend;
		drawGroup.firstInstance = firstInstance;
		drawGroup.instanceCount = static_cast<uint32_t>(group.size());
		drawGroup.indexCount = indexCount;
		drawGroup.firstIndex = firstIndex;
		drawGroup.vertexOffset = vertexOffset;
		drawGroups.push_back(drawGroup);
		firstInstance += static_cast<uint32_t>(group.size());
	}

	auto cmd = VKCONTEXT->GetCommandBuffer();
	_instanceBuffer->WriteDataAsync(cmd, instances.data(), instances.size() * sizeof(ParticleInstance));
	cmd->SubmitNowAndWait();
}

void EffectPass::Execute(RenderGraph::PassFrameCmdContext& cmdCtx, RenderGraph::FrameDataRegistry& registry, const RenderGraph::PassFrameContext& ctx, RenderState& state)
{
	auto& drawGroups = *registry.Get<std::vector<DrawGroup>>("Groups");
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
