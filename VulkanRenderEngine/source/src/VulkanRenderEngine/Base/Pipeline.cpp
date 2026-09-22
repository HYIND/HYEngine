#include "vkstdafx.h"
#include "shaderc/shaderc.hpp"
#include "VulkanRenderEngine/Base/Pipeline.h"
#include "VulkanRenderEngine/VKContext.h"
#include <map>
#include <regex>
#include <filesystem>
#include <fstream>

static std::string ReadFromFile(const std::string& path)
{
	std::ifstream file;
	file.exceptions(std::ifstream::badbit);// 异常机制处理：保证ifstream对象可以抛出异常：

	try
	{
		file.open(path);
		if (!file) {
			std::cerr << "文件打开失败" << ",path=" << path << std::endl;
			return std::string();
		}

		std::stringstream stream;
		stream << file.rdbuf();
		file.close();

		return stream.str();
	}
	catch (std::ifstream::failure e) {
		std::cout << "ERROR::FILE_NOT_SUCCESSFULLY_READ" << std::endl;
	}

	return std::string();
}
static std::string RemoveComments(std::string code)
{
	// 1. 删除多行注释
	std::regex blockComment(R"(/\*.*?\*/)");
	code = std::regex_replace(code, blockComment, "");

	// 2. 删除单行注释
	std::regex lineComment(R"(//[^\n]*)");
	code = std::regex_replace(code, lineComment, "");

	//std::cout << code << '\n';
	return code;
}
static std::string preProcessInclude(std::string shaderSource)
{
	struct matchData
	{
		size_t pos;
		size_t length;
		std::filesystem::path filepath;
	};

	while (true)
	{
		bool IsRepleaceAnyStr = false;
		std::vector<matchData> matches;  // 存储位置和文件名

		// 查找 #include
		std::regex includePattern(R"(#include\s*[<\"]([^>\"]*)[>\"])");
		std::sregex_iterator it(shaderSource.begin(), shaderSource.end(), includePattern);
		std::sregex_iterator end;

		// 先收集所有匹配的位置
		for (; it != end; ++it)
		{
			std::smatch match = *it;
			std::string includeStr = match.str(0);

			size_t pos = match.position(0);
			size_t length = includeStr.length();
			std::string filepath = match.str(1);

			matches.push_back({ pos,length,filepath });
		}

		for (auto it = matches.rbegin(); it != matches.rend(); ++it)
		{
			auto& matchdata = *it;
			for (auto& path : { matchdata.filepath.string(), matchdata.filepath.filename().string() })
			{
				std::string code = ReadFromFile(path);
				if (!code.empty())
				{
					shaderSource.replace(matchdata.pos, matchdata.length, code);
					IsRepleaceAnyStr = true;
					break;
				}
			}
		}

		if (!IsRepleaceAnyStr)
			break;
	}

	//std::cout << shaderSource << '\n';
	return shaderSource;
}
static std::string insertDefinesAfterVersionAdvanced(const std::string& shaderSource, const std::map<std::string, std::string>& defines) {
	if (defines.empty()) {
		return shaderSource;
	}

	// 查找 #version，忽略前面的注释和空行
	std::regex versionPattern(R"(#version\s+[0-9]+\s*(?:es|core|compatibility)?)");

	std::string definestr;
	for (auto& [name, value] : defines)
		definestr += std::format("#define {} {}\n", name, value);

	std::smatch match;
	if (std::regex_search(shaderSource, match, versionPattern))
	{
		std::string versionLine = match.str(0);
		size_t versionStart = match.position(0);
		size_t versionEnd = versionStart + versionLine.length();

		std::string result = shaderSource;
		result.insert(versionEnd, "\n" + definestr);// 在 #version 之后插入
		return result;
	}

	// 没找到 #version，在开头插入
	return definestr + "\n" + shaderSource;
}

shaderc::SpvCompilationResult CompileSourceCodeToSPIRV(const std::string& sourceCode, ShaderType type, const std::string& path)
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

	shaderc_shader_kind shaderKind;

	if (type == ShaderType::Vertex)
		shaderKind = shaderc_shader_kind::shaderc_vertex_shader;
	else if (type == ShaderType::Geometry)
		shaderKind = shaderc_shader_kind::shaderc_geometry_shader;
	else if (type == ShaderType::Fragment)
		shaderKind = shaderc_shader_kind::shaderc_fragment_shader;
	else if (type == ShaderType::Compute)
		shaderKind = shaderc_shader_kind::shaderc_compute_shader;
	else if (type == ShaderType::RayGen)
		shaderKind = shaderc_shader_kind::shaderc_raygen_shader;
	else if (type == ShaderType::Miss)
		shaderKind = shaderc_shader_kind::shaderc_miss_shader;
	else if (type == ShaderType::ClosestHit)
		shaderKind = shaderc_shader_kind::shaderc_closesthit_shader;
	else if (type == ShaderType::AnyHit)
		shaderKind = shaderc_shader_kind::shaderc_anyhit_shader;
	else if (type == ShaderType::Intersection)
		shaderKind = shaderc_shader_kind::shaderc_intersection_shader;
	else if (type == ShaderType::Callable)
		shaderKind = shaderc_shader_kind::shaderc_callable_shader;

	options.SetOptimizationLevel(shaderc_optimization_level_performance);// 开启性能优化
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);// 设置目标环境为 Vulkan 1.4

	auto result = compiler.CompileGlslToSpv(
		sourceCode.c_str(),
		sourceCode.size(),
		shaderKind,
		path.c_str(),
		options
	);

	return result;
}


static bool ProcessFileToSPIRV(const std::string& path, std::vector<uint32_t>& code, ShaderType type, const std::map<std::string, std::string>& defines = {})
{
	std::string sourceCode;
	sourceCode = ReadFromFile(path);
	sourceCode = RemoveComments(sourceCode);
	sourceCode = preProcessInclude(sourceCode);
	sourceCode = insertDefinesAfterVersionAdvanced(sourceCode, defines);

	auto result = CompileSourceCodeToSPIRV(sourceCode, type, path);

	// 检查编译结果
	if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
		std::cerr << "Shader 编译失败 (" << path << "): "
			<< result.GetErrorMessage() << std::endl;
		return false;
	}

	// 返回 SPIR-V 字节码
	code.assign(result.cbegin(), result.cend());
	return true;
};

static bool ProcessCodeToSPIRV(const std::string& source, std::vector<uint32_t>& code, ShaderType type, const std::map<std::string, std::string>& defines = {})
{
	std::string sourceCode = source;
	sourceCode = RemoveComments(sourceCode);
	sourceCode = preProcessInclude(sourceCode);
	sourceCode = insertDefinesAfterVersionAdvanced(sourceCode, defines);


	static const std::string path = "DefaultPath";
	auto result = CompileSourceCodeToSPIRV(sourceCode, type, path);

	// 检查编译结果
	if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
		std::cerr << "Shader 编译失败 (" << "unnameshader" << "): "
			<< result.GetErrorMessage() << std::endl;
		return false;
	}

	// 返回 SPIR-V 字节码
	code.assign(result.cbegin(), result.cend());
	return true;
};


class RestireSetGroupHolder :public VKWrapper::IVKResource
{
public:
	RestireSetGroupHolder(std::weak_ptr<Pipeline::DescriptorSetLayoutData> parent, std::shared_ptr<Pipeline::DescriptorSetGroup> data)
		:parent(parent), data(data) {}
	virtual void Destroy() {
		if (auto layoutData = parent.lock()) {
			layoutData->setGroupPool.Recycle(std::move(data));
		}
	}

public:
	std::weak_ptr<Pipeline::DescriptorSetLayoutData> parent;
	std::shared_ptr<Pipeline::DescriptorSetGroup> data;
};

void PipelineConfig::AddDefineMacro(const std::string& name, const std::string& value)
{
	defines[name] = value;
}

void PipelineConfig::RemoveDefineMarco(const std::string& name)
{
	defines.erase(name);
}

PipelineConfig& PipelineConfig::AddUnifromBuffer(uint32_t binding, uint32_t set)
{
	return AddDescriptor(binding, vk::DescriptorType::eUniformBuffer, defaultShaderStageFlags, 1, set);
}

PipelineConfig& PipelineConfig::AddStorageBuffer(uint32_t binding, uint32_t set)
{
	return AddDescriptor(binding, vk::DescriptorType::eStorageBuffer, defaultShaderStageFlags, 1, set);
}

PipelineConfig& PipelineConfig::AddUnifromTexture(uint32_t binding, uint32_t set)
{
	return AddDescriptor(binding, vk::DescriptorType::eCombinedImageSampler, defaultShaderStageFlags, 1, set);
}

PipelineConfig& PipelineConfig::AddUnifromBufferArray(uint32_t binding, uint32_t count, uint32_t set)
{
	return AddDescriptor(binding, vk::DescriptorType::eUniformBuffer, defaultShaderStageFlags, count, set);
}

PipelineConfig& PipelineConfig::AddUnifromTextureArray(uint32_t binding, uint32_t count, uint32_t set)
{
	return AddDescriptor(binding, vk::DescriptorType::eCombinedImageSampler, defaultShaderStageFlags, count, set);
}

PipelineConfig& PipelineConfig::AddUnifromVariableTextureArray(uint32_t binding, uint32_t maxCount, uint32_t set)
{
	vk::DescriptorBindingFlags flags =
		vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
	auto& result = AddDescriptor(binding, vk::DescriptorType::eCombinedImageSampler, defaultShaderStageFlags, maxCount, set, flags);
	variableEntrys[set].isVariable = true;
	variableEntrys[set].maxCount = maxCount;
	return result;
}

PipelineConfig& PipelineConfig::AddDescriptor(uint32_t binding, vk::DescriptorType type, vk::ShaderStageFlags stageflags, uint32_t count, uint32_t set, vk::DescriptorBindingFlags flags) {
	uint32_t needsize = set + 1;
	if (descriptorSetLayouts.size() < needsize)
		descriptorSetLayouts.resize(needsize);

	vk::DescriptorSetLayoutBinding layout;
	layout
		.setBinding(binding)
		.setDescriptorType(type)
		.setStageFlags(stageflags)	// 在哪个着色器中使用
		.setDescriptorCount(count);		// 单个元素还是数组
	descriptorSetLayouts[set].push_back(layout);

	if (bindingFlags.size() < needsize)
		bindingFlags.resize(needsize);
	bindingFlags[set].push_back(flags);

	if (variableEntrys.size() < needsize)
		variableEntrys.resize(needsize);

	return *this;
}

PipelineConfig& PipelineConfig::AddCameraUnifromDataBinding() {
	AddUnifromBuffer(GeneralBindingPoint::Camera_Cur.binding, GeneralBindingPoint::Camera_Cur.set);
	AddUnifromBuffer(GeneralBindingPoint::Camera_Prev.binding, GeneralBindingPoint::Camera_Prev.set);
	return *this;
}

PipelineConfig& PipelineConfig::AddBindlessMaterialTextureBinding()
{
	this->AddStorageBuffer(GeneralBindingPoint::Material_materials.binding, GeneralBindingPoint::Material_materials.set)
		.AddUnifromVariableTextureArray(GeneralBindingPoint::Material_textures.binding, 5000, GeneralBindingPoint::Material_textures.set);
	return *this;
}

PipelineConfig& PipelineConfig::AddLightDataBinding()
{
	this->AddStorageBuffer(GeneralBindingPoint::Light_DirLightMetaData.binding, GeneralBindingPoint::Light_DirLightMetaData.set)
		.AddStorageBuffer(GeneralBindingPoint::Light_DirLightCascadeData.binding, GeneralBindingPoint::Light_DirLightCascadeData.set)
		.AddStorageBuffer(GeneralBindingPoint::Light_PointLightMetaData.binding, GeneralBindingPoint::Light_PointLightMetaData.set)
		.AddStorageBuffer(GeneralBindingPoint::Light_SpotLightMetaData.binding, GeneralBindingPoint::Light_SpotLightMetaData.set);
	return *this;
}


PipelineConfig& PipelineConfig::AddAnimationDataBinding()
{
	this->AddStorageBuffer(GeneralBindingPoint::Animation_MetaData.binding, GeneralBindingPoint::Animation_MetaData.set)
		.AddStorageBuffer(GeneralBindingPoint::Animation_MatData.binding, GeneralBindingPoint::Animation_MatData.set)
		.AddStorageBuffer(GeneralBindingPoint::Animation_PrevMetaData.binding, GeneralBindingPoint::Animation_PrevMetaData.set)
		.AddStorageBuffer(GeneralBindingPoint::Animation_PrevMatData.binding, GeneralBindingPoint::Animation_PrevMatData.set);
	return *this;
}

void PipelineConfig::AddPushConstant(uint32_t size) {
	pushConstantSizes.push_back(size);
}

DynamicRenderInfo& DynamicRenderInfo::SetRenderArea(uint32_t width, uint32_t height, uint32_t x, uint32_t y)
{
	renderingInfo
		.setRenderArea({ { (int32_t)x, (int32_t)y },{ width, height } })
		.setLayerCount(1);
	return *this;
}

DynamicRenderInfo& DynamicRenderInfo::AddColorAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp, vk::ClearColorValue clearColor)
{
	vk::RenderingAttachmentInfo colorAttachment;
	colorAttachment
		.setImageView(imageView)
		.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
		.setLoadOp(loadOp)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setClearValue(clearColor);

	colorAttachments.push_back(std::move(colorAttachment));
	renderingInfo.setColorAttachments(colorAttachments);
	return *this;
}

DynamicRenderInfo& DynamicRenderInfo::AddDepthAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp, vk::ClearDepthStencilValue clearValue)
{
	if (!depthAttachment)
		depthAttachment = new vk::RenderingAttachmentInfo();

	auto& attachment = *depthAttachment;
	attachment
		.setImageView(imageView)
		.setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
		.setLoadOp(loadOp)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setClearValue(clearValue);

	renderingInfo.setPDepthAttachment(depthAttachment);
	return *this;
}

DynamicRenderInfo& DynamicRenderInfo::AddStencilAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp, vk::ClearDepthStencilValue clearValue)
{
	if (!stencilAttachment)
		stencilAttachment = new vk::RenderingAttachmentInfo();

	auto& attachment = *stencilAttachment;
	attachment
		.setImageView(imageView)
		.setImageLayout(vk::ImageLayout::eStencilAttachmentOptimal)
		.setLoadOp(loadOp)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setClearValue(clearValue);

	renderingInfo.setPStencilAttachment(stencilAttachment);
	return *this;
}

DynamicRenderInfo& DynamicRenderInfo::AddDepthStencilAttachment(vk::ImageView imageView, vk::AttachmentLoadOp loadOp, vk::ClearDepthStencilValue clearValue)
{
	if (!depthStencilAttachment)
		depthStencilAttachment = new vk::RenderingAttachmentInfo();

	auto& attachment = *depthStencilAttachment;
	attachment
		.setImageView(imageView)
		.setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
		.setLoadOp(loadOp)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setClearValue(clearValue);

	renderingInfo.setPDepthAttachment(depthStencilAttachment);
	renderingInfo.setPStencilAttachment(depthStencilAttachment);
	return *this;
}

Pipeline::~Pipeline() {
	Release();
}

Pipeline::Pipeline(Pipeline&& other) noexcept
	: m_device(other.m_device)
	, m_pipeline(other.m_pipeline)
	, m_layout(other.m_layout)
	, m_descriptorSetLayouts(std::move(other.m_descriptorSetLayouts))
{
	other.m_device = nullptr;
	other.m_pipeline = VK_NULL_HANDLE;
	other.m_layout = VK_NULL_HANDLE;
}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept {
	if (this != &other) {
		Release();
		m_device = other.m_device;
		m_pipeline = other.m_pipeline;
		m_layout = other.m_layout;
		m_descriptorSetLayouts = std::move(other.m_descriptorSetLayouts);
		other.m_device = nullptr;
		other.m_pipeline = VK_NULL_HANDLE;
		other.m_layout = VK_NULL_HANDLE;
	}
	return *this;
}

void Pipeline::Bind(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, BindingRecord& bindingRecord)
{
	if (!cmdBuffer)
		return;

	DescriptorSetGroupHolder setGroupHolder;
	if (!m_descriptorSetLayouts->GetDescriptorSetGroup(setGroupHolder))
		return;

	bindingRecord.BindAllEntry(cmdBuffer, setGroupHolder.data, m_bindStage);
	cmdBuffer->bindDescriptorSets(m_bindPoint, m_layout, 0, setGroupHolder.data->sets);
}

void Pipeline::SetPushConstants(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd, const void* data, uint32_t size, uint32_t offset)
{
	if (!cmd)
		return;

	vk::ShaderStageFlags flags;
	if (m_bindStage == ImageLayout::BindStage::Graphics)
		flags = vk::ShaderStageFlagBits::eAllGraphics;
	else if (m_bindStage == ImageLayout::BindStage::Compute)
		flags = vk::ShaderStageFlagBits::eCompute;
	else if (m_bindStage == ImageLayout::BindStage::RayTracing)
		flags = vk::ShaderStageFlagBits::eRaygenKHR
		| vk::ShaderStageFlagBits::eClosestHitKHR
		| vk::ShaderStageFlagBits::eMissKHR
		| vk::ShaderStageFlagBits::eAnyHitKHR
		| vk::ShaderStageFlagBits::eIntersectionKHR
		| vk::ShaderStageFlagBits::eCallableKHR;

	cmd->pushConstants(m_layout, flags, offset, size, data);
}

vk::Pipeline Pipeline::GetHandle() const { return m_pipeline; }

vk::PipelineLayout Pipeline::GetLayout() const { return m_layout; }

const std::vector<vk::DescriptorSetLayout>& Pipeline::GetDescriptorSetLayout() const { return m_descriptorSetLayouts->setLayouts; }

VKCore::VulkanDevice* Pipeline::GetDevice() const { return m_device; }

bool Pipeline::IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

std::vector<uint32_t> Pipeline::ReadSPIRVFromSourcePath(const std::string& path, ShaderType type, const std::map<std::string, std::string>& defines)
{
	std::vector<uint32_t> result;
	ProcessFileToSPIRV(path, result, type, defines);
	return result;
}

std::vector<uint32_t> Pipeline::ReadSPIRVFromSourceCode(const std::string& sourceCode, ShaderType type, const std::map<std::string, std::string>& defines)
{
	std::vector<uint32_t> result;
	ProcessCodeToSPIRV(sourceCode, result, type, defines);
	return result;
}

std::vector<uint32_t> Pipeline::ReadSPIRVBinaryPath(const std::string& path)
{
	return std::vector<uint32_t>();
}

void Pipeline::Release()
{
	if (m_device)
	{
		if (m_pipeline)
			m_device->GetHandle().destroyPipeline(m_pipeline);
		if (m_layout)
			m_device->GetHandle().destroyPipelineLayout(m_layout);
	}
	m_device = nullptr;
	m_pipeline = VK_NULL_HANDLE;
	m_layout = VK_NULL_HANDLE;
	m_descriptorSetLayouts.reset();
}

vk::Result Pipeline::CreatePipelineLayout(const PipelineConfig& config) {

	vk::UniqueDescriptorSetLayout handle;

	auto& bindings = config.descriptorSetLayouts;
	auto& bindingFlags = config.bindingFlags;
	auto& variableEntrys = config.variableEntrys;

	std::vector<vk::PushConstantRange> pushConstants;
	pushConstants.reserve(config.pushConstantSizes.size());

	vk::ShaderStageFlags flags;
	if (m_bindStage == ImageLayout::BindStage::Graphics)
		flags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
	else if (m_bindStage == ImageLayout::BindStage::Compute)
		flags = vk::ShaderStageFlagBits::eCompute;
	else if (m_bindStage == ImageLayout::BindStage::RayTracing)
		flags =
		vk::ShaderStageFlagBits::eRaygenKHR
		| vk::ShaderStageFlagBits::eClosestHitKHR
		| vk::ShaderStageFlagBits::eMissKHR
		| vk::ShaderStageFlagBits::eAnyHitKHR
		| vk::ShaderStageFlagBits::eIntersectionKHR
		| vk::ShaderStageFlagBits::eCallableKHR;

	uint32_t offset = 0;
	for (const auto& size : config.pushConstantSizes)
	{
		vk::PushConstantRange constant;
		constant
			.setStageFlags(flags)
			.setOffset(offset)
			.setSize(size);

		pushConstants.push_back(constant);
		offset += size;
	}

	{
		std::vector<vk::DescriptorSetLayout> setLayouts;
		if (auto result = CreateDescriptorSetLayout(bindings, bindingFlags, setLayouts); result != vk::Result::eSuccess)
			return result;
		m_descriptorSetLayouts = std::make_shared<DescriptorSetLayoutData>(m_device, std::move(setLayouts));
	}

	if (auto result = CreateDescriptorSets(bindingFlags, variableEntrys); result != vk::Result::eSuccess)
		return result;

	vk::PipelineLayoutCreateInfo createInfo;
	createInfo.setSetLayouts(m_descriptorSetLayouts->setLayouts)
		.setPushConstantRanges(pushConstants);

	auto [result, layout] = m_device->GetHandle().createPipelineLayout(createInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("fail to create PipelineLayout! error = {}\n", to_string(result));
		return result;
	}

	m_layout = layout;
	return vk::Result::eSuccess;
}

vk::Result Pipeline::CreateDescriptorSetLayout(const std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& bindings, const std::vector<std::vector<vk::DescriptorBindingFlags>>& bindingFlags, std::vector<vk::DescriptorSetLayout>& outLayouts)
{
	outLayouts.clear();
	outLayouts.reserve(bindings.size());
	for (uint32_t i = 0; i < bindings.size(); i++)
	{
		auto& binding = bindings[i];
		auto& flags = bindingFlags[i];

		vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo;
		bindingFlagsInfo.setBindingFlags(flags);

		vk::DescriptorSetLayoutCreateInfo createInfo = {};
		createInfo.setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool)
			.setBindings(binding)
			.setPNext(&bindingFlagsInfo);

		auto [result, layout] = m_device->GetHandle().createDescriptorSetLayout(createInfo);
		if (result != vk::Result::eSuccess)
		{
			std::cout << std::format("fail to create DescriptorSetLayout! error = {}\n", to_string(result));
			return result;
		}
		outLayouts.push_back(std::move(layout));
	}
	return vk::Result::eSuccess;
}

vk::Result Pipeline::CreateDescriptorSets(
	const std::vector<std::vector<vk::DescriptorBindingFlags>>& bindingFlags,
	const std::vector<PipelineConfig::VariableEntry>& variableEntrys
)
{
	const std::vector<vk::DescriptorSetLayout>& setLayouts = m_descriptorSetLayouts->setLayouts;
	std::vector<DescriptorSetLayoutData::SetLayoutInfo>& setLayoutInfos = m_descriptorSetLayouts->setLayoutInfos;
	setLayoutInfos.resize(setLayouts.size());
	for (uint32_t i = 0; i < setLayouts.size(); i++)
	{
		setLayoutInfos[i].isVariable = variableEntrys[i].isVariable;
		setLayoutInfos[i].variableCount = variableEntrys[i].maxCount;
		setLayoutInfos[i].isNullSet = bindingFlags[i].empty();
	}
	return vk::Result::eSuccess;
}

//vk::Result Pipeline::CreateDescriptorSets(
//	const std::vector<vk::DescriptorSetLayout>& setLayouts,
//	const std::vector<std::vector<vk::DescriptorBindingFlags>>& bindingFlags,
//	const std::vector<PipelineConfig::VariableEntry>& variableEntrys,
//	std::vector<vk::DescriptorSet>& outSets
//)
//{
//	for (uint32_t i = 0; i < setLayouts.size(); i++)
//	{
//		vk::DescriptorSetAllocateInfo allocInfo;
//		allocInfo
//			.setDescriptorPool(VKCONTEXT->GetDescriptorPool())
//			.setSetLayouts(setLayouts[i]);
//
//		uint32_t variableCount = 0;
//		vk::DescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo;
//		if (variableEntrys[i].isVariable)
//		{
//			variableCount = variableEntrys[i].maxCount;
//			variableCountInfo.setDescriptorCounts(variableCount);
//			allocInfo.setPNext(&variableCountInfo);
//		}
//
//		auto [result, descriptorSets] = VKCONTEXT->GetDeviceHandle().allocateDescriptorSets(allocInfo);
//		if (result != vk::Result::eSuccess) {
//			std::cout << std::format("fail to create descriptorSets! error = {}\n", to_string(result));
//			return result;
//		}
//
//		outSets.push_back(descriptorSets[0]);
//	}
//	return vk::Result::eSuccess;
//}

Pipeline::DescriptorSetGroup::DescriptorSetGroup(VKCore::VulkanDevice* device, const std::vector<vk::DescriptorSet>& sets)
	:device(device), sets(sets) {}

Pipeline::DescriptorSetGroup::~DescriptorSetGroup() {
	if (!sets.empty())
		VKCONTEXT->FreeDescriptorSets(sets);
}

Pipeline::DescriptorSetGroupPool::DescriptorSetGroupPool(uint32_t maxResNum) :_maxResNum(maxResNum) {}

std::shared_ptr<Pipeline::DescriptorSetGroup> Pipeline::DescriptorSetGroupPool::Fetch()
{
	LockGuard guard(_mutex);
	if (_iDleList.size() > 0) // 不为空，从中取一个
	{
		auto it = _iDleList.begin();
		auto handle = *it;
		_iDleList.erase(it);
		return handle;
	}
	return nullptr;
}

bool Pipeline::DescriptorSetGroupPool::Recycle(std::shared_ptr<DescriptorSetGroup> handle)
{
	LockGuard guard(_mutex);
	auto it = _datas.find(handle);
	if (it != _datas.end())
	{
		// 已持有的数据
		if (_iDleList.find(handle) == _iDleList.end())
		{
			_iDleList.insert(handle);
			return true;
		}
		else
		{
			std::cerr << "DescriptorSetGroupPool::RecycleData double free!!!\n";
			return true;
		}
	}
	else // 外部数据，非池子持有的
	{
		if (_datas.size() < _maxResNum)
		{
			_datas.insert(handle);
			_iDleList.insert(handle);
			return true;
		}
		else
		{
			return false;
		}
	}
}

void Pipeline::DescriptorSetGroupPool::Clear() {
	LockGuard guard(_mutex);
	_iDleList.clear();
	_datas.clear();
}

Pipeline::DescriptorSetLayoutData::DescriptorSetLayoutData(VKCore::VulkanDevice* device, const std::vector<vk::DescriptorSetLayout>& setLayouts)
	:device(device), setLayouts(setLayouts) {}

Pipeline::DescriptorSetLayoutData::~DescriptorSetLayoutData() {
	for (auto& desc : setLayouts)
	{
		if (desc) device->GetHandle().destroyDescriptorSetLayout(desc);
	}
}

bool Pipeline::DescriptorSetLayoutData::GetDescriptorSetGroup(DescriptorSetGroupHolder& holder) {
	auto setGroup = setGroupPool.Fetch();
	if (!setGroup)
	{
		std::vector<vk::DescriptorSet> descriptorSets;
		descriptorSets.reserve(setLayouts.size());
		for (uint32_t i = 0; i < setLayouts.size(); i++)
		{
			//if (setLayoutInfos[i].isNullSet)
			//{
			//	descriptorSets.push_back(VK_NULL_HANDLE);
			//	continue;
			//}

			vk::DescriptorSetAllocateInfo allocInfo;
			allocInfo
				.setSetLayouts(setLayouts[i]);

			vk::DescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo;
			if (setLayoutInfos[i].isVariable)
			{
				variableCountInfo.setDescriptorCounts(setLayoutInfos[i].variableCount);
				allocInfo.setPNext(&variableCountInfo);
			}

			auto [result, temp] = VKCONTEXT->AllocateDescriptorSets(allocInfo);
			if (result != vk::Result::eSuccess) {
				std::cout << std::format("fail to create descriptorSets! error = {}\n", to_string(result));
				return false;
			}

			descriptorSets.push_back(temp[0]);
		}

		setGroup = std::make_shared<DescriptorSetGroup>(device, std::move(descriptorSets));
	}

	holder.parent = shared_from_this();
	holder.data = std::move(setGroup);
	return true;
}

Pipeline::DescriptorSetGroupHolder::DescriptorSetGroupHolder(std::weak_ptr<DescriptorSetLayoutData> parent, std::shared_ptr<DescriptorSetGroup> data)
	:parent(parent), data(data) {}

Pipeline::DescriptorSetGroupHolder::~DescriptorSetGroupHolder() {
	if (data)
		VKCONTEXT->Retire(new RestireSetGroupHolder(std::move(parent), std::move(data)));
	parent.reset();
	data.reset();
}

void BindingRecord::SetUniformBlock(const std::shared_ptr<UniformBlock>& uniformBlock, uint32_t binding, uint32_t set)
{
	SetUniformBlock(uniformBlock, BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetStorageBlock(const std::shared_ptr<StorageBlock>& storageBlock, uint32_t binding, uint32_t set)
{
	SetStorageBlock(storageBlock, BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetUniformTexture(const std::shared_ptr<Texture2D>& uniformTex, vk::ImageAspectFlags aspect, uint32_t binding, uint32_t set)
{
	SetUniformTexture(uniformTex, aspect, BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetUniformTextureCube(const std::shared_ptr<TextureCube>& uniformTexCube, vk::ImageAspectFlags aspect, uint32_t binding, uint32_t set)
{
	SetUniformTextureCube(uniformTexCube, aspect, BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetUniformTextureArray(const std::shared_ptr<ITextureArrayProvider>& provider, uint32_t binding, uint32_t set)
{
	SetUniformTextureArray(provider, BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetUniformTextureArray(const std::vector<std::shared_ptr<Texture2D>>& array, vk::ImageAspectFlags aspect, uint32_t binding, uint32_t set)
{
	SetUniformTextureArray(array, aspect, BindingPoint{ .binding = binding, .set = set });
}

std::shared_ptr<UniformBlock> BindingRecord::GetUniformBlock(uint32_t binding, uint32_t set)
{
	return GetUniformBlock(BindingPoint{ .binding = binding, .set = set });
}

std::shared_ptr<StorageBlock> BindingRecord::GetStorageBlock(uint32_t binding, uint32_t set)
{
	return GetStorageBlock(BindingPoint{ .binding = binding, .set = set });
}

std::shared_ptr<UniformBlock> BindingRecord::FindUniformBlock(uint32_t binding, uint32_t set)
{
	return FindUniformBlock(BindingPoint{ .binding = binding, .set = set });
}

std::shared_ptr<StorageBlock> BindingRecord::FindStorageBlock(uint32_t binding, uint32_t set)
{
	return FindStorageBlock(BindingPoint{ .binding = binding, .set = set });
}

std::shared_ptr<Texture2D> BindingRecord::FindUniformTexture(uint32_t binding, uint32_t set)
{
	return FindUniformTexture(BindingPoint{ .binding = binding, .set = set });
}

std::shared_ptr<ITextureArrayProvider> BindingRecord::FindUniformTextureArray(uint32_t binding, uint32_t set)
{
	return FindUniformTextureArray(BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetUniformBlock(const std::shared_ptr<UniformBlock>& uniformBlock, const BindingPoint& bp)
{
	if (!uniformBlock)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::UniformBlock, .data = UniformBlockEntry{.block = uniformBlock} };
	m_bindingData[bp] = entry;
}

void BindingRecord::SetStorageBlock(const std::shared_ptr<StorageBlock>& storageBlock, const BindingPoint& bp)
{
	if (!storageBlock)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::StorageBlock, .data = StorageBlockEntry{.block = storageBlock} };
	m_bindingData[bp] = entry;
}

void BindingRecord::SetUniformTexture(const std::shared_ptr<Texture2D>& uniformTex, vk::ImageAspectFlags aspect, const BindingPoint& bp)
{
	if (!uniformTex)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::UniformTex, .data = UniformTextureEntry{.texture = uniformTex, .aspect = aspect} };
	m_bindingData[bp] = entry;
}

void BindingRecord::SetUniformTextureCube(const std::shared_ptr<TextureCube>& UniformTexCube, vk::ImageAspectFlags aspect, const BindingPoint& bp)
{
	if (!UniformTexCube)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::UniformTexCube, .data = UniformTextureCubeEntry{.texture = UniformTexCube, .aspect = aspect} };
	m_bindingData[bp] = entry;
}

void BindingRecord::SetUniformTextureArray(const std::shared_ptr<ITextureArrayProvider>& provider, const BindingPoint& bp)
{
	if (!provider)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::UniformTexArray, .data = UniformTextureArrayEntry{.provider = provider} };
	m_bindingData[bp] = entry;
}

void BindingRecord::SetUniformTextureArray(const std::vector<std::shared_ptr<Texture2D>>& array, vk::ImageAspectFlags aspect, const BindingPoint& bp)
{
	if (array.empty())
		return;
	auto provider = std::make_shared<BaseTextureArrayProvider>(array, aspect);
	BindingEntry entry{ .type = BindingEntry::DataType::UniformTexArray, .data = UniformTextureArrayEntry{.provider = provider} };
	m_bindingData[bp] = entry;
}

std::shared_ptr<UniformBlock> BindingRecord::GetUniformBlock(const BindingPoint& bp)
{
	auto data = FindUniformBlock(bp);
	if (!data)
	{
		data = std::make_shared<UniformBlock>();
		SetUniformBlock(data, bp);
	}
	return data;
}

std::shared_ptr<StorageBlock> BindingRecord::GetStorageBlock(const BindingPoint& bp)
{
	auto data = FindStorageBlock(bp);
	if (!data)
	{
		data = std::make_shared<StorageBlock>();
		SetStorageBlock(data, bp);
	}
	return data;
}

std::shared_ptr<UniformBlock> BindingRecord::FindUniformBlock(const BindingPoint& bp)
{
	auto it = m_bindingData.find(bp);
	if (it == m_bindingData.end() || it->second.type != BindingEntry::DataType::UniformBlock)
		return nullptr;

	BindingEntry entry = it->second;
	UniformBlockEntry* dataptr = std::get_if<UniformBlockEntry>(&entry.data);
	if (!dataptr)
		return nullptr;

	return dataptr->block;
}

std::shared_ptr<StorageBlock> BindingRecord::FindStorageBlock(const BindingPoint& bp)
{
	auto it = m_bindingData.find(bp);
	if (it == m_bindingData.end() || it->second.type != BindingEntry::DataType::StorageBlock)
		return nullptr;

	BindingEntry entry = it->second;
	StorageBlockEntry* dataptr = std::get_if<StorageBlockEntry>(&entry.data);
	if (!dataptr)
		return nullptr;

	return dataptr->block;
}

std::shared_ptr<Texture2D> BindingRecord::FindUniformTexture(const BindingPoint& bp)
{
	auto it = m_bindingData.find(bp);
	if (it == m_bindingData.end() || it->second.type != BindingEntry::DataType::UniformTex)
		return nullptr;

	BindingEntry entry = it->second;
	UniformTextureEntry* dataptr = std::get_if<UniformTextureEntry>(&entry.data);
	if (!dataptr)
		return nullptr;

	return dataptr->texture;
}

std::shared_ptr<ITextureArrayProvider> BindingRecord::FindUniformTextureArray(const BindingPoint& bp)
{
	auto it = m_bindingData.find(bp);
	if (it == m_bindingData.end() || it->second.type != BindingEntry::DataType::UniformTexArray)
		return {};

	BindingEntry entry = it->second;
	UniformTextureArrayEntry* dataptr = std::get_if<UniformTextureArrayEntry>(&entry.data);
	if (!dataptr)
		return {};

	return dataptr->provider;
}

void BindingRecord::SetCameraUnifromData(const std::shared_ptr<UniformBlock>& curCmaeraUBO, const std::shared_ptr<UniformBlock>& prevCameraUBO)
{
	if (curCmaeraUBO) SetUniformBlock(curCmaeraUBO, GeneralBindingPoint::Camera_Cur);
	if (prevCameraUBO) SetUniformBlock(prevCameraUBO, GeneralBindingPoint::Camera_Prev);
}

void BindingRecord::SetBindlessMaterialTexture(const std::shared_ptr<StorageBlock>& materials, const std::shared_ptr<ITextureArrayProvider>& textures)
{
	if (materials) SetStorageBlock(materials, GeneralBindingPoint::Material_materials);
	if (textures) SetUniformTextureArray(textures, GeneralBindingPoint::Material_textures);
}

void BindingRecord::SetLightStorageData(
	const std::shared_ptr<StorageBlock>& _ssbo_dirLightMeta,
	const std::shared_ptr<StorageBlock>& _ssbo_dirLightCascade,
	const std::shared_ptr<StorageBlock>& _ssbo_pointLightMeta,
	const std::shared_ptr<StorageBlock>& _ssbo_spotLightMeta
)
{
	if (_ssbo_dirLightMeta) SetStorageBlock(_ssbo_dirLightMeta, GeneralBindingPoint::Light_DirLightMetaData);
	if (_ssbo_dirLightCascade) SetStorageBlock(_ssbo_dirLightCascade, GeneralBindingPoint::Light_DirLightCascadeData);
	if (_ssbo_pointLightMeta) SetStorageBlock(_ssbo_pointLightMeta, GeneralBindingPoint::Light_PointLightMetaData);
	if (_ssbo_spotLightMeta) SetStorageBlock(_ssbo_spotLightMeta, GeneralBindingPoint::Light_SpotLightMetaData);
}

void BindingRecord::BindAllEntry(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage)
{
	if (!cmdBuffer)
		return;

	uint32_t size = data->sets.size();
	for (auto& [bindingpoint, entry] : m_bindingData)
	{
		if (bindingpoint.set < size && data->sets[bindingpoint.set] != VK_NULL_HANDLE)
			BindEntry(cmdBuffer, data, bindStage, bindingpoint, entry);
	}
}

void BindingRecord::BindEntry(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, const BindingPoint& point, BindingEntry& entry)
{
	if (entry.type == BindingEntry::DataType::UniformBlock)
	{
		UniformBlockEntry* dataptr = std::get_if<UniformBlockEntry>(&entry.data);
		if (!dataptr) return;
		BindUniformBlock(*dataptr, data, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::StorageBlock)
	{
		StorageBlockEntry* dataptr = std::get_if<StorageBlockEntry>(&entry.data);
		if (!dataptr) return;
		BindStorageBlock(*dataptr, data, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::UniformTex)
	{
		UniformTextureEntry* dataptr = std::get_if<UniformTextureEntry>(&entry.data);
		if (!dataptr) return;
		BindUniformTexture(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::UniformTexCube)
	{
		UniformTextureCubeEntry* dataptr = std::get_if<UniformTextureCubeEntry>(&entry.data);
		if (!dataptr) return;
		BindUniformTextureCube(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::UniformTexArray)
	{
		UniformTextureArrayEntry* dataptr = std::get_if<UniformTextureArrayEntry>(&entry.data);
		if (!dataptr) return;
		BindUniformTextureArray(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::StorageImage)
	{
		StorageImageEntry* dataptr = std::get_if<StorageImageEntry>(&entry.data);
		if (!dataptr) return;
		BindStorageImage(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::StorageImageArray)
	{
		StorageImageArrayEntry* dataptr = std::get_if<StorageImageArrayEntry>(&entry.data);
		if (!dataptr) return;
		BindStorageImageArray(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::AccelerationStructure)
	{
		AccelerationStructureEntry* dataptr = std::get_if<AccelerationStructureEntry>(&entry.data);
		if (!dataptr) return;
		BindAccelerationStructure(*dataptr, data, point.binding, point.set);
	}
}

void BindingRecord::BindUniformBlock(UniformBlockEntry& entry, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, uint32_t binding, uint32_t set)
{
	if (entry.block)
	{
		auto buffer = entry.block->GetBuffer();
		if (buffer)
			entry.buffer = buffer;
	}

	if (!entry.buffer)
		return;

	vk::DescriptorBufferInfo bufferInfo;
	bufferInfo
		.setBuffer(entry.buffer->GetHandle())
		.setOffset(0)
		.setRange(entry.buffer->GetSize());

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDstArrayElement(0)
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)
		.setBufferInfo(bufferInfo);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindStorageBlock(StorageBlockEntry& entry, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, uint32_t binding, uint32_t set)
{
	if (entry.block)
	{
		auto buffer = entry.block->GetBuffer();
		if (buffer)
			entry.buffer = buffer;
	}

	if (!entry.buffer)
		return;

	vk::DescriptorBufferInfo bufferInfo;
	bufferInfo
		.setBuffer(entry.buffer->GetHandle())
		.setOffset(0)
		.setRange(entry.buffer->GetSize());

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDstArrayElement(0)
		.setDescriptorType(vk::DescriptorType::eStorageBuffer)
		.setBufferInfo(bufferInfo);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindUniformTexture(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, UniformTextureEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.texture)
	{
		if (entry.descEntry.version != entry.texture->GetDescBindEntryVersion())
			entry.descEntry = entry.texture->GetDescBindEntry(entry.aspect);
	}

	auto& texrure = entry.texture;
	auto& imageView = entry.descEntry.imageView;
	auto& sampler = entry.descEntry.sampler;

	if (!texrure || !imageView || !sampler)
		return;

	vk::ImageLayout imageLayout;
	texrure->TransitionLayout(cmdBuffer, &imageLayout, bindStage, ImageLayout::BindUsage::Read);

	vk::DescriptorImageInfo imageInfo;
	imageInfo.setImageView(imageView->GetHandle())
		.setSampler(sampler->GetHandle())
		.setImageLayout(imageLayout);

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDstArrayElement(0)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
		.setImageInfo(imageInfo);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindUniformTextureCube(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, UniformTextureCubeEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.texture)
	{
		if (entry.descEntry.version != entry.texture->GetDescBindEntryVersion())
			entry.descEntry = entry.texture->GetDescBindEntry();
	}

	auto& texrure = entry.texture;
	auto& imageView = entry.descEntry.imageView;
	auto& sampler = entry.descEntry.sampler;

	if (!texrure || !imageView || !sampler)
		return;

	vk::ImageLayout imageLayout;
	texrure->TransitionLayout(cmdBuffer, &imageLayout, bindStage, ImageLayout::BindUsage::Read);

	vk::DescriptorImageInfo imageInfo;
	imageInfo.setImageView(imageView->GetHandle())
		.setSampler(sampler->GetHandle())
		.setImageLayout(imageLayout);

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDstArrayElement(0)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
		.setImageInfo(imageInfo);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindUniformTextureArray(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, UniformTextureArrayEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.provider)
		entry.descEntrys = entry.provider->GetTextureDescBindEntrys();

	std::vector<vk::DescriptorImageInfo> imageInfos;
	for (auto& desc : entry.descEntrys)
	{
		auto imageFormat = desc.image->GetFormat();
		vk::ImageLayout imageLayout;
		vk::PipelineStageFlags dstStageMask;
		ImageLayout::GetImageLayoutAndStageFlag(&imageLayout, &dstStageMask, imageFormat, bindStage, ImageLayout::BindUsage::Read);
		desc.image->TransitionLayout(cmdBuffer, imageLayout, dstStageMask);

		vk::DescriptorImageInfo imageInfo;
		imageInfo.setImageView(desc.imageView->GetHandle())
			.setSampler(desc.sampler->GetHandle())
			.setImageLayout(imageLayout);

		imageInfos.push_back(imageInfo);
	}

	if (imageInfos.empty())
		return;

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDstArrayElement(0)
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
		.setImageInfo(imageInfos);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindStorageImage(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, StorageImageEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.texture)
	{
		if (entry.descEntry.version != entry.texture->GetDescBindEntryVersion())
			entry.descEntry = entry.texture->GetDescBindEntry(entry.aspect, entry.baseLevel, entry.levelCount);
	}

	auto& texrure = entry.texture;
	auto& imageView = entry.descEntry.imageView;
	auto& sampler = entry.descEntry.sampler;

	if (!texrure || !imageView || !sampler)
		return;

	vk::ImageLayout imageLayout;
	texrure->TransitionLayout(cmdBuffer, &imageLayout, bindStage, ImageLayout::BindUsage::Write);

	vk::DescriptorImageInfo imageInfo;
	imageInfo.setImageView(imageView->GetHandle())
		.setSampler(sampler->GetHandle())
		.setImageLayout(imageLayout);

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDstArrayElement(0)
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setImageInfo(imageInfo);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindStorageImageArray(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, StorageImageArrayEntry& arrayEntry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;


	std::vector<vk::DescriptorImageInfo> imageInfos;
	for (auto& entry : arrayEntry.entrys)
	{
		if (entry.descEntry.version != entry.texture->GetDescBindEntryVersion())
			entry.descEntry = entry.texture->GetDescBindEntry(entry.aspect, entry.baseLevel, entry.levelCount);

		auto imageFormat = entry.descEntry.image->GetFormat();
		vk::ImageLayout imageLayout;
		vk::PipelineStageFlags dstStageMask;
		ImageLayout::GetImageLayoutAndStageFlag(&imageLayout, &dstStageMask, imageFormat, bindStage, ImageLayout::BindUsage::Read);
		entry.descEntry.image->TransitionLayout(cmdBuffer, imageLayout, dstStageMask, entry.baseLevel, entry.levelCount);

		vk::DescriptorImageInfo imageInfo;
		imageInfo.setImageView(entry.descEntry.imageView->GetHandle())
			.setSampler(entry.descEntry.sampler->GetHandle())
			.setImageLayout(imageLayout);

		imageInfos.push_back(imageInfo);
	}

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDstArrayElement(0)
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setImageInfo(imageInfos);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindAccelerationStructure(AccelerationStructureEntry& entry, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, uint32_t binding, uint32_t set)
{
	if (entry.handle == VK_NULL_HANDLE)
		return;

	vk::WriteDescriptorSetAccelerationStructureKHR asWrite;
	asWrite.setAccelerationStructures(entry.handle);

	vk::WriteDescriptorSet write;
	write
		.setDstSet(data->sets[set])
		.setDstBinding(binding)
		.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR)
		.setDescriptorCount(1)
		.setPNext(&asWrite);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

vk::Result Pipeline::CreateShaderModule(const std::vector<uint32_t>& code, vk::ShaderModule& outModule) {
	vk::ShaderModuleCreateInfo createInfo = {};
	createInfo.setCode(code);

	auto [result, module] = m_device->GetHandle().createShaderModule(createInfo);
	if (result != vk::Result::eSuccess)
	{
		std::cout << std::format("fail to create ShaderModule! error = {}\n", to_string(result));
		return result;
	}
	outModule = module;
	return vk::Result::eSuccess;
}