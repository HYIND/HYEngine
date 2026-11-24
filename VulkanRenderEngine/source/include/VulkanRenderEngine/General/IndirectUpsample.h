#pragma once

#include "VulkanRenderEngine/Base/ComputePipeline.h"

// 间接光（RTGI / RTR）的联合双边上采样 + 材质重新调制：低分辨率降噪结果 → 全分辨率输出。
// 见 shader/Filter/IndirectUpsample.comp 与 shader/Filter/IndirectDemodulation.comp。
class IndirectUpsample
{
public:
	enum class Remodulation : uint32_t
	{
		None = 0,			// 输入没有去调制（软件光追）
		PathTracing = 1,	// 乘 漫反射 + 镜面反射率（RTGI）
		Specular = 2,		// 乘 镜面反射率（RTR / SSR）
		DiffuseSpecular = 3,	// 拆成两路的路径追踪：lowResImage × 漫反射率 + lowResSpecularImage × 镜面反射率（RTGI / SSGI）
		DiffuseSpecularAdd = 4,	// 同上但不调制，两路直接相加（未去调制的软件光追 RTGI）
	};

public:
	IndirectUpsample(const std::string& shaderPath = "shader/Filter/IndirectUpsample.comp");

	void Execute(
		const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd,
		const std::shared_ptr<Texture2D>& lowResImage,
		const std::shared_ptr<Texture2D>& positionMap,
		const std::shared_ptr<Texture2D>& normalMap,
		const std::shared_ptr<Texture2D>& depthMap,
		const std::shared_ptr<Texture2D>& albedoMap,
		const std::shared_ptr<Texture2D>& metallicRoughnessMap,
		const std::shared_ptr<Texture2D>& outputImage,
		Remodulation mode,
		std::shared_ptr<UniformBlock>& curCameraUBO,
		std::shared_ptr<UniformBlock>& prevCameraUBO,
		const std::shared_ptr<Texture2D>& lowResSpecularImage = nullptr	// 只在 DiffuseSpecular / DiffuseSpecularAdd 时读
	);

private:
	ComputePipeline _pipeline;
};
