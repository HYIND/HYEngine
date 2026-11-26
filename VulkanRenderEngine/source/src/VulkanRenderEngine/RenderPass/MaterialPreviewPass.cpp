#include "vkstdafx.h"
#include "VulkanRenderEngine/RenderPass/MaterialPreviewPass.h"
#include "VulkanRenderEngine/Base/GraphicsPipeline.h"
#include "VulkanRenderEngine/Base/DynamicBlock.h"
#include "VulkanRenderEngine/Base/StaticMesh.h"
#include "VulkanRenderEngine/Base/TextureCube.h"
#include "VulkanRenderEngine/General/HDRIEnvironment.h"
#include "VulkanRenderEngine/VKContext.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
	constexpr const char* kVertexShaderPath = "shader/Preview/material_preview.vs";
	constexpr const char* kFragmentShaderPath = "shader/Preview/material_preview.fs";
	constexpr const char* kSkyVertexShaderPath = "shader/Preview/material_preview_sky.vs";
	constexpr const char* kSkyFragmentShaderPath = "shader/Preview/material_preview_sky.fs";

	constexpr vk::Format kColorFormat = vk::Format::eR8G8B8A8Unorm;
	constexpr vk::Format kDepthFormat = vk::Format::eD24UnormS8Uint;
	constexpr uint32_t kMaxInternalSize = 2048;

	// 背景：alpha = 0，颜色取编辑器缩略图格子的底色，超采样缩小后物体边缘不会出现黑边
	constexpr float kClearGray = 40.0f / 255.0f;

	constexpr uint32_t kSphereRings = 48;
	constexpr uint32_t kSphereSegments = 96;
	constexpr float kCubeHalfExtent = 0.65f;		// 转到对角线朝向相机时也不出画面
	constexpr float kPlaneHalfExtent = 0.95f;

	constexpr float PI = 3.14159265358979f;

	// 和 material_preview.vs / .fs 的 push_constant 块保持一致；128 字节是规范保证的最小上限
	struct PreviewPushConstants
	{
		glm::mat4 viewProj;
		glm::vec4 cameraPos;		// xyz：相机位置
		glm::vec4 params;			// x：曝光
		glm::vec4 keyLightDir;		// xyz：主光方向（指向光源），同时是环境里柔光箱的方向
		glm::vec4 rimLightDir;		// xyz：轮廓光方向（指向光源）
	};
	static_assert(sizeof(PreviewPushConstants) == 128, "PreviewPushConstants 布局要和着色器一致，且不能超过 128 字节");
	// HDRI 模式下各字段的额外含义（摄影棚模式不用）：
	//   params.y：环境绕 Y 轴旋转（弧度），params.z：环境亮度，params.w：> 0.5 时叠加主光 + 轮廓光
	//   keyLightDir.w：背景 mip 级（Blend 材质和背后的 HDRI 混合时用）

	// 和 material_preview_sky.vs / .fs 一致
	struct SkyPushConstants
	{
		glm::mat4 invViewProjRotation;	// (投影 × 只有旋转的视图矩阵) 的逆：NDC → 世界方向
		glm::vec4 params;				// x：曝光，y：环境旋转（弧度），z：环境亮度，w：背景 mip 级
	};
	static_assert(sizeof(SkyPushConstants) == 80);

	// 方向约定：yaw 绕 Y 轴、0 = +Z 一侧；pitch 为高度角
	glm::vec3 DirectionFromAngles(float yawDeg, float pitchDeg)
	{
		const float yaw = glm::radians(yawDeg);
		const float pitch = glm::radians(std::clamp(pitchDeg, -89.0f, 89.0f));
		return glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
	}

	// ---------------- 网格 ----------------
	// UV 约定统一：从外面正对着看，u 向右、v 向下（v = 0 在上边），贴图不镜像；
	// 切线 = dP/du、副切线 = dP/dv（和 Assimp 按 UV 求出来的方向一致）

	// 单位 UV 球。两极退化处由着色器 normalize 兜底
	void BuildSphere(std::vector<Vertex>& vertices, std::vector<unsigned int>& indices)
	{
		vertices.reserve((kSphereRings + 1) * (kSphereSegments + 1));
		indices.reserve(kSphereRings * kSphereSegments * 6);

		for (uint32_t r = 0; r <= kSphereRings; ++r)
		{
			const float v = static_cast<float>(r) / kSphereRings;
			const float theta = v * PI;
			const float sinT = std::sin(theta);
			const float cosT = std::cos(theta);

			for (uint32_t s = 0; s <= kSphereSegments; ++s)
			{
				const float u = static_cast<float>(s) / kSphereSegments;
				const float phi = u * 2.0f * PI;
				const float sinP = std::sin(phi);
				const float cosP = std::cos(phi);

				Vertex vertex{};
				vertex.Position = glm::vec3(-sinT * cosP, cosT, sinT * sinP);
				vertex.Normal = vertex.Position;
				vertex.TexCoords = glm::vec2(u, v);
				vertex.Tangent = glm::vec3(sinP, 0.0f, cosP);
				vertex.Bitangent = glm::vec3(-cosT * cosP, -sinT, cosT * sinP);
				vertices.push_back(vertex);
			}
		}

		const uint32_t stride = kSphereSegments + 1;
		for (uint32_t r = 0; r < kSphereRings; ++r)
		{
			for (uint32_t s = 0; s < kSphereSegments; ++s)
			{
				const uint32_t a = r * stride + s;
				const uint32_t b = a + stride;
				indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}
	}

	// 一个面：中心 normal * halfExtent，u 沿 uAxis（向右）、v 沿 vAxis（向下），要求 cross(vAxis, uAxis) == normal
	void AddFace(std::vector<Vertex>& vertices, std::vector<unsigned int>& indices,
		const glm::vec3& normal, const glm::vec3& uAxis, const glm::vec3& vAxis, float halfExtent, float depth)
	{
		const uint32_t base = static_cast<uint32_t>(vertices.size());
		for (uint32_t j = 0; j < 2; ++j)
		{
			for (uint32_t i = 0; i < 2; ++i)
			{
				const float u = static_cast<float>(i);
				const float v = static_cast<float>(j);
				Vertex vertex{};
				vertex.Position = normal * depth + uAxis * ((u * 2.0f - 1.0f) * halfExtent) + vAxis * ((v * 2.0f - 1.0f) * halfExtent);
				vertex.Normal = normal;
				vertex.TexCoords = glm::vec2(u, v);
				vertex.Tangent = uAxis;
				vertex.Bitangent = vAxis;
				vertices.push_back(vertex);
			}
		}
		// 0 1 / 2 3（不剔除，绕序无所谓）
		indices.insert(indices.end(), { base + 0, base + 2, base + 1, base + 1, base + 2, base + 3 });
	}

	void BuildCube(std::vector<Vertex>& vertices, std::vector<unsigned int>& indices)
	{
		const float h = kCubeHalfExtent;
		AddFace(vertices, indices, { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 }, h, h);		// +Z
		AddFace(vertices, indices, { 0, 0, -1 }, { -1, 0, 0 }, { 0, -1, 0 }, h, h);	// -Z
		AddFace(vertices, indices, { 1, 0, 0 }, { 0, 0, -1 }, { 0, -1, 0 }, h, h);		// +X
		AddFace(vertices, indices, { -1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 }, h, h);		// -X
		AddFace(vertices, indices, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, h, h);		// +Y（顶面）
		AddFace(vertices, indices, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, h, h);		// -Y（底面）
	}

	void BuildPlane(std::vector<Vertex>& vertices, std::vector<unsigned int>& indices)
	{
		AddFace(vertices, indices, { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 }, kPlaneHalfExtent, 0.0f);
	}

	MaterialData MakeMaterialData(const MaterialPreviewInput& input, std::vector<std::shared_ptr<Texture2D>>& outTextures)
	{
		const MaterialProperties& p = input.properties;

		MaterialData data{};
		data.albedo = p.albedo;
		data.metallic = p.metallic;
		data.roughness = p.roughness;
		data.ambientOcclusion = p.ambientOcclusion;
		data.opacity = p.opacity;
		data.IOR = p.IOR;
		data.emissionColor = p.emissionColor;
		data.emissionStrength = p.emissionStrength;
		data.alphamode = p.alphamode;
		data.maskthreshold = p.maskthreshold;
		data.twosided = p.twosided ? 1 : 0;

		// 贴图数组只放用到的贴图，紧凑排列；下标就是在 outTextures 里的位置
		auto assign = [&](TextureType type, uint32_t& index, uint32_t& count)
			{
				index = 0;
				count = 0;
				auto it = input.textures.find(type);
				if (it == input.textures.end())
					return;
				const auto& texture = it->second;
				if (!texture || texture->IsEmpty() || !texture->GetSampler())
					return;
				// 同一张图放在多个槽里只占一个位置
				auto existing = std::find(outTextures.begin(), outTextures.end(), texture);
				index = static_cast<uint32_t>(existing - outTextures.begin());
				if (existing == outTextures.end())
					outTextures.push_back(texture);
				count = 1;
			};

		assign(TextureType::Albedo, data.texture_albedo, data.texture_albedo_count);
		assign(TextureType::Metallic, data.texture_metallic, data.texture_metallic_count);
		assign(TextureType::Roughness, data.texture_roughness, data.texture_roughness_count);
		assign(TextureType::Normal, data.texture_normal, data.texture_normal_count);
		assign(TextureType::AO, data.texture_ao, data.texture_ao_count);
		assign(TextureType::Emissive, data.texture_emissive, data.texture_emissive_count);
		assign(TextureType::MetallicRoughness, data.texture_metallicroughness, data.texture_metallicroughness_count);
		assign(TextureType::Height, data.texture_height, data.texture_height_count);
		assign(TextureType::Opacity, data.texture_opacity, data.texture_opacity_count);
		return data;
	}
}

MaterialPreviewPass& MaterialPreviewPass::Instance()
{
	static MaterialPreviewPass instance;
	return instance;
}

std::shared_ptr<MaterialPreviewPass::Resources> MaterialPreviewPass::CreateResources()
{
	auto resources = std::make_shared<Resources>();

	// ---------------- 管线：单附件前向着色，只用 set 2（材质 SSBO + 贴图数组）和 push constant ----------------
	//   ibl = true：同一个着色器定义 PREVIEW_IBL，多 set 0 的四张环境贴图
	const auto createMeshPipeline = [](bool ibl) -> std::shared_ptr<GraphicsPipeline>
		{
			GraphicsPipelineConfig config;
			config.vertexPath = kVertexShaderPath;
			config.fragmentPath = kFragmentShaderPath;
			if (ibl)
				config.AddDefineMacro("PREVIEW_IBL");

			config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
			config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());

			// 不做帧缓冲混合：摄影棚模式 Blend 材质把透明度直接写进 alpha，交给 ImGui 叠在棋盘格上；
			// HDRI 模式在着色器里自己和背后的 HDRI 混合，输出不透明
			config.AddColorAttachment(kColorFormat, GraphicsPipelineConfig::DefaultColorAttachmentBlend);
			config.SetDepthStencilAttachmentFormat(kDepthFormat);
			config.SetDepthTest(true, true);
			// 不剔除：球和立方体是闭合凸体，深度测试只留最前面的表面；平面要双面看；也不用关心投影翻转后的绕序
			config.SetCullMode(vk::CullModeFlagBits::eNone);

			if (ibl)
			{
				config
					.AddUnifromTexture(0)		// 漫反射辐照度 cube
					.AddUnifromTexture(1)		// 镜面预过滤 cube
					.AddUnifromTexture(2)		// BRDF LUT
					.AddUnifromTexture(3);		// 背景 cube
			}
			config.AddBindlessMaterialTextureBinding();		// set 2
			config.AddPushConstant(sizeof(PreviewPushConstants));

			if (!config.Validate())
				return nullptr;
			auto pipeline = std::make_shared<GraphicsPipeline>();
			if (!pipeline->Create(config) || !pipeline->IsValid())
				return nullptr;
			return pipeline;
		};

	resources->pipeline = createMeshPipeline(false);
	if (!resources->pipeline)
		return nullptr;

	// HDRI 用的两条管线失败不影响摄影棚模式（缩略图照常），只是 HDRI 预览退回摄影棚
	resources->iblPipeline = createMeshPipeline(true);
	{
		GraphicsPipelineConfig config;
		config.vertexPath = kSkyVertexShaderPath;
		config.fragmentPath = kSkyFragmentShaderPath;
		// 全屏三角形也用 Vertex 的顶点格式（只用位置），和网格共用顶点缓冲的写法
		config.AddVertexInputAttributeDescription(StaticMesh::GetVertexInputAttributeDescription());
		config.AddVertexInputBindingDescription(StaticMesh::GetVertexInputBindingDescription());
		config.AddColorAttachment(kColorFormat, GraphicsPipelineConfig::DefaultColorAttachmentBlend);
		config.SetDepthStencilAttachmentFormat(kDepthFormat);
		config.SetDepthTest(false, false);
		config.SetCullMode(vk::CullModeFlagBits::eNone);
		config.AddUnifromTexture(0);		// 背景 cube
		config.AddPushConstant(sizeof(SkyPushConstants));

		auto pipeline = std::make_shared<GraphicsPipeline>();
		if (config.Validate() && pipeline->Create(config) && pipeline->IsValid())
			resources->skyPipeline = std::move(pipeline);
	}
	if (!resources->iblPipeline || !resources->skyPipeline)
	{
		resources->iblPipeline.reset();
		resources->skyPipeline.reset();
		std::cerr << "[ MaterialPreviewPass ] HDRI 预览管线创建失败，HDRI 模式退回摄影棚灯光\n";
	}

	// ---------------- 网格：自己的小顶点 / 索引缓冲，不进 IndirectDrawManager ----------------
	using Builder = void(*)(std::vector<Vertex>&, std::vector<unsigned int>&);
	const Builder builders[] = { &BuildSphere, &BuildCube, &BuildPlane };
	static_assert(std::size(builders) == static_cast<size_t>(MaterialPreviewShape::Count));
	for (size_t i = 0; i < std::size(builders); ++i)
	{
		std::vector<Vertex> vertices;
		std::vector<unsigned int> indices;
		builders[i](vertices, indices);

		auto& mesh = resources->meshes[i];
		const uint64_t vertexBytes = vertices.size() * sizeof(Vertex);
		const uint64_t indexBytes = indices.size() * sizeof(unsigned int);
		mesh.vertices = std::make_shared<VertexBufferBlock>(vertexBytes);
		mesh.indices = std::make_shared<IndexBufferBlock>(indexBytes);
		mesh.vertices->WriteData(vertices.data(), vertexBytes);
		mesh.indices->WriteData(indices.data(), indexBytes);
		mesh.indexCount = static_cast<uint32_t>(indices.size());
	}

	// ---------------- 全屏三角形（HDRI 背景）：位置直接是 NDC ----------------
	{
		std::vector<Vertex> vertices(3);
		vertices[0].Position = glm::vec3(-1.0f, -1.0f, 0.0f);
		vertices[1].Position = glm::vec3(3.0f, -1.0f, 0.0f);
		vertices[2].Position = glm::vec3(-1.0f, 3.0f, 0.0f);
		const unsigned int indices[3] = { 0, 1, 2 };

		auto& mesh = resources->fullscreenTriangle;
		mesh.vertices = std::make_shared<VertexBufferBlock>(sizeof(Vertex) * 3);
		mesh.indices = std::make_shared<IndexBufferBlock>(sizeof(indices));
		mesh.vertices->WriteData(vertices.data(), sizeof(Vertex) * 3);
		mesh.indices->WriteData(indices, sizeof(indices));
		mesh.indexCount = 3;
	}

	// ---------------- 占位图：贴图数组至少要有一项 ----------------
	{
		TextureConfig config;
		config.minFilter = vk::Filter::eLinear;
		config.magFilter = vk::Filter::eLinear;
		resources->placeholder = std::make_shared<Texture2D>(1, 1, vk::Format::eR8G8B8A8Unorm, config, 1, true);
		uint8_t white[4] = { 255, 255, 255, 255 };
		resources->placeholder->UpdateTextureData(white);
		// 多个线程会同时绑它：固定成只读采样布局，之后谁都不用再转换
		resources->placeholder->MakeSampledImmutable();
	}

	return resources;
}

std::shared_ptr<const MaterialPreviewPass::Resources> MaterialPreviewPass::Acquire()
{
	std::lock_guard lock(_mutex);
	if (_released || _initFailed)
		return nullptr;
	if (_resources)
		return _resources;

	if (!_shutdownRegistered)
	{
		VKCONTEXT->AddShutdownCallback([this] { Release(); });
		_shutdownRegistered = true;
	}

	auto resources = CreateResources();
	if (!resources)
	{
		_initFailed = true;
		std::cerr << "[ MaterialPreviewPass ] 初始化失败（管线或着色器创建不成功），材质预览不可用\n";
		return nullptr;
	}
	_resources = std::move(resources);
	return _resources;
}

void MaterialPreviewPass::Release()
{
	std::lock_guard lock(_mutex);
	_resources.reset();
	_released = true;
}

bool MaterialPreviewPass::Render(const MaterialPreviewInput& input, const MaterialPreviewView& view, const std::shared_ptr<Texture2D>& output)
{
	if (!output || output->IsEmpty())
		return false;

	// 持有一份：Render 期间即使别的线程调了 Release，管线和网格也活到这次渲染结束
	auto resources = Acquire();
	if (!resources)
		return false;

	const size_t shapeIndex = std::min(static_cast<size_t>(view.shape), resources->meshes.size() - 1);
	const MeshBuffers& mesh = resources->meshes[shapeIndex];
	if (!mesh.vertices || !mesh.indices || mesh.indexCount == 0)
		return false;

	const uint32_t outWidth = output->GetWidth();
	const uint32_t outHeight = output->GetHeight();
	if (outWidth == 0 || outHeight == 0)
		return false;
	uint32_t supersample = std::clamp(view.supersample, 1u, 4u);
	while (supersample > 1 && std::max(outWidth, outHeight) * supersample > kMaxInternalSize)
		--supersample;
	const uint32_t width = outWidth * supersample;
	const uint32_t height = outHeight * supersample;

	// ---------------- 材质数据（只有一项）和贴图数组 ----------------
	std::vector<std::shared_ptr<Texture2D>> textures;
	MaterialData materialData = MakeMaterialData(input, textures);
	if (textures.empty())
		textures.push_back(resources->placeholder);

	// ---------------- 相机和灯光 ----------------
	const glm::vec3 eye = std::max(view.distance, 1.2f) * DirectionFromAngles(view.yaw, view.pitch);
	const glm::mat4 viewMatrix = glm::lookAtRH(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(std::clamp(view.fovY, 5.0f, 120.0f)),
		static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.0f);
	projection[1][1] *= -1.0f;		// Vulkan 裁剪空间 Y 朝下

	const float lightYaw = view.lightYaw + (view.lightFollowCamera ? view.yaw : 0.0f);
	const glm::vec3 keyLight = DirectionFromAngles(lightYaw, view.lightPitch);
	// 轮廓光：主光水平方向的对侧、偏上方；主光几乎竖直时退回到固定方向
	glm::vec2 rimHorizontal(-keyLight.x, -keyLight.z);
	rimHorizontal = glm::length(rimHorizontal) > 1e-3f ? glm::normalize(rimHorizontal) : glm::vec2(0.75f, -0.66f);
	const glm::vec3 rimLight = glm::normalize(glm::vec3(rimHorizontal.x, 0.5f, rimHorizontal.y));

	PreviewPushConstants push{};
	push.viewProj = projection * viewMatrix;
	push.cameraPos = glm::vec4(eye, 1.0f);
	push.params = glm::vec4(view.exposure, 0.0f, 0.0f, 0.0f);
	push.keyLightDir = glm::vec4(keyLight, 0.0f);
	push.rimLightDir = glm::vec4(rimLight, 0.0f);

	// ---------------- HDRI 环境（HDRI 管线没建成功时退回摄影棚） ----------------
	const HDRIEnvironment* environment = view.environment.get();
	const bool useEnvironment = environment && resources->iblPipeline && resources->skyPipeline
		&& environment->GetBackground() && environment->GetDiffuse() && environment->GetPrefilter() && environment->GetBRDFLUT();
	SkyPushConstants skyPush{};
	if (useEnvironment)
	{
		const float environmentYaw = glm::radians(view.environmentYaw);
		const float environmentIntensity = std::max(view.environmentIntensity, 0.0f);
		const uint32_t backgroundLevels = std::max(environment->GetBackground()->GetMaxLevel(), 1u);
		const float backgroundLod = std::clamp(view.backgroundBlur, 0.0f, 1.0f) * static_cast<float>(backgroundLevels - 1);

		push.params = glm::vec4(view.exposure, environmentYaw, environmentIntensity, view.environmentDirectLights ? 1.0f : 0.0f);
		push.keyLightDir.w = backgroundLod;

		// 背景只看方向：视图矩阵去掉平移
		const glm::mat4 viewRotation = glm::mat4(glm::mat3(viewMatrix));
		skyPush.invViewProjRotation = glm::inverse(projection * viewRotation);
		skyPush.params = glm::vec4(view.exposure, environmentYaw, environmentIntensity, backgroundLod);
	}

	// ---------------- 这次渲染自己的资源 ----------------
	auto colorTarget = std::make_shared<Texture2D>(width, height, kColorFormat, TextureConfig{}, 1, false);
	auto depthTarget = std::make_shared<Texture2D>(width, height, kDepthFormat, TextureConfig{}, 1, false);
	if (colorTarget->IsEmpty() || depthTarget->IsEmpty())
		return false;
	auto materialBlock = std::make_shared<StorageBlock>(sizeof(MaterialData));

	auto cmd = VKCONTEXT->GetCommandBuffer();
	if (!cmd)
		return false;
	cmd->Begin();

	materialBlock->WriteDataAsync(cmd, &materialData, sizeof(MaterialData));
	materialBlock->Barrier(cmd, BufferUsage::TransferWrite, BufferUsage::StorageRead);

	// 屏障不能录在动态渲染里面：贴图先切到采样布局（只读资产贴图本来就是，跳过）
	for (const auto& texture : textures)
	{
		if (!texture->IsSampledImmutable())
			texture->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Sample);
	}
	if (useEnvironment)
	{
		// HDRIEnvironment 生成完已经是只读布局；万一没切成功，在这里（动态渲染外面）转一次
		for (const auto& cube : { environment->GetDiffuse(), environment->GetPrefilter(), environment->GetBackground() })
		{
			if (!cube->IsSampledImmutable())
				cube->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Sample);
		}
		if (!environment->GetBRDFLUT()->IsSampledImmutable())
			environment->GetBRDFLUT()->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Sample);
	}
	colorTarget->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);
	depthTarget->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Write);

	DynamicRenderInfo renderInfo;
	renderInfo
		.AddColorAttachment(colorTarget->GetImageView(vk::ImageAspectFlagBits::eColor), vk::AttachmentLoadOp::eClear, vk::ClearColorValue{ kClearGray, kClearGray, kClearGray, 0.0f })
		.AddDepthStencilAttachment(depthTarget->GetImageView(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil), vk::AttachmentLoadOp::eClear)
		.SetRenderArea(width, height);

	cmd->beginRendering(renderInfo);
	cmd->setDynamicViewport(width, height);

	// HDRI 背景：先画全屏三角形（不写深度），物体画在上面
	if (useEnvironment)
	{
		GraphicsBindingRecord skyBinding;
		skyBinding.SetUniformTextureCube(environment->GetBackground(), vk::ImageAspectFlagBits::eColor, 0);
		resources->skyPipeline->Bind(cmd, skyBinding);
		resources->skyPipeline->SetPushConstants(cmd, &skyPush, sizeof(skyPush));
		cmd->bindVertexBuffers(resources->fullscreenTriangle.vertices);
		cmd->bindIndexBuffer(resources->fullscreenTriangle.indices);
		cmd->drawIndexed(resources->fullscreenTriangle.indexCount, 1, 0, 0, 0);
	}

	const auto& meshPipeline = useEnvironment ? resources->iblPipeline : resources->pipeline;
	GraphicsBindingRecord binding;
	binding.SetBindlessMaterialTexture(materialBlock, std::make_shared<BaseTextureArrayProvider>(textures));
	if (useEnvironment)
	{
		binding.SetUniformTextureCube(environment->GetDiffuse(), vk::ImageAspectFlagBits::eColor, 0);
		binding.SetUniformTextureCube(environment->GetPrefilter(), vk::ImageAspectFlagBits::eColor, 1);
		binding.SetUniformTexture(environment->GetBRDFLUT(), vk::ImageAspectFlagBits::eColor, 2);
		binding.SetUniformTextureCube(environment->GetBackground(), vk::ImageAspectFlagBits::eColor, 3);
	}
	meshPipeline->Bind(cmd, binding);
	meshPipeline->SetPushConstants(cmd, &push, sizeof(push));

	cmd->bindVertexBuffers(mesh.vertices);
	cmd->bindIndexBuffer(mesh.indices);
	cmd->drawIndexed(mesh.indexCount, 1, 0, 0, 0);

	cmd->endRendering();

	// 超采样结果线性缩小到输出，输出切到采样布局（ImGui 直接画，不用再单独提交布局转换）
	Texture2D::BlitImageAsync(cmd, colorTarget, output, vk::Filter::eLinear);
	output->TransitionLayout(cmd, nullptr, ImageLayout::BindStage::Graphics, ImageLayout::BindUsage::Read);

	cmd->End();
	cmd->SubmitNowAndWait();
	return true;
}
