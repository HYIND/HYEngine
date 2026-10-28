#include "SceneCodec/ComponentCodec.h"
#include "SceneCodec/BuiltInCodecs.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;
using namespace NComponentCodec;

std::vector<ComponentCodec>& NComponentCodec::GetComponentCodecs()
{
	static std::vector<ComponentCodec> codecs = NBuiltInCodecs::GenerateBuiltInCodecs();
	return codecs;
}

void NComponentCodec::RegisterCodec(ComponentCodec codec)
{
	auto& codecs = GetComponentCodecs();
	if (codec.moduleId.empty())
		codec.moduleId = "engine";
	auto existing = std::find_if(codecs.begin(), codecs.end(), [&](const ComponentCodec& item) {
		return item.type == codec.type || item.componentType == codec.componentType;
		});
	if (existing != codecs.end())
	{
		if (existing->type != codec.type || existing->componentType != codec.componentType
			|| existing->nativeTypeName != codec.nativeTypeName)
			throw std::runtime_error("Scene component identity collision between '" + existing->type + "' (" + existing->nativeTypeName + ") and '" + codec.type + "' (" + codec.nativeTypeName + ")'");
		*existing = std::move(codec);
	}
	else
		codecs.push_back(std::move(codec));
}

namespace
{
	class HostCodecRegistry final : public NComponentCodec::IComponentCodecRegistry
	{
	public:
		void RegisterCodec(NComponentCodec::ComponentCodec codec) override
		{
			NComponentCodec::RegisterCodec(std::move(codec));
		}
	};
}

NComponentCodec::IComponentCodecRegistry& NComponentCodec::GetHostComponentCodecRegistry()
{
	static HostCodecRegistry registry;
	return registry;
}

void NComponentCodec::UnregisterCodecsByModule(const std::string& moduleId)
{
	auto& codecs = GetComponentCodecs();
	std::erase_if(codecs, [&](const ComponentCodec& codec) { return codec.moduleId == moduleId; });
}

struct NComponentCodec::EngineModuleManager::Impl
{
	struct LoadedModule
	{
		void* handle = nullptr;
		const EngineModuleAPI* api = nullptr;
		std::string id;
		std::string moduleFileName;
	};
	ModuleHost host;
	void* hostServices;
	std::vector<LoadedModule> modules;
	explicit Impl(ModuleHost h, void* s) : host(h), hostServices(s) {}
};

NComponentCodec::EngineModuleManager::EngineModuleManager(ModuleHost host, void* hostServices)
	: m_impl(std::make_unique<Impl>(host, hostServices)) {}

NComponentCodec::EngineModuleManager::~EngineModuleManager() { UnloadAll(); }

bool NComponentCodec::EngineModuleManager::LoadModule(const fs::path& path, std::string* error)
{
#ifdef _WIN32
	const auto handle = LoadLibrary(path.c_str());
	if (!handle) {
		if (error) *error = "LoadLibrary failed for " + path.string();
		return false;
	}

	auto getApi = reinterpret_cast<GetEngineModuleAPIFn>(GetProcAddress(handle, "GetEngineModuleAPI"));
	if (!getApi) {
		FreeLibrary(handle);
		if (error) *error = "Module does not export GetEngineModuleAPI";
		return false;
	}

	const auto* api = getApi(EngineModuleApiVersion);
	if (!api
		|| api->apiVersion != EngineModuleApiVersion
		|| !api->moduleId
		|| !*api->moduleId
		|| !api->registerComponentCodecs
		|| !api->registerSystems)
	{
		FreeLibrary(handle); if (error) *error = "Invalid or incompatible Engine Module API"; return false;
	}

	static auto IsDebugPair = [](const std::string& a, const std::string& b) -> bool
		{
			fs::path pa(a), pb(b);

			std::string extA = pa.extension().string();
			std::string extB = pb.extension().string();

			static auto toLower = [](std::string& s) {
				std::transform(s.begin(), s.end(), s.begin(),
					[](unsigned char c) {
						return static_cast<char>(std::tolower(c));
					});
				};

			toLower(extA);
			toLower(extB);

			if (extA != extB)
				return false;

			std::string nameA = pa.stem().string();
			std::string nameB = pb.stem().string();

			static auto isEndAssociatedWith = [](const std::string& nameA, const std::string& nameB, const std::string& suffix) {
				if (suffix.empty())
					return nameA == nameB;
				return nameA + suffix == nameB || nameB + suffix == nameA;
				};

			return isEndAssociatedWith(nameA, nameB, "_d")
				|| isEndAssociatedWith(nameA, nameB, "_debug")
				|| isEndAssociatedWith(nameA, nameB, "_Debug")
				|| isEndAssociatedWith(nameA, nameB, "_DEBUG")
				|| isEndAssociatedWith(nameA, nameB, "D")
				|| isEndAssociatedWith(nameA, nameB, "d");
		};

	auto it = std::find_if(
		m_impl->modules.begin(),
		m_impl->modules.end(),
		[&](const auto& m) {
			return m.id == api->moduleId;
		});

	if (it != m_impl->modules.end()) {
		FreeLibrary(handle);
		auto& existModule = *it;
		if (IsDebugPair(existModule.moduleFileName, path.filename().string()))
		{
			return true;
		}
		else 
		{
			if (error) *error = "Module ID is already loaded";
			return false;
		}
	}

	if (std::any_of(m_impl->modules.begin(), m_impl->modules.end(),
		[&](const auto& m) { return m.id == api->moduleId; }))
	{
		FreeLibrary(handle);
		if (error) *error = "Module ID is already loaded"; return false;
	}

	class ModuleCodecRegistry final : public NComponentCodec::IComponentCodecRegistry
	{
		NComponentCodec::IComponentCodecRegistry& host;
		std::string moduleId;
	public:
		ModuleCodecRegistry(NComponentCodec::IComponentCodecRegistry& h, std::string id)
			: host(h), moduleId(std::move(id)) {}
		void RegisterCodec(NComponentCodec::ComponentCodec codec) override
		{
			codec.moduleId = moduleId;
			host.RegisterCodec(std::move(codec));
		}
	};

	ModuleCodecRegistry registry(NComponentCodec::GetHostComponentCodecRegistry(), api->moduleId);
	EngineModuleContext context{ m_impl->host, api->moduleId, m_impl->hostServices };

	try
	{
		api->registerComponentCodecs(registry, context);
		if (api->initialize) api->initialize(context);
	}
	catch (const std::exception& e)
	{
		UnregisterCodecsByModule(api->moduleId); FreeLibrary(handle); if (error) *error = e.what(); return false;
	}

	m_impl->modules.push_back({ handle, api, api->moduleId, path.filename().string() });
	return true;

#else
	(void)path; if (error) *error = "Dynamic module loading is currently implemented for Windows only"; return false;
#endif
}

void NComponentCodec::EngineModuleManager::LoadModulesFromDirectory(const fs::path& directory)
{
	if (!fs::exists(directory)) 
		return;

	for (const auto& entry : fs::directory_iterator(directory))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".dll") continue;
		std::string error;
		if (!LoadModule(entry.path(), &error))
			std::cout << std::format("error when LoadModule with path = {}, error = {}", entry.path().string(), error);
	}
}

void NComponentCodec::EngineModuleManager::RegisterSystems(World& world)
{
	for (auto& module : m_impl->modules)
	{
		EngineModuleContext context{ m_impl->host, module.id.c_str(), m_impl->hostServices };
		module.api->registerSystems(world, context);
	}
}

void NComponentCodec::EngineModuleManager::UnloadAll()
{
	if (!m_impl) return;
	for (auto it = m_impl->modules.rbegin(); it != m_impl->modules.rend(); ++it)
	{
		EngineModuleContext context{ m_impl->host, it->id.c_str(), m_impl->hostServices };
		if (it->api->shutdown) it->api->shutdown(context);
		UnregisterCodecsByModule(it->id);
#ifdef _WIN32
		if (it->handle) FreeLibrary((HMODULE)it->handle);
#endif
	}
	m_impl->modules.clear();
}

std::vector<std::string> NComponentCodec::EngineModuleManager::GetLoadedModuleIds() const
{
	std::vector<std::string> ids;
	for (const auto& module : m_impl->modules) ids.push_back(module.id);
	return ids;
}
