#include "vkstdafx.h"
#include "shaderc/shaderc.hpp"
#include "VulkanRenderEngine/Base/Pipeline.h"
#include "VulkanRenderEngine/VKContext.h"
#include <map>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

static std::tuple<std::string, bool> ReadFromFile(const std::string& path)
{
	std::ifstream file;
	file.exceptions(std::ifstream::badbit);// 异常机制处理：保证ifstream对象可以抛出异常：

	try
	{
		file.open(path);
		if (!file) {
			std::cerr << "文件打开失败" << ",path=" << path << std::endl;
			return { std::string(), false };
		}

		std::stringstream stream;
		stream << file.rdbuf();
		file.close();

		return { stream.str(), true };
	}
	catch (std::ifstream::failure e) {
		std::cout << "ERROR::FILE_NOT_SUCCESSFULLY_READ" << std::endl;
	}

	return { std::string(), false };
}
// 去掉 // 与 /* */ 注释。手写状态机而不用 std::regex：
//   * 原先的块注释正则 /\*.*?\*/ 里 '.' 不匹配换行，跨行块注释根本删不掉；
//   * MSVC 的 std::regex 是递归实现，[\s\S]*? 匹配长注释时可能爆栈。
// 块注释里的换行原样保留，shaderc 报错的行号不会因此整体错位。
static std::string RemoveComments(const std::string& code)
{
	std::string out;
	out.reserve(code.size());

	const size_t n = code.size();
	size_t i = 0;
	while (i < n)
	{
		if (code[i] == '/' && i + 1 < n && code[i + 1] == '/')
		{
			i += 2;
			while (i < n && code[i] != '\n')
				++i;
		}
		else if (code[i] == '/' && i + 1 < n && code[i + 1] == '*')
		{
			i += 2;
			while (i < n && !(code[i] == '*' && i + 1 < n && code[i + 1] == '/'))
			{
				if (code[i] == '\n')
					out += '\n';
				++i;
			}
			i = std::min(n, i + 2);
			out += ' ';		// 防止 a/**/b 被拼成 ab
		}
		else
		{
			out += code[i++];
		}
	}
	return out;
}

// 解析 include 路径：先按原路径找，找不到再按文件名找（与原实现的查找顺序一致）。
// outKey 是规范化后的绝对路径，用于检测循环包含。
static bool ResolveIncludeFile(const std::filesystem::path& includePath, std::string& outCode, std::string& outKey)
{
	for (const auto& candidate : { includePath, includePath.filename() })
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(candidate, ec))
			continue;

		auto [code, readResult] = ReadFromFile(candidate.string());
		if (!readResult)
			continue;

		auto canonical = std::filesystem::weakly_canonical(candidate, ec);
		outKey = ec ? candidate.lexically_normal().string() : canonical.string();
		outCode = std::move(code);
		return true;
	}
	return false;
}

// 递归展开 #include。和原先「整段循环替换」的实现相比：
//   1) 被包含文件的内容先去注释再展开 —— 原实现只对顶层文件去注释，
//      被包含文件注释里写的 #include 也会被当真展开（DDGIUniforms.comp 曾因此无限自包含）；
//   2) 只认行首（可有空白）的 #include 指令，不会误伤行内的同名字符串；
//   3) 维护包含栈：A -> B -> A 这类循环直接跳过并报错，不再死循环；另有深度上限兜底；
//   4) 不做 #pragma once 语义：同一文件被多处包含时照常展开，去重交给文件自己的 #ifndef 守卫，
//      这样依赖「包含前先 #define 不同宏」的头文件（如 lightingHelper 的 OnlyIBLHelper）行为不变。
static std::string ExpandIncludes(const std::string& source, std::vector<std::string>& includeStack, uint32_t depth)
{
	static const std::regex includeLine(R"(^\s*#\s*include\s*[<"]([^>"]*)[>"].*$)");
	constexpr uint32_t MaxIncludeDepth = 32;

	std::string result;
	result.reserve(source.size());

	size_t lineStart = 0;
	while (lineStart <= source.size())
	{
		size_t lineEnd = source.find('\n', lineStart);
		const bool lastLine = (lineEnd == std::string::npos);
		if (lastLine)
			lineEnd = source.size();

		std::string line = source.substr(lineStart, lineEnd - lineStart);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		std::smatch match;
		if (std::regex_match(line, match, includeLine))
		{
			const std::string includeName = match.str(1);
			std::string code;
			std::string key;

			if (!ResolveIncludeFile(std::filesystem::path(includeName), code, key))
			{
				std::cerr << "[Shader] include 文件找不到: " << includeName << std::endl;
			}
			else if (std::find(includeStack.begin(), includeStack.end(), key) != includeStack.end())
			{
				std::cerr << "[Shader] 检测到循环 include，已跳过: " << includeName << std::endl;
			}
			else if (depth >= MaxIncludeDepth)
			{
				std::cerr << "[Shader] include 嵌套超过 " << MaxIncludeDepth << " 层，已跳过: " << includeName << std::endl;
			}
			else
			{
				includeStack.push_back(key);
				result += ExpandIncludes(RemoveComments(code), includeStack, depth + 1);
				includeStack.pop_back();
			}
		}
		else
		{
			result += line;
		}

		if (lastLine)
			break;
		result += '\n';
		lineStart = lineEnd + 1;
	}

	return result;
}

// rootPath：顶层 shader 文件路径。它必须作为包含栈的第一个元素，
// 否则 A -> B -> A 要晚一层才被识别（会多展开一份 A）。
// 从内存源码编译时没有路径，传空即可，此时只能检测被包含文件之间的循环。
static std::string PreProcessInclude(const std::string& shaderSource, const std::string& rootPath = {})
{
	std::vector<std::string> includeStack;
	if (!rootPath.empty())
	{
		std::error_code ec;
		auto canonical = std::filesystem::weakly_canonical(rootPath, ec);
		includeStack.push_back(ec ? std::filesystem::path(rootPath).lexically_normal().string() : canonical.string());
	}
	return ExpandIncludes(shaderSource, includeStack, 0);
}

static std::string InsertDefinesAfterVersionAdvanced(const std::string& shaderSource, const std::map<std::string, std::string>& defines) {
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

	options.SetGenerateDebugInfo();

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


// SPIR-V 内存缓存（只在本进程内有效，不落盘）。
// 键 = shader 阶段 + 展开 include、插入宏之后的最终源码，编译选项固定，所以：
//   * 同一个 shader 用同样的宏被多个 Pass / 多个实例创建时（TemporalAccumulate、À-trous、上采样、combin 等）只编译一次；
//   * 改了 shader 或它 include 的文件、换了宏，源码不同，自然重新编译，不会拿到旧结果。
// 编译失败的结果不缓存（下次还会再报错）。多线程创建管线时同一个键可能被同时编译一次，结果一样，后写的覆盖。
namespace
{
	struct SpirvCache
	{
		std::mutex mutex;
		std::unordered_map<std::string, std::vector<uint32_t>> entries;
	};

	SpirvCache& GetSpirvCache()
	{
		static SpirvCache cache;
		return cache;
	}

	std::string MakeSpirvCacheKey(ShaderType type, const std::string& finalSource)
	{
		std::string key = std::to_string(static_cast<int>(type));
		key.push_back('\n');
		key += finalSource;
		return key;
	}

	bool FindCachedSpirv(const std::string& key, std::vector<uint32_t>& code)
	{
		auto& cache = GetSpirvCache();
		std::lock_guard lock(cache.mutex);
		auto it = cache.entries.find(key);
		if (it == cache.entries.end())
			return false;
		code = it->second;
		return true;
	}

	void StoreCachedSpirv(std::string key, const std::vector<uint32_t>& code)
	{
		auto& cache = GetSpirvCache();
		std::lock_guard lock(cache.mutex);
		cache.entries[std::move(key)] = code;
	}
}

static bool ProcessFileToSPIRV(const std::string& path, std::vector<uint32_t>& code, ShaderType type, const std::map<std::string, std::string>& defines = {})
{
	auto [sourceCode, readResult] = ReadFromFile(path);
	if (sourceCode.empty() || !readResult)
		return false;

	sourceCode = RemoveComments(sourceCode);
	sourceCode = PreProcessInclude(sourceCode, path);
	sourceCode = InsertDefinesAfterVersionAdvanced(sourceCode, defines);

	auto cacheKey = MakeSpirvCacheKey(type, sourceCode);
	if (FindCachedSpirv(cacheKey, code))
		return true;

	auto result = CompileSourceCodeToSPIRV(sourceCode, type, path);

	// 检查编译结果
	if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
		std::cerr << "Shader 编译失败 (" << path << "): "
			<< result.GetErrorMessage() << std::endl;
		return false;
	}

	// 返回 SPIR-V 字节码
	code.assign(result.cbegin(), result.cend());
	StoreCachedSpirv(std::move(cacheKey), code);
	return true;
};

static bool ProcessCodeToSPIRV(const std::string& source, std::vector<uint32_t>& code, ShaderType type, const std::map<std::string, std::string>& defines = {})
{
	std::string sourceCode = source;
	sourceCode = RemoveComments(sourceCode);
	sourceCode = PreProcessInclude(sourceCode);
	sourceCode = InsertDefinesAfterVersionAdvanced(sourceCode, defines);

	auto cacheKey = MakeSpirvCacheKey(type, sourceCode);
	if (FindCachedSpirv(cacheKey, code))
		return true;

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
	StoreCachedSpirv(std::move(cacheKey), code);
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

PipelineConfig& PipelineConfig::AddShadowBindlessMaterialTextureBinding()
{
	this->AddStorageBuffer(GeneralBindingPoint::Shadow_Material_materials.binding, GeneralBindingPoint::Shadow_Material_materials.set)
		.AddUnifromVariableTextureArray(GeneralBindingPoint::Shadow_Material_textures.binding, 5000, GeneralBindingPoint::Shadow_Material_textures.set);
	return *this;
}

PipelineConfig& PipelineConfig::AddLightDataBinding()
{
	this->AddStorageBuffer(GeneralBindingPoint::Light_DirLightMetaData.binding, GeneralBindingPoint::Light_DirLightMetaData.set)
		.AddStorageBuffer(GeneralBindingPoint::Light_DirLightCascadeData.binding, GeneralBindingPoint::Light_DirLightCascadeData.set)
		.AddStorageBuffer(GeneralBindingPoint::Light_PointLightMetaData.binding, GeneralBindingPoint::Light_PointLightMetaData.set)
		.AddStorageBuffer(GeneralBindingPoint::Light_SpotLightMetaData.binding, GeneralBindingPoint::Light_SpotLightMetaData.set)
		.AddStorageBuffer(GeneralBindingPoint::Light_DirLightCascadeDistanceData.binding, GeneralBindingPoint::Light_DirLightCascadeDistanceData.set);
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
		.setStoreOp(vk::AttachmentStoreOp::eStore);

	if (loadOp == vk::AttachmentLoadOp::eClear)
		attachment.setClearValue(clearValue);

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

std::tuple<std::vector<uint32_t>, bool> Pipeline::ReadSPIRVFromSourcePath(const std::string& path, ShaderType type, const std::map<std::string, std::string>& defines)
{
	std::vector<uint32_t> result;
	bool flag = ProcessFileToSPIRV(path, result, type, defines);
	return { result,flag };
}

std::tuple<std::vector<uint32_t>, bool> Pipeline::ReadSPIRVFromSourceCode(const std::string& sourceCode, ShaderType type, const std::map<std::string, std::string>& defines)
{
	std::vector<uint32_t> result;
	bool flag = ProcessCodeToSPIRV(sourceCode, result, type, defines);
	return { result,flag };
}

std::tuple<std::vector<uint32_t>, bool> Pipeline::ReadSPIRVBinaryPath(const std::string& path)
{
	return { std::vector<uint32_t>(),false };
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

void BindingRecord::SetUniformTexture3D(const std::shared_ptr<Texture3D>& uniformTex, vk::ImageAspectFlags aspect, uint32_t binding, uint32_t set)
{
	SetUniformTexture3D(uniformTex, aspect, BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetStorageImage3D(const std::shared_ptr<Texture3D>& storageImage, vk::ImageAspectFlags aspect, uint32_t binding, uint32_t set)
{
	SetStorageImage3D(storageImage, aspect, BindingPoint{ .binding = binding, .set = set });
}

void BindingRecord::SetUniformTexture3D(const std::shared_ptr<Texture3D>& uniformTex, vk::ImageAspectFlags aspect, const BindingPoint& bp)
{
	if (!uniformTex)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::UniformTex3D, .data = UniformTexture3DEntry{.texture = uniformTex, .aspect = aspect} };
	m_bindingData[bp] = entry;
}

void BindingRecord::SetStorageImage3D(const std::shared_ptr<Texture3D>& storageImage, vk::ImageAspectFlags aspect, const BindingPoint& bp)
{
	if (!storageImage)
		return;
	BindingEntry entry{ .type = BindingEntry::DataType::StorageImage3D, .data = StorageImage3DEntry{.texture = storageImage, .aspect = aspect} };
	m_bindingData[bp] = entry;
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

void BindingRecord::SetShadowBindlessMaterialTexture(const std::shared_ptr<StorageBlock>& materials, const std::shared_ptr<ITextureArrayProvider>& textures)
{
	if (materials) SetStorageBlock(materials, GeneralBindingPoint::Shadow_Material_materials);
	if (textures) SetUniformTextureArray(textures, GeneralBindingPoint::Shadow_Material_textures);
}

void BindingRecord::SetLightStorageData(
	const std::shared_ptr<StorageBlock>& _ssbo_dirLightMeta,
	const std::shared_ptr<StorageBlock>& _ssbo_dirLightCascade,
	const std::shared_ptr<StorageBlock>& _ssbo_pointLightMeta,
	const std::shared_ptr<StorageBlock>& _ssbo_spotLightMeta,
	const std::shared_ptr<StorageBlock>& _ssbo_dirLightCascadeDistances
)
{
	if (_ssbo_dirLightMeta) SetStorageBlock(_ssbo_dirLightMeta, GeneralBindingPoint::Light_DirLightMetaData);
	if (_ssbo_dirLightCascade) SetStorageBlock(_ssbo_dirLightCascade, GeneralBindingPoint::Light_DirLightCascadeData);
	if (_ssbo_pointLightMeta) SetStorageBlock(_ssbo_pointLightMeta, GeneralBindingPoint::Light_PointLightMetaData);
	if (_ssbo_spotLightMeta) SetStorageBlock(_ssbo_spotLightMeta, GeneralBindingPoint::Light_SpotLightMetaData);
	if (_ssbo_dirLightCascadeDistances) SetStorageBlock(_ssbo_dirLightCascadeDistances, GeneralBindingPoint::Light_DirLightCascadeDistanceData);
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
	else if (entry.type == BindingEntry::DataType::UniformTexArray)
	{
		UniformTextureArrayEntry* dataptr = std::get_if<UniformTextureArrayEntry>(&entry.data);
		if (!dataptr) return;
		BindUniformTextureArray(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::UniformTexCube)
	{
		UniformTextureCubeEntry* dataptr = std::get_if<UniformTextureCubeEntry>(&entry.data);
		if (!dataptr) return;
		BindUniformTextureCube(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
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
	else if (entry.type == BindingEntry::DataType::StorageImage2DArray)
	{
		StorageImage2DArrayEntry* dataptr = std::get_if<StorageImage2DArrayEntry>(&entry.data);
		if (!dataptr) return;
		BindStorageImage2DArray(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::StorageImageCube)
	{
		StorageImageCubeEntry* dataptr = std::get_if<StorageImageCubeEntry>(&entry.data);
		if (!dataptr) return;
		BindStorageImageCube(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::AccelerationStructure)
	{
		AccelerationStructureEntry* dataptr = std::get_if<AccelerationStructureEntry>(&entry.data);
		if (!dataptr) return;
		BindAccelerationStructure(*dataptr, data, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::UniformTex3D)
	{
		UniformTexture3DEntry* dataptr = std::get_if<UniformTexture3DEntry>(&entry.data);
		if (!dataptr) return;
		BindUniformTexture3D(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
	}
	else if (entry.type == BindingEntry::DataType::StorageImage3D)
	{
		StorageImage3DEntry* dataptr = std::get_if<StorageImage3DEntry>(&entry.data);
		if (!dataptr) return;
		BindStorageImage3D(cmdBuffer, data, bindStage, *dataptr, point.binding, point.set);
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
	texrure->TransitionLayout(cmdBuffer, &imageLayout, bindStage, ImageLayout::BindUsage::Sample);

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
		// 采样读：光追 / 计算 Pass 绑 bindless 时不再把整组纹理切到 GENERAL、下一个图形 Pass 再切回只读
		vk::ImageLayout imageLayout = desc.image->TransitionLayoutForSample(cmdBuffer, bindStage);

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

void BindingRecord::BindUniformTextureCube(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, UniformTextureCubeEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.texture)
	{
		if (entry.descEntry.version != entry.texture->GetDescBindEntryVersion())
			entry.descEntry = entry.texture->GetCubeDescBindEntry(entry.aspect);
	}

	auto& texrure = entry.texture;
	auto& imageView = entry.descEntry.imageView;
	auto& sampler = entry.descEntry.sampler;

	if (!texrure || !imageView || !sampler)
		return;

	vk::ImageLayout imageLayout;
	texrure->TransitionLayout(cmdBuffer, &imageLayout, bindStage, ImageLayout::BindUsage::Sample);

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

void BindingRecord::BindStorageImage2DArray(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, StorageImage2DArrayEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.image2DArray)
	{
		if (entry.descEntry.version != entry.image2DArray->GetDescBindEntryVersion())
			entry.descEntry = entry.image2DArray->GetDescBindEntry(entry.aspect, entry.baseLevel, entry.levelCount);
	}

	auto& texrure = entry.image2DArray;
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
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setImageInfo(imageInfo);

	VKCONTEXT->GetDeviceHandle().updateDescriptorSets(write, nullptr);
}

void BindingRecord::BindStorageImageCube(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, StorageImageCubeEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.imageCube)
	{
		if (entry.descEntry.version != entry.imageCube->GetDescBindEntryVersion())
			entry.descEntry = entry.imageCube->GetCubeDescBindEntry(entry.aspect);
	}

	auto& texrure = entry.imageCube;
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
		.setDescriptorType(vk::DescriptorType::eStorageImage)
		.setImageInfo(imageInfo);

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

void BindingRecord::BindUniformTexture3D(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, UniformTexture3DEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.texture)
	{
		if (entry.descEntry.version != entry.texture->GetDescBindEntryVersion())
			entry.descEntry = entry.texture->GetDescBindEntry(entry.aspect);
	}

	auto& texture = entry.texture;
	auto& imageView = entry.descEntry.imageView;
	auto& sampler = entry.descEntry.sampler;

	if (!texture || !imageView || !sampler)
		return;

	vk::ImageLayout imageLayout;
	texture->TransitionLayout(cmdBuffer, &imageLayout, bindStage, ImageLayout::BindUsage::Sample);

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

void BindingRecord::BindStorageImage3D(const std::shared_ptr<VKWrapper::VKCommandBuffer>& cmdBuffer, std::shared_ptr<Pipeline::DescriptorSetGroup>& data, ImageLayout::BindStage& bindStage, StorageImage3DEntry& entry, uint32_t binding, uint32_t set)
{
	if (!cmdBuffer)
		return;

	if (entry.texture)
	{
		if (entry.descEntry.version != entry.texture->GetDescBindEntryVersion())
			entry.descEntry = entry.texture->GetDescBindEntry(entry.aspect, entry.baseLevel, entry.levelCount);
	}

	auto& texture = entry.texture;
	auto& imageView = entry.descEntry.imageView;
	auto& sampler = entry.descEntry.sampler;

	if (!texture || !imageView || !sampler)
		return;

	vk::ImageLayout imageLayout;
	texture->TransitionLayout(cmdBuffer, &imageLayout, bindStage, ImageLayout::BindUsage::Write, entry.baseLevel);

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