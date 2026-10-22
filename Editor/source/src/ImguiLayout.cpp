#include "ImguiLayout.h"

#include "Helper/FileIO.h"
#include "glm/gtc/type_ptr.hpp"
#include "CommonComponent.h"
#include "PropertiesHelper.h"
#include "IconManager.h"
#include <filesystem>
#include <cmath>
#include "Coroutine.h"
#include "EditorTags.h"

namespace fs = std::filesystem;

ImGuizmo::OPERATION ImguiLayout::mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
ImGuizmo::MODE ImguiLayout::mCurrentGizmoMode = ImGuizmo::WORLD;
bool ImguiLayout::useSnap = false;
float ImguiLayout::snap[3] = { 0.1f, 0.1f, 0.1f };
Entity ImguiLayout::selectedEntity = Entity();

char ImguiLayout::m_newProjectPathBuffer[1000];
char ImguiLayout::m_newProjectName[1000];

std::string ImguiLayout::m_pendingAction;

char ImguiLayout::m_wantOpenProjectPathBuffer[1000];

bool ImguiLayout::s_openSaveConfirmPopup = false;
bool ImguiLayout::s_openNewProjectPopup = false;

std::string ImguiLayout::m_selectedFolder;

AssetImportPopup ImguiLayout::s_assetImportPopup;
AssetGUID ImguiLayout::m_currentSceneAssetGuid;
static AssetGUID s_materialEditorGuid;
static bool s_materialEditorOpen = false;
struct MaterialOrigin
{
	AssetGUID guid;
	MaterialProperties properties;
	std::map<TextureType, AssetGUID> textures;
	bool valid = false;
};
static MaterialOrigin s_materialOrigin;
static std::string s_sceneSaveErrorMessage;

static bool OpenProjectAndLoadDefaultScene(const std::string& filePath, ProjectManager* projectManager, WorldManager* worldManager)
{
	if (!projectManager || !worldManager || !projectManager->OpenProject(filePath))
		return false;

	ImguiLayout::m_currentSceneAssetGuid.clear();
	ImguiLayout::selectedEntity = Entity();
	const AssetGUID defaultSceneGUID = projectManager->GetDefaultSceneGUID();
	if (!defaultSceneGUID.empty())
	{
		auto database = projectManager->GetDatabase();
		auto meta = database ? database->GetAssetMetaByGuid(defaultSceneGUID) : nullptr;
		if (meta && meta->type == AssetType::Scene)
		{
			auto asset = projectManager->LoadAsset(defaultSceneGUID);
			auto scene = asset ? asset->AsScene() : nullptr;
			if (scene && worldManager->LoadFromSceneCodec(scene, projectManager))
			{
				ImguiLayout::m_currentSceneAssetGuid = defaultSceneGUID;
				return true;
			}
		}
	}

	worldManager->InitDefaultWorld();
	return true;
}

void ImguiLayout::OpenMaterialEditor(const AssetGUID& guid)
{
	if (s_materialEditorOpen && s_materialEditorGuid != guid)
	{
		auto currentAsset = ProjectManager::Instance()->LoadAsset(s_materialEditorGuid);
		auto currentMaterial = currentAsset ? std::dynamic_pointer_cast<MaterialAsset>(currentAsset) : nullptr;
		if (currentMaterial && s_materialOrigin.valid && s_materialOrigin.guid == s_materialEditorGuid)
		{
			currentMaterial->SetProperties(s_materialOrigin.properties);
			std::vector<TextureType> currentTextures;
			for (const auto& [type, textureGuid] : currentMaterial->GetTextureDependencies())
				currentTextures.push_back(type);
			for (TextureType type : currentTextures)
				currentMaterial->SetTextureDependency(type, {}, nullptr);
			for (const auto& [type, textureGuid] : s_materialOrigin.textures)
			{
				auto textureAsset = ProjectManager::Instance()->LoadAsset(textureGuid);
				auto texture = textureAsset ? textureAsset->AsTexture() : nullptr;
				if (texture)
					currentMaterial->SetTextureDependency(type, textureGuid, texture);
			}
		}
	}
	const bool isNewSession = !s_materialEditorOpen || s_materialEditorGuid != guid;
	s_materialEditorGuid = guid;
	s_materialEditorOpen = true;
	if (isNewSession)
		s_materialOrigin.valid = false;
}

bool ImguiLayout::SaveCurrentScene(ProjectManager* projectManager)
{
	if (!projectManager || !projectManager->IsProjectOpen())
		return false;
	auto worldManager = WorldManager::Instance();
	auto world = worldManager ? worldManager->GetWorld() : nullptr;
	if (!world)
		return false;
	WorldManager::RegisterSceneComponentCodecs();
	auto serializeCurrentWorld = [world](SceneCodec& scene)
		{
			auto serialize = [&scene](World& activeWorld)
				{
					const bool serialized = scene.Serialize(activeWorld, [](Entity entity) {
						if (auto follow = entity.tryGetComponent<CameraFollow>())
						{
							if (follow->target && follow->target.hasComponent<TagEditorCamera>())
								return false;
						}
						return !entity.hasComponent<TagEditorCamera>();
						});
					if (!serialized)
						return false;

					auto& settings = scene.GetSettings();
					settings.erase("editorFreeCamera");
					for (Entity entity : activeWorld.getAllEntity())
					{
						if (!entity.hasComponents<CameraComponent, Transform, CameraFollow>())
							continue;

						auto& follow = entity.getComponent<CameraFollow>();
						Entity freeEntity = follow.target;
						if (!freeEntity || !freeEntity.hasComponents<TagFreeCamera, TagCurrentControl, Transform, PlayerInput, Controller, TagEditorCamera>())
							continue;

						const auto& camera = entity.getComponent<CameraComponent>();
						const auto& cameraTransform = entity.getComponent<Transform>();
						const auto& controller = freeEntity.getComponent<Controller>();
						const auto& freeCamera = freeEntity.getComponent<TagFreeCamera>();
						const auto finiteVec3 = [](const glm::vec3& value) {
							return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
							};
						const auto finiteQuat = [](const glm::quat& value) {
							return std::isfinite(value.w) && std::isfinite(value.x)
								&& std::isfinite(value.y) && std::isfinite(value.z);
							};
						if (!finiteVec3(cameraTransform.position) || !finiteVec3(cameraTransform.scale)
							|| !finiteQuat(cameraTransform.rotation)
							|| !finiteVec3(follow.offset)
							|| !std::isfinite(controller.yaw) || !std::isfinite(controller.pitch)
							|| !std::isfinite(freeCamera.velocity)
							|| !std::isfinite(camera.fov) || !std::isfinite(camera.nearPlane)
							|| !std::isfinite(camera.farPlane))
							return false;

						settings["editorFreeCamera"] = {
							{"yaw", controller.yaw},
							{"pitch", controller.pitch},
							{"velocity", freeCamera.velocity},
							{"cameraTransform", {
								{"position", { cameraTransform.position.x, cameraTransform.position.y, cameraTransform.position.z }},
								{"rotation", { cameraTransform.rotation.w, cameraTransform.rotation.x, cameraTransform.rotation.y, cameraTransform.rotation.z }},
								{"scale", { cameraTransform.scale.x, cameraTransform.scale.y, cameraTransform.scale.z }}
							}},
							{"fov", camera.fov},
							{"nearPlane", camera.nearPlane},
							{"farPlane", camera.farPlane},
							{"offset", { follow.offset.x, follow.offset.y, follow.offset.z }}
						};
						break;
					}
					return true;
				};

			try
			{
				if (!world->isRunning())
					return serialize(*world);

				bool result = false;
				world->SubmitCommand([&](World& activeWorld) {
					result = serialize(activeWorld);
					}).get();
				return result;
			}
			catch (const std::exception&)
			{
				return false;
			}
		};

	if (!m_currentSceneAssetGuid.empty())
	{
		auto database = projectManager->GetDatabase();
		auto meta = database ? database->GetAssetMetaByGuid(m_currentSceneAssetGuid) : nullptr;
		if (meta && meta->type == AssetType::Scene)
		{
			if (auto asset = projectManager->LoadAsset(m_currentSceneAssetGuid))
			{
				if (auto scene = asset->AsScene())
				{
					if (!serializeCurrentWorld(*scene))
					{
						s_sceneSaveErrorMessage = "无法序列化当前场景。";
						return false;
					}

					bool result = projectManager->SaveAsset(meta->guid);

					if (!result)
						s_sceneSaveErrorMessage = "无法写入当前场景资产文件。";

					return result;
				}
			}
		}
	}
	else
	{
		std::string filePath;
		if (!FileIO::SaveOneFile(
			filePath,
			"Untitled.scene",
			"scene",
			{ {"场景文件", "scene"} },
			projectManager->GetProjectFolderFullPath())
			)
			return false;

		SceneCodec scene;
		scene.SetName(fs::path(filePath).stem().string());
		if (!serializeCurrentWorld(scene))
		{
			s_sceneSaveErrorMessage = "无法序列化当前场景。";
			return false;
		}
		AssetMeta meta;
		if (!projectManager->CreateSceneAsset(filePath, scene, &meta))
		{
			s_sceneSaveErrorMessage = "保存失败。请确认所选路径位于项目根目录内，且未与其他类型的资产冲突。";
			return false;
		}

		m_currentSceneAssetGuid = meta.guid;
		return true;
	}

}

// 新建文件夹
bool        m_showNewFolderPopup = false;
char        m_newFolderBuffer[256] = {};
std::string m_newFolderParentPath;

// 重命名
bool        m_showRenamePopup = false;
char        m_renameBuffer[256] = {};
std::string m_renameTarget;
bool        m_renameIsFolder = false;


static bool WantToAddDirLight = false;
static bool WantToAddPointLight = false;
static bool WantToAddSpotLight = false;
static bool WantToAddCube = false;
static bool WantToAddSphere = false;
static bool WantToAddEmptyEntity = false;

extern VkDevice g_Device;


const char* GetOpName(ImGuizmo::OPERATION operation)
{
	// 注意：OPERATION 是位标志，需要按位检查
	if (operation == ImGuizmo::TRANSLATE)
		return "平移";
	if (operation == ImGuizmo::ROTATE)
		return "旋转";
	if (operation == ImGuizmo::SCALE)
		return "缩放";
	if (operation == ImGuizmo::BOUNDS)
		return "Bounds";
	if (operation == ImGuizmo::UNIVERSAL)
		return "Universal";

	std::string combined;
	if (operation & ImGuizmo::TRANSLATE) combined += "T";
	if (operation & ImGuizmo::ROTATE) combined += "R";
	if (operation & ImGuizmo::SCALE) combined += "S";

	static std::string result;
	result = combined.empty() ? "Unknown" : combined;
	return result.c_str();
}

const char* GetModeName(ImGuizmo::MODE mode)
{
	if (mode == ImGuizmo::LOCAL)
		return "Local";
	if (mode == ImGuizmo::WORLD)
		return "World";

	return "Unknown";
}

static btVector3 GlmToBullet(const glm::vec3& v) {
	return btVector3(v.x, v.y, v.z);
}

static btQuaternion GlmToBullet(const glm::quat& q) {
	return btQuaternion(q.x, q.y, q.z, q.w);
}

void ImguiLayout::DrawFolderTree(const std::string& currentPathStr, const std::string& parentPathStr, const std::string& rootPathStr)
{
	fs::path currentPath(currentPathStr);
	fs::path parentPath(parentPathStr);
	fs::path rootPath(rootPathStr);

	if (!Tool::IsSubDirectory(currentPath, parentPath) || !Tool::IsSubDirectory(currentPath, rootPath))
		return;

	fs::path relativeParentPath = fs::relative(currentPath, parentPath);
	fs::path relativeRootPath = fs::relative(currentPath, rootPath);

	if (m_selectedFolder.empty())
		m_selectedFolder = ".";

	bool isSelected = (m_selectedFolder == relativeRootPath.string());

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_OpenOnDoubleClick |
		ImGuiTreeNodeFlags_SpanAvailWidth;
	if (isSelected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	auto relativePathStr = relativeParentPath.string();
	if (relativeParentPath.string().empty() || relativeParentPath.string() == ".")
		relativePathStr = currentPath.filename().string();

	bool isSetOpen = Tool::IsSubDirectory(m_selectedFolder, relativeRootPath, rootPath) && m_selectedFolder != relativeRootPath.string();
	if (isSetOpen) ImGui::SetNextItemOpen(true);
	bool isExpanded = ImGui::TreeNodeEx(relativePathStr.c_str(), flags);

	if (ImGui::IsItemClicked()) {
		m_selectedFolder = relativeRootPath.string();
	}

	if (isSetOpen || isExpanded) {
		// 递归绘制子文件夹
		std::vector<std::string> folders;

		try {
			for (const auto& entry : fs::directory_iterator(currentPath)) {
				if (entry.is_directory()) {
					folders.push_back(entry.path().filename().string());
				}
			}
		}
		catch (...) {
			return;
		}

		std::sort(folders.begin(), folders.end());
		for (const auto& folder : folders) {
			std::filesystem::path childPath = currentPath / folder;
			DrawFolderTree(childPath.string(), currentPathStr, rootPathStr);
		}
		if (isExpanded)
			ImGui::TreePop();
	}
}

void ImguiLayout::DrawFolderContent(ProjectManager* projectManager, const std::string& currentPathStr)
{
	std::filesystem::path currentPath(currentPathStr);

	std::vector<std::string> folders;
	std::vector<std::string> files;

	try {
		for (const auto& entry : fs::directory_iterator(currentPath)) {
			std::string name = entry.path().filename().string();
			std::u8string utf8Name = entry.path().filename().u8string();
			std::string displayName(reinterpret_cast<const char*>(utf8Name.data()), utf8Name.size());
			if (entry.is_directory()) {
				folders.push_back(displayName);
			}
			else {
				files.push_back(displayName);
			}
		}
	}
	catch (...) {
		return;
	}

	std::unordered_map<std::string, AssetMeta> metas;
	if (Tool::IsSubDirectory(currentPath, projectManager->GetProjectFolderFullPath()))
	{
		fs::path relativeAssetFolderPath = fs::relative(currentPath, projectManager->GetProjectFolderFullPath());
		auto m = projectManager->GetAssetMetasInFolder(relativeAssetFolderPath.string());

		std::for_each(std::execution::seq, m.begin(), m.end(),
			[&](AssetMeta& meta) -> void {
				auto filename = fs::path(meta.path).filename().string();
				metas[filename] = std::move(meta);
			});
	}

	std::sort(folders.begin(), folders.end());
	std::sort(files.begin(), files.end(),
		[&](const std::string& file_a, const std::string& file_b) -> bool
		{
			auto it1 = metas.find(file_a);
			auto it2 = metas.find(file_b);
			bool isAsset_a = it1 != metas.end();
			bool isAsset_b = it2 != metas.end();

			if (isAsset_a != isAsset_b)
			{
				if (isAsset_a) return true;
				if (isAsset_b) return false;
			}
			else if (!isAsset_a)
			{
				return file_a < file_b;
			}
			else
			{
				return it1->second.type > it2->second.type;
			}
		}
	);

	struct Item {
		bool isFolder;
		bool isAssetFile;
		AssetMeta meta;
		AssetPath name;
	};
	std::vector<Item> items;
	items.resize(folders.size() + files.size());

	std::for_each(std::execution::par,
		std::views::enumerate(folders).begin(),
		std::views::enumerate(folders).end(),
		[&](const auto& pair) {
			const auto& [idx, folder] = pair;
			items[idx] = { true, false, {}, std::move(folder) };
		});

	std::for_each(std::execution::par,
		std::views::enumerate(files).begin(),
		std::views::enumerate(files).end(),
		[&](const auto& pair) {
			const auto& [idx, file] = pair;
			bool isAsset = metas.find(file) != metas.end();
			items[folders.size() + idx] = { false, isAsset, isAsset ? metas[file] : AssetMeta{}, std::move(file) };
		});

	// 网格布局
	float iconSize = 80.0f;
	float padding = 8.f;
	float textHeight = ImGui::GetFontSize() * 2;
	float windowWidth = ImGui::GetContentRegionAvail().x;
	float rowHeight = iconSize + textHeight + padding;

	int columns = std::max(1, (int)(windowWidth / (iconSize + padding)));

	int totalItems = items.size();
	int totalRows = (totalItems + columns - 1) / columns;


	ImGui::Columns(columns, nullptr, false);

	auto* iconMgr = IconManager::Get();

	bool anyItemHovered = false;

	// ---- 用于删除/重命名等操作的延迟处理（避免在遍历时修改目录） ----
	std::string pendingDeletePath;   // 待删除的完整路径
	AssetGUID   pendingDeleteGuid;   // 若是资产，记录 guid
	bool        pendingDeleteIsAsset = false;
	std::string pendingDeleteName;   // 用于日志

	auto DrawFolder = [&](const Item& item) -> void
		{
			ImGui::PushID(item.name.c_str());

			ImVec2 iconPos = ImGui::GetCursorScreenPos();
			ImVec2 iconRect(iconSize, iconSize);

			ImGui::InvisibleButton("##icon_area", iconRect);

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImTextureID folderIcon = iconMgr->GetFolderIcon();
			if (folderIcon != 0) {
				drawList->AddImage(
					folderIcon,
					ImVec2(iconPos.x + iconSize * 0.1, iconPos.y + iconSize * 0.1),
					ImVec2(iconPos.x + iconSize * 0.9, iconPos.y + iconSize * 0.9)
				);
			}

			bool isHovered = ImGui::IsItemHovered();
			bool isDbClicked = isHovered && ImGui::IsMouseDoubleClicked(0);
			bool isRightClicked = isHovered && ImGui::IsMouseClicked(1);

			if (isHovered)
			{
				anyItemHovered = true;
				drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
					IM_COL32(70, 130, 200, 150));
			}

			auto fullFolderPath = fs::weakly_canonical(currentPath / item.name);

			if (isDbClicked)
			{
				fs::path relativeRootPath = fs::relative(fullFolderPath, fs::path(projectManager->GetProjectFolderFullPath()));
				m_selectedFolder = relativeRootPath.string();
			}

			if (isRightClicked) {
			}

			if (ImGui::BeginPopupContextItem("folder_context_menu"))
			{
				if (ImGui::MenuItem("打开")) {
					fs::path relativeRootPath = fs::relative(fullFolderPath, fs::path(projectManager->GetProjectFolderFullPath()));
					m_selectedFolder = relativeRootPath.string();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("复制路径")) {
					ImGui::SetClipboardText(fullFolderPath.string().c_str());
				}
				if (ImGui::MenuItem("复制文件夹名")) {
					ImGui::SetClipboardText(item.name.c_str());
				}

				ImGui::Separator();

				if (ImGui::MenuItem("重命名")) {
					m_renameTarget = fullFolderPath.string();
					m_renameIsFolder = true;
					strncpy_s(m_renameBuffer, item.name.c_str(), sizeof(m_renameBuffer) - 1);
					m_showRenamePopup = true;
				}

				if (ImGui::MenuItem("删除文件夹")) {
					// 延迟删除，避免在遍历/绘制时修改目录
					pendingDeletePath = fullFolderPath.string();
					pendingDeleteIsAsset = false;
					pendingDeleteGuid.clear();
					pendingDeleteName = item.name;
				}

				ImGui::EndPopup();
			}

			ImVec2 textPos = ImGui::GetCursorScreenPos();
			ImVec2 textSize = ImGui::CalcTextSize(item.name.c_str(), nullptr, false, iconSize);
			drawList->AddText(
				ImGui::GetFont(),
				ImGui::GetFontSize(),
				textPos,
				ImGui::GetColorU32(ImGuiCol_Text),
				item.name.c_str(),
				nullptr,
				iconSize,
				nullptr
			);
			ImGui::Dummy(ImVec2(iconSize, textSize.y));

			ImGui::NextColumn();
			ImGui::PopID();
		};

	auto DrawFile = [&](const Item& item) -> void {
		bool isAssetFile = item.isAssetFile;
		AssetMeta meta = item.meta;

		auto fullFilePath = fs::weakly_canonical(currentPath / item.name);

		ImGui::PushID(item.name.c_str());

		ImVec2 iconPos = ImGui::GetCursorScreenPos();
		ImVec2 iconRect(iconSize, iconSize);

		ImGui::InvisibleButton("##icon_area", iconRect);

		ImTextureID fileIcon = isAssetFile ?
			iconMgr->GetIconByAssetType(meta.type)
			: iconMgr->GetIconByFilePath(item.name);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (fileIcon != 0) {
			drawList->AddImage(
				fileIcon,
				ImVec2(iconPos.x + iconSize * 0.1, iconPos.y + iconSize * 0.1),
				ImVec2(iconPos.x + iconSize * 0.9, iconPos.y + iconSize * 0.9)
			);
		}

		bool isHovered = ImGui::IsItemHovered();
		bool isDbClicked = isHovered && ImGui::IsMouseDoubleClicked(0);
		bool isRightClicked = isHovered && ImGui::IsMouseClicked(1);

		if (isHovered)
		{
			anyItemHovered = true;
			drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
				IM_COL32(70, 130, 200, 150));
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				if (isAssetFile)
				{
					AssetGUID str = meta.guid;
					ImGui::SetDragDropPayload("ASSET_GUID", str.c_str(), str.size() + 1);
				}
				else
				{
					std::string str = fullFilePath.string();
					ImGui::SetDragDropPayload("ASSET_FILE", str.c_str(), str.size() + 1);
				}

				ImGui::Image(fileIcon, ImVec2(iconSize * 0.8, iconSize * 0.8));
				ImGui::Text("%s", item.name.c_str());
				ImGui::EndDragDropSource();
			}
		}

		if (isDbClicked)
		{
			if (isAssetFile && meta.type == AssetType::Material)
				OpenMaterialEditor(meta.guid);
		}

		if (isRightClicked) {
		}

		if (ImGui::BeginPopupContextItem("file_context_menu"))
		{
			if (ImGui::MenuItem("打开")) { /*OpenFile*/ }

			ImGui::Separator();

			if (ImGui::MenuItem("复制路径")) {
				ImGui::SetClipboardText(fullFilePath.string().c_str());
			}
			if (ImGui::MenuItem("复制文件名")) {
				ImGui::SetClipboardText(item.name.c_str());
			}

			ImGui::Separator();

			if (ImGui::MenuItem("重命名")) {
				m_renameTarget = fullFilePath.string();
				m_renameIsFolder = false;
				strncpy_s(m_renameBuffer, item.name.c_str(), sizeof(m_renameBuffer) - 1);
				m_showRenamePopup = true;
			}

			std::string deleteStr = isAssetFile ? "删除资产" : "删除文件";
			if (ImGui::MenuItem(deleteStr.c_str()))
			{
				pendingDeletePath = fullFilePath.string();
				pendingDeleteIsAsset = isAssetFile;
				pendingDeleteGuid = isAssetFile ? meta.guid : AssetGUID{};
				pendingDeleteName = item.name;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("属性")) { /*ShowFileProperties*/ }

			ImGui::EndPopup();
		}

		ImVec2 textPos = ImGui::GetCursorScreenPos();
		ImVec2 textSize = ImGui::CalcTextSize(item.name.c_str(), nullptr, false, iconSize);
		drawList->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize(),
			textPos,
			ImGui::GetColorU32(ImGuiCol_Text),
			item.name.c_str(),
			nullptr,
			iconSize,
			nullptr
		);
		ImGui::Dummy(ImVec2(iconSize, textSize.y));

		ImGui::NextColumn();
		ImGui::PopID();
		};


	// ==========================================================
	// 绘制网格内容
	// ==========================================================
	ImGuiListClipper clipper;
	clipper.Begin(totalRows, rowHeight);
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
		{
			int startIndex = row * columns;
			int endIndex = std::min(startIndex + columns, totalItems);

			for (int i = startIndex; i < endIndex; i++)
			{
				const auto& item = items[i];
				if (item.isFolder)
					DrawFolder(item);
				else
					DrawFile(item);
			}
		}
	}

	ImGui::Columns(1);

	bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

	if (windowHovered && !anyItemHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("blank_context_menu");
	}

	if (ImGui::BeginPopup("blank_context_menu"))
	{
		if (ImGui::MenuItem("新建文件夹"))
		{
			m_showNewFolderPopup = true;
			m_newFolderBuffer[0] = '\0';
			m_newFolderParentPath = currentPath.string();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("刷新")) {
			// 下一帧自动重新读取
		}

		ImGui::EndPopup();
	}

	// ==========================================================
	// 新建文件夹弹窗
	// ==========================================================
	if (m_showNewFolderPopup)
	{
		ImGui::OpenPopup("新建文件夹");
	}
	if (ImGui::BeginPopupModal("新建文件夹", &m_showNewFolderPopup, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("父目录: %s", m_newFolderParentPath.c_str());
		ImGui::Separator();

		ImGui::InputText("文件夹名", m_newFolderBuffer, sizeof(m_newFolderBuffer));

		bool valid = m_newFolderBuffer[0] != '\0';

		if (valid) {
			fs::path newPath = fs::path(m_newFolderParentPath) / m_newFolderBuffer;
			if (fs::exists(newPath)) {
				ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "已存在同名文件/文件夹");
				valid = false;
			}
		}

		if (valid && ImGui::Button("创建", ImVec2(120, 0)))
		{
			try {
				fs::create_directory(fs::path(m_newFolderParentPath) / m_newFolderBuffer);
				m_showNewFolderPopup = false;
				ImGui::CloseCurrentPopup();
			}
			catch (const fs::filesystem_error& e) {
				// 可以弹一个错误提示
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(120, 0))) {
			m_showNewFolderPopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	// ==========================================================
	// 重命名弹窗
	// ==========================================================
	if (m_showRenamePopup)
	{
		ImGui::OpenPopup("重命名");
	}
	if (ImGui::BeginPopupModal("重命名", &m_showRenamePopup, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("目标: %s", m_renameTarget.c_str());
		ImGui::Separator();

		ImGui::InputText("新名称", m_renameBuffer, sizeof(m_renameBuffer));

		bool valid = m_renameBuffer[0] != '\0';

		if (valid && ImGui::Button("确定", ImVec2(120, 0)))
		{
			try {
				fs::path oldPath(m_renameTarget);
				fs::path newPath = oldPath.parent_path() / m_renameBuffer;
				if (!fs::exists(newPath)) {
					fs::rename(oldPath, newPath);
				}
				m_showRenamePopup = false;
				ImGui::CloseCurrentPopup();
			}
			catch (const fs::filesystem_error&) {
				// 错误处理
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(120, 0))) {
			m_showRenamePopup = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	// ==========================================================
	// 延迟删除处理（遍历结束后执行）
	// ==========================================================
	if (!pendingDeletePath.empty())
	{
		try {
			auto path = fs::path(pendingDeletePath);
			if (fs::exists(path))
			{
				if (fs::is_directory(path))
				{
					projectManager->DeleteAssetsInFolder(path);
					fs::remove_all(path);
				}
				else
				{
					fs::remove_all(path);
					if (pendingDeleteIsAsset && !pendingDeleteGuid.empty())
						projectManager->DeleteAsset(pendingDeleteGuid);
				}
			}
		}
		catch (const fs::filesystem_error& e) {
			// 可以记录日志 / 弹窗
		}
	}
}

void ImguiLayout::DrawMainMenu(GLFWwindow* window, WorldManager* worldManager, ProjectManager* projectManager)
{

	auto NewProjectPopup = [&]()->void {
		ImGui::OpenPopup("NewProjectPopup");
		if (ImGui::BeginPopupModal("NewProjectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("创建新项目");
			ImGui::Separator();

			ImGui::Text("项目路径:");
			ImGui::SameLine();
			ImGui::InputText("##ProjectPath", m_newProjectPathBuffer, sizeof(m_newProjectPathBuffer));
			ImGui::SameLine();
			if (ImGui::Button("浏览...")) {
				std::string folderPath;
				if (FileIO::SelectFolder(folderPath))
				{
					memset(m_newProjectPathBuffer, 0, sizeof(m_newProjectPathBuffer));
					strcpy(m_newProjectPathBuffer, folderPath.c_str());
				}
			}

			ImGui::Text("项目名称:");
			ImGui::InputText("##ProjectName", m_newProjectName, sizeof(m_newProjectName));

			if (ImGui::Button("创建项目")) {
				if (strlen(m_newProjectName) > 0 && strlen(m_newProjectPathBuffer) > 0) {
					projectManager->CreateProject(m_newProjectPathBuffer, m_newProjectName);
					memset(m_newProjectName, 0, sizeof(m_newProjectName));
					ImGui::CloseCurrentPopup();
				}
				s_openNewProjectPopup = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("取消")) {
				memset(m_newProjectName, 0, sizeof(m_newProjectName));
				ImGui::CloseCurrentPopup();
				s_openNewProjectPopup = false;
			}
			ImGui::EndPopup();
		}
		};

	auto ExecutePendingAction = [&](GLFWwindow* window)-> void {
		if (m_pendingAction == "new") {
			s_openNewProjectPopup = true;
		}
		else if (m_pendingAction == "open") {
			std::string filePath;
			if (FileIO::OpenOneFile(filePath, { { "project",".project" } }))
			{
				OpenProjectAndLoadDefaultScene(filePath, projectManager, worldManager);
				m_selectedFolder.clear();
			}
		}
		else if (m_pendingAction == "exit") {
			glfwSetWindowShouldClose(window, true);
		}
		else if (m_pendingAction == "wantOpenProject") {
			std::string filePath(m_wantOpenProjectPathBuffer);
			if (!filePath.empty())
			{
				OpenProjectAndLoadDefaultScene(filePath, projectManager, worldManager);
				m_selectedFolder.clear();
			}
			memset(m_wantOpenProjectPathBuffer, 0, sizeof(m_wantOpenProjectPathBuffer));
		}
		m_pendingAction = "";
		};

	auto SaveConfirmPopup = [&]()->void {
		ImGui::OpenPopup("SaveConfirmPopup");
		if (ImGui::BeginPopupModal("SaveConfirmPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("当前项目有未保存的变更.");
			ImGui::Text("是否在继续操作前保存项目?");

			if (ImGui::Button("保存")) {
				projectManager->SaveProject();
				ImGui::CloseCurrentPopup();
				ExecutePendingAction(window);
				m_pendingAction = "";
				s_openSaveConfirmPopup = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("不保存")) {
				ImGui::CloseCurrentPopup();
				ExecutePendingAction(window);
				m_pendingAction = "";
				s_openSaveConfirmPopup = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("取消")) {
				ImGui::CloseCurrentPopup();
				m_pendingAction = "";
				s_openSaveConfirmPopup = false;
			}
			ImGui::EndPopup();
		}
		};

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("文件"))
		{
			// ===== New =====
			if (ImGui::MenuItem("新建项目", "Ctrl+N")) {
				if (projectManager->IsProjectOpen()) {
					m_pendingAction = "new";
					s_openSaveConfirmPopup = true;
				}
				else {
					s_openNewProjectPopup = true;
				}
			}

			// ===== Open =====
			if (ImGui::MenuItem("打开项目", "Ctrl+O")) {
				if (projectManager->IsProjectOpen()) {
					m_pendingAction = "open";
					s_openSaveConfirmPopup = true;
				}
				else {
					std::string filePath;
					if (FileIO::OpenOneFile(filePath, { { "project",".project" } }))
					{
						OpenProjectAndLoadDefaultScene(filePath, projectManager, worldManager);
						m_selectedFolder.clear();
					}
				}
			}

			// ===== Save（只有项目打开时才可用）=====
			if (ImGui::MenuItem("保存项目", "Ctrl+S", false, projectManager->IsProjectOpen())) {
				if (projectManager->IsProjectOpen()) {
					projectManager->SaveProject();
				}
				else
				{
					ImGui::OpenPopup("SaveTempProjectPopup");
				}
			}

			if (ImGui::MenuItem("导入资产", "Ctrl+I", false, projectManager->IsProjectOpen())) {
				if (projectManager->IsProjectOpen())
					s_assetImportPopup.SetVisiable(true);
			}

			ImGui::Separator();
			if (ImGui::MenuItem("关闭", "Alt+F4")) {
				if (projectManager->IsProjectOpen()) {
					m_pendingAction = "exit";
					s_openSaveConfirmPopup = true;
				}
				else {
					glfwSetWindowShouldClose(window, true);
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("编辑"))
		{
			if (ImGui::MenuItem("撤销", "Ctrl+Z")) {}
			if (ImGui::MenuItem("恢复", "Ctrl+Y")) {}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("场景"))
		{
			if (ImGui::MenuItem("保存场景", nullptr, false,
				worldManager->GetEditorState() == EditorWorldState::Editing && projectManager && projectManager->IsProjectOpen()))
				SaveCurrentScene(projectManager);
			const bool canSetDefaultScene = worldManager->GetEditorState() == EditorWorldState::Editing
				&& projectManager && projectManager->IsProjectOpen() && !m_currentSceneAssetGuid.empty();
			if (ImGui::MenuItem("设为默认场景", nullptr, false, canSetDefaultScene))
				projectManager->SetDefaultSceneGUID(m_currentSceneAssetGuid);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("窗口"))
		{
			ImGui::MenuItem("场景");
			ImGui::MenuItem("场景对象");
			ImGui::MenuItem("对象属性");
			ImGui::MenuItem("项目管理");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("添加"))
		{
			if (ImGui::BeginMenu("光源"))
			{
				if (ImGui::MenuItem("平行光"))
				{
					WantToAddDirLight = true;
				}
				if (ImGui::MenuItem("点光源"))
				{
					WantToAddPointLight = true;
				}
				if (ImGui::MenuItem("聚光"))
				{
					WantToAddSpotLight = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("几何体"))
			{
				if (ImGui::MenuItem("立方体"))
				{
					WantToAddCube = true;
				}
				if (ImGui::MenuItem("球体"))
				{
					WantToAddSphere = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("空实体"))
			{
				WantToAddEmptyEntity = true;
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	if (s_openSaveConfirmPopup) {
		SaveConfirmPopup();
	}
	if (s_openNewProjectPopup) {
		NewProjectPopup();
	}
	if (!s_sceneSaveErrorMessage.empty()) {
		ImGui::OpenPopup("SceneSaveErrorPopup");
		if (ImGui::BeginPopupModal("SceneSaveErrorPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted(s_sceneSaveErrorMessage.c_str());
			if (ImGui::Button("确定")) {
				s_sceneSaveErrorMessage.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	if (s_assetImportPopup.GetVisiable()) {
		s_assetImportPopup.DrawPopup(projectManager);
	}
}

void ImguiLayout::DrawPlayControls(WorldManager* worldManager, ProjectManager* projectManager)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 38.f));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 5.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	ImGui::Begin("##PlayControls", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
	const float controlsWidth = ImGui::CalcTextSize("Paused").x + ImGui::GetStyle().ItemSpacing.x
		+ ImGui::CalcTextSize("Resume").x + ImGui::GetStyle().FramePadding.x * 2.f
		+ ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 2.f;
	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - controlsWidth) * 0.5f);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(worldManager->GetEditorState() == EditorWorldState::Playing ? "Playing" :
		worldManager->GetEditorState() == EditorWorldState::Paused ? "Paused" : "Editing");
	ImGui::SameLine();
	if (worldManager->GetEditorState() == EditorWorldState::Editing)
	{
		if (ImGui::Button("Play"))
			worldManager->PlayWorld();
	}
	else if (worldManager->GetEditorState() == EditorWorldState::Playing)
	{
		if (ImGui::Button("Pause"))
			worldManager->PauseWorld();
	}
	else
	{
		if (ImGui::Button("Resume"))
			worldManager->ContinueWorld();
	}
	if (worldManager->GetEditorState() != EditorWorldState::Editing)
	{
		ImGui::SameLine();
		if (ImGui::Button("Stop"))
		{
			worldManager->StopPlayback(projectManager);
			selectedEntity = Entity();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void ImguiLayout::SetDockspace(bool firstFrame)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// 设置窗口位置和大小：为主菜单和播放控制条预留空间
	float menuBarHeight = ImGui::GetFrameHeight();
	float statusBarHeight = ImGui::GetFrameHeightWithSpacing();
	constexpr float playControlsHeight = 38.f;

	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight + playControlsHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight - playControlsHeight - statusBarHeight));

	ImGui::SetNextWindowViewport(viewport->ID);

	// 使用 PushStyleVar 让窗口背景完全透明
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	// 窗口标志
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	// 用 "##" 隐藏窗口名称，但仍然保留 ID
	ImGui::Begin("##MainDockSpaceContainer", nullptr, window_flags);
	ImGui::PopStyleVar(3);

	// 创建 Dockspace
	ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoTabBar);

	// ============ 3. 设置初始布局（仅第一帧） ============
	if (firstFrame)
	{
		// 清除已有布局
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight - playControlsHeight - statusBarHeight));

		ImGuiID dock_left, dock_right, dock_bottom_left, dock_bottom_right, dock_center, dock_left_bottom;


		// 分割布局
		ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.20f, &dock_left, &dock_right);
		ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Right, 0.25f, &dock_right, &dock_center);
		ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.30f, &dock_left_bottom, &dock_left);
		ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.25f, &dock_bottom_left, &dock_center);
		ImGui::DockBuilderSplitNode(dock_bottom_left, ImGuiDir_Right, 0.75f, &dock_bottom_right, &dock_bottom_left);


		// 停靠窗口
		ImGui::DockBuilderDockWindow("Scene Objects", dock_left);
		ImGui::DockBuilderDockWindow("Options", dock_left_bottom);
		ImGui::DockBuilderDockWindow("Properties", dock_right);
		ImGui::DockBuilderDockWindow("Folder Tree", dock_bottom_left);
		ImGui::DockBuilderDockWindow("Folder", dock_bottom_right);
		ImGui::DockBuilderDockWindow("Scene View", dock_center);

		ImGui::DockBuilderFinish(dockspace_id);
	}

	ImGui::End();  // 结束 Dockspace 容器窗口
}

void ImguiLayout::DrawSceneObjectList(WorldManager* worldManager)
{
	ImGui::Begin("Scene Objects");
	ImGui::Text("场景对象");
	ImGui::Separator();

	// 获取所有实体
	auto entities = worldManager->GetWorld()->getAllEntity();

	// 构建实体列表（缓存实体 ID 和名称）
	struct EntityItem {
		Entity entity;
		std::string name;
	};
	std::vector<EntityItem> items;
	items.reserve(entities.size());

	for (auto& entity : entities)
	{
		EntityItem item;
		item.entity = entity;

		if (auto tag = entity.tryGetComponent<NameTag>())
		{
			item.name = tag->name;
		}
		else
		{
			item.name = std::format("Unnamed_Entity_{}", entity.getId());
		}
		items.push_back(std::move(item));
	}

	static Entity renamingEntity;
	static uint32_t renameBufferSize = 256;
	static char renameBuffer[256] = "";
	static bool isRenaming = false;
	static float renamingStartTime = 0.0f;

	// 绘制列表
	for (auto& item : items)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
			ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth;  // 让整行可点击

		ImGui::PushID(item.entity.getId());

		// 高亮选中的实体
		bool isSelected = (selectedEntity && selectedEntity == item.entity);
		bool isShowRename = false;
		if (isSelected)
		{
			flags |= ImGuiTreeNodeFlags_Selected;

			// ========== 判断是否处于重命名状态 ==========
			isShowRename = isRenaming && renamingEntity == item.entity;
			if (isShowRename)
			{
				// 重命名模式：显示输入框
				// 自动聚焦
				ImGui::SetKeyboardFocusHere();
				if (ImGui::InputText("##RenameInput", renameBuffer, sizeof(renameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue))
				{
					// 按回车确认
					if (strlen(renameBuffer) > 0)
					{
						worldManager->GetWorld()->SubmitCommand([entity = item.entity, newName = std::string(renameBuffer)]() -> void {
							if (!entity) return;
							SetNameTag(entity, newName);
							});
					}
					isRenaming = false;
					renamingEntity = Entity();
				}

				// 按 ESC 取消
				if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					isRenaming = false;
					renamingEntity = Entity();
				}

				// 点击其他地方取消重命名
				if (!ImGui::IsItemActive() && !ImGui::IsItemHovered())
				{
					if (ImGui::GetTime() - renamingStartTime > 0.2f)
					{
						isRenaming = false;
						renamingEntity = Entity();
					}
				}
			}

		}

		if (!isShowRename)
		{
			// 使用实体 ID 作为树节点的唯一 ID
			ImGui::TreeNodeEx("##node", flags, "%s", item.name.c_str());

			// 处理点击选中
			if (ImGui::IsItemClicked())
			{
				selectedEntity = item.entity;
			}

			// 处理右键菜单（可选）
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				selectedEntity = item.entity;
				ImGui::OpenPopup("EntityContextMenu");
			}

			if (isSelected && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
			{
				// 填充当前名称
				strncpy_s(renameBuffer, item.name.c_str(), sizeof(renameBuffer) - 1);
				renamingEntity = selectedEntity;
				isRenaming = true;
				renamingStartTime = ImGui::GetTime();
			}

			// 右键菜单（可选功能）
			if (ImGui::BeginPopup("EntityContextMenu"))
			{
				if (ImGui::MenuItem("Delete"))
				{
					worldManager->GetWorld()->SubmitCommand([entity = item.entity, world = worldManager->GetWorld()]() -> void {
						if (!entity || !world)
							return;
						world->destroyEntity(entity);
						}
					);
				}
				if (ImGui::MenuItem("Duplicate"))
				{
					if (Entity newEntity; worldManager->DuplicateEntity(item.entity, newEntity) && newEntity)
						selectedEntity = newEntity;
				}

				ImGui::EndPopup();
			}
		}

		ImGui::PopID();
	}

	ImGui::End();
}

void ImguiLayout::DrawObjectProperties(ProjectManager* projectManager)
{
	ImGui::Begin("Properties");
	ImGui::Text("属性");
	ImGui::Separator();
	struct ComponentFactory
	{
		const char* name;
		std::function<bool(Entity)> has;
		std::function<void(Entity)> add;
	};
	static const std::vector<ComponentFactory> componentFactories = {
		{ "Transform", [](Entity entity) { return entity.hasComponent<Transform>(); }, [](Entity entity) { entity.addComponent<Transform>(); } },
		{ "Physics", [](Entity entity) { return entity.hasComponent<Physics>(); }, [](Entity entity) { entity.addComponent<Physics>(); } },
		{ "Movement", [](Entity entity) { return entity.hasComponent<Movement>(); }, [](Entity entity) { entity.addComponent<Movement>(); } },
		{ "Light", [](Entity entity) { return entity.hasComponent<RenderLight>(); }, [](Entity entity) { entity.addComponent<RenderLight>(); } },
		{ "Camera", [](Entity entity) { return entity.hasComponent<CameraComponent>(); }, [](Entity entity) { entity.addComponent<CameraComponent>(); } },
		{ "FreeCameraProp", [](Entity entity) { return entity.hasComponent<TagFreeCamera>(); }, [](Entity entity) { entity.addComponent<TagFreeCamera>(); } },
		{ "Render Model", [](Entity entity) { return entity.hasComponent<RenderModel>(); }, [](Entity entity) { entity.addComponent<RenderModel>(); } },
		{ "Script", [](Entity entity) { return entity.hasComponent<Script>(); }, [](Entity entity) { entity.addComponent<Script>(); } },
		{ "CurrentControl", [](Entity entity) { return entity.hasComponent<TagCurrentControl>(); }, [](Entity entity) { entity.addComponent<TagCurrentControl>(); } }
	};

	auto world = WorldManager::Instance()->GetWorld();
	const bool entitySelected = world && selectedEntity && world->IsValidWorldEntity(selectedEntity);
	if (entitySelected)
	{
		ImGui::Text("已选择: %d", selectedEntity.getId());
		ImGui::Separator();
		auto components = PropertiesHelper::GetAllComponents(selectedEntity);	// 获取所有组件
		PropertiesHelper::DrawAllProperties(selectedEntity, components, projectManager);		// 绘制所有属性
		ImGui::Spacing();
		if (ImGui::Button("Add Component", ImVec2(-1.f, 36.f)))
			ImGui::OpenPopup("AddComponentPopup");
		if (ImGui::BeginPopupModal("AddComponentPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Select a component to add");
			ImGui::Separator();
			for (const auto& factory : componentFactories)
			{
				const bool alreadyAdded = factory.has(selectedEntity);
				if (alreadyAdded)
					ImGui::BeginDisabled();
				if (ImGui::Selectable(factory.name))
				{
					if (world->IsValidWorldEntity(selectedEntity))
						factory.add(selectedEntity);
					ImGui::CloseCurrentPopup();
				}
				if (alreadyAdded)
					ImGui::EndDisabled();
			}
			ImGui::Spacing();
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	else
	{
		ImGui::Text("未选中实体");
		ImGui::Separator();
	}

	ImGui::End();
}

void ImguiLayout::DrawMaterialEditor(ProjectManager* projectManager)
{
	if (!s_materialEditorOpen || !projectManager)
		return;

	auto database = projectManager->GetDatabase();
	auto meta = database ? database->GetAssetMetaByGuid(s_materialEditorGuid) : nullptr;
	if (!meta || meta->type != AssetType::Material)
	{
		s_materialEditorOpen = false;
		return;
	}

	auto assetObject = projectManager->LoadAsset(meta->guid);
	auto materialAsset = assetObject ? std::dynamic_pointer_cast<MaterialAsset>(assetObject) : nullptr;
	if (!materialAsset)
	{
		s_materialEditorOpen = false;
		s_materialOrigin.valid = false;
		return;
	}
	if (!s_materialOrigin.valid || s_materialOrigin.guid != meta->guid)
	{
		s_materialOrigin.guid = meta->guid;
		s_materialOrigin.properties = materialAsset->GetProperties();
		s_materialOrigin.textures = materialAsset->GetTextureDependencies();
		s_materialOrigin.valid = true;
	}
	auto restoreOrigin = [&]() {
		if (!s_materialOrigin.valid || s_materialOrigin.guid != meta->guid)
			return;
		materialAsset->SetProperties(s_materialOrigin.properties);
		std::vector<TextureType> currentTextures;
		for (const auto& [type, textureGuid] : materialAsset->GetTextureDependencies())
			currentTextures.push_back(type);
		for (TextureType type : currentTextures)
			materialAsset->SetTextureDependency(type, {}, nullptr);
		for (const auto& [type, textureGuid] : s_materialOrigin.textures)
		{
			auto textureObject = projectManager->LoadAsset(textureGuid);
			auto texture = textureObject ? textureObject->AsTexture() : nullptr;
			if (texture)
				materialAsset->SetTextureDependency(type, textureGuid, texture);
		}
		};

	bool open = true;
	if (!ImGui::Begin(std::format("Material Editor: {}###MaterialEditor", meta->name).c_str(), &open, ImGuiWindowFlags_NoDocking))
	{
		ImGui::End();
		s_materialEditorOpen = open;
		if (!open)
		{
			restoreOrigin();
			s_materialOrigin.valid = false;
		}
		return;
	}

	bool changed = false;
	auto properties = materialAsset->GetProperties();
	const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.f;
	ImGui::BeginChild("MaterialEditorContent", ImVec2(0.f, -footerHeight), ImGuiChildFlags_None);
	if (ImGui::BeginTable("MaterialEditorColumns", 2,
		ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("Textures", ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::BeginChild("MaterialPropertyPanel", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
		ImGui::TextUnformatted("Material Properties");
		ImGui::Separator();
		ImGui::SetNextItemWidth(250.f);
		changed |= ImGui::ColorEdit3("Albedo", glm::value_ptr(properties.albedo));
		ImGui::SetNextItemWidth(130.f);
		changed |= ImGui::DragFloat("Metallic", &properties.metallic, 0.01f, 0.f, 1.f);
		ImGui::SetNextItemWidth(130.f);
		changed |= ImGui::DragFloat("Roughness", &properties.roughness, 0.01f, 0.001f, 1.f);
		ImGui::SetNextItemWidth(130.f);
		changed |= ImGui::DragFloat("Ambient Occlusion", &properties.ambientOcclusion, 0.01f, 0.f, 1.f);
		ImGui::SetNextItemWidth(130.f);
		changed |= ImGui::DragFloat("Opacity", &properties.opacity, 0.01f, 0.f, 1.f);
		ImGui::SetNextItemWidth(130.f);
		changed |= ImGui::DragFloat("IOR", &properties.IOR, 0.01f, 1.f, 3.f);
		ImGui::Spacing();
		ImGui::TextUnformatted("Emission");
		ImGui::SetNextItemWidth(250.f);
		changed |= ImGui::ColorEdit3("Emission Color", glm::value_ptr(properties.emissionColor));
		ImGui::SetNextItemWidth(130.f);
		changed |= ImGui::DragFloat("Emission Strength", &properties.emissionStrength, 0.01f, 0.f, 100.f);
		ImGui::Spacing();
		int alphaMode = static_cast<int>(properties.alphamode);
		const char* alphaModes[] = { "Opaque", "Mask", "Blend" };
		ImGui::SetNextItemWidth(130.f);
		if (ImGui::Combo("Alpha Mode", &alphaMode, alphaModes, IM_ARRAYSIZE(alphaModes)))
		{
			properties.alphamode = static_cast<AlphaMode>(alphaMode);
			changed = true;
		}
		if (properties.alphamode == AlphaMode::Mask)
		{
			ImGui::SetNextItemWidth(130.f);
			changed |= ImGui::DragFloat("Mask Threshold", &properties.maskthreshold, 0.01f, 0.f, 1.f);
		}
		changed |= ImGui::Checkbox("Two Sided", &properties.twosided);
		ImGui::EndChild();

		ImGui::TableSetColumnIndex(1);
		ImGui::BeginChild("MaterialTexturePanel", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
		ImGui::TextUnformatted("Texture Slots");
		ImGui::SameLine();
		ImGui::TextDisabled("Drag texture assets onto a slot");
		ImGui::Separator();
		const std::pair<TextureType, const char*> textureSlots[] = {
			{ TextureType::Albedo, "Albedo Texture" },
			{ TextureType::Metallic, "Metallic Texture" },
			{ TextureType::Roughness, "Roughness Texture" },
			{ TextureType::Normal, "Normal Texture" },
			{ TextureType::AO, "AO Texture" },
			{ TextureType::Emissive, "Emissive Texture" },
			{ TextureType::MetallicRoughness, "Metallic Roughness Texture" },
			{ TextureType::Height, "Height Texture" },
			{ TextureType::Opacity, "Opacity Texture" }
		};
		for (const auto& [type, label] : textureSlots)
		{
			ImGui::PushID(static_cast<int>(type));
			ImGui::BeginGroup();
			ImGui::TextUnformatted(label);
			const auto& dependencies = materialAsset->GetTextureDependencies();
			auto dependency = dependencies.find(type);
			std::string textureLabel = "None";
			if (dependency != dependencies.end())
			{
				auto textureMeta = database->GetAssetMetaByGuid(dependency->second);
				textureLabel = textureMeta ? textureMeta->name : dependency->second;
			}
			ImGui::Button(textureLabel.c_str(), ImVec2(220.f, 34.f));
			if (ImGui::BeginDragDropTarget())
			{
				if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
				{
					const auto* guidData = static_cast<const char*>(payload->Data);
					if (guidData && guidData[0] != '\0')
					{
						auto textureMeta = database->GetAssetMetaByGuid(AssetGUID(guidData));
						if (textureMeta && textureMeta->type == AssetType::Texture)
						{
							auto textureObject = projectManager->LoadAsset(textureMeta->guid);
							auto texture = textureObject ? textureObject->AsTexture() : nullptr;
							if (texture)
							{
								materialAsset->SetTextureDependency(type, textureMeta->guid, texture);
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::SameLine();
			if (dependency != dependencies.end() && ImGui::SmallButton("Clear"))
				materialAsset->SetTextureDependency(type, {}, nullptr);
			ImGui::EndGroup();
			ImGui::Spacing();
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::EndTable();
	}
	ImGui::EndChild();
	if (changed)
		materialAsset->SetProperties(properties);
	ImGui::Separator();
	ImGui::BeginChild("MaterialEditorFooter", ImVec2(0.f, footerHeight), ImGuiChildFlags_None);
	if (open)
	{
		if (ImGui::Button("Save"))
		{
			if (!projectManager->SaveAsset(meta->guid))
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "材质保存失败");
			}
			else
			{
				s_materialOrigin.properties = materialAsset->GetProperties();
				s_materialOrigin.textures = materialAsset->GetTextureDependencies();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Restore"))
			restoreOrigin();
	}
	ImGui::EndChild();
	ImGui::End();
	s_materialEditorOpen = open;
	if (!open)
	{
		restoreOrigin();
		s_materialOrigin.valid = false;
	}
}

void ImguiLayout::DrawAssetBrowser(ProjectManager* projectManager)
{
	{
		ImGui::Begin("Folder Tree");
		ImGui::Text("文件树");
		ImGui::Separator();

		if (!projectManager->IsProjectOpen()) {
			ImGui::Text("未打开项目");
		}
		else
		{
			// 递归显示文件夹
			auto projectPath = projectManager->GetProjectFolderFullPath();
			DrawFolderTree(projectPath, projectPath, projectPath);

		}
		ImGui::End();
	}

	{

		ImGui::Begin("Folder");

		if (!projectManager->IsProjectOpen()) {
			ImGui::Text("未打开文件夹");
			ImGui::Separator();
		}
		else
		{
			fs::path selectedFullFolderPath = fs::path(projectManager->GetProjectAssetFullPath(m_selectedFolder));
			if (fs::exists(selectedFullFolderPath))
			{
				DrawFolderContent(projectManager, selectedFullFolderPath.string());
			}
			else
				ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No Folders open");
		}

		ImGui::End();
	}
	DrawMaterialEditor(projectManager);
}

void ImguiLayout::DrawSceneView(WorldManager* worldManager, ProjectManager* projectManager)
{
	ImGui::Begin("Scene View");

	ImGuizmo::BeginFrame();
	ImGuizmo::SetDrawlist();

	// 获取场景视口区域
	ImVec2 viewportPos = ImGui::GetCursorScreenPos();
	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	ImGuizmo::SetRect(viewportPos.x, viewportPos.y,
		viewportSize.x, viewportSize.y);

	if (WantToAddDirLight || WantToAddPointLight || WantToAddSpotLight || WantToAddCube || WantToAddSphere || WantToAddEmptyEntity)
	{
		auto triBuffer = worldManager->GetTriBuffer();
		auto rayOrigin = triBuffer->acquireReadBuffer()->position;
		auto rayDirection = triBuffer->acquireReadBuffer()->direction;

		auto result = worldManager->RayCast(rayOrigin, rayDirection);
		glm::vec3 position = result.hit ? result.hitPoint : rayOrigin + rayDirection * 15.f;

		if (WantToAddDirLight)
		{
			worldManager->AddLight(LightType::Directional, position);
			WantToAddDirLight = false;
		}
		if (WantToAddPointLight)
		{
			worldManager->AddLight(LightType::Point, position);
			WantToAddPointLight = false;
		}
		if (WantToAddSpotLight)
		{
			worldManager->AddLight(LightType::Spot, position);
			WantToAddSpotLight = false;
		}
		if (WantToAddCube)
		{
			worldManager->AddGeometry(GeometryType::Cube, position);
			WantToAddCube = false;
		}
		if (WantToAddSphere)
		{
			worldManager->AddGeometry(GeometryType::Sphere, position);
			WantToAddSphere = false;
		}
		if (WantToAddEmptyEntity)
		{
			worldManager->AddEmptyEntity(position);
			WantToAddEmptyEntity = false;
		}
	}

	// 显示场景渲染
	auto renderer = worldManager->GetVulkanRener();
	if (renderer && renderer->GetVulkanDevice() && viewportSize.x > 0 && viewportSize.y > 0)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);

		static Task<bool> targetInitializationTask;
		static bool targetInitialized = false;
		static bool targetInitializationFailed = false;

		if (!targetInitialized)
		{
			if (!targetInitializationTask)
			{
				auto width = static_cast<uint32_t>(viewportSize.x);
				auto height = static_cast<uint32_t>(viewportSize.y);
				targetInitializationTask = CoroTask::Run([renderer, width, height, worldManager]() mutable -> bool {
					try
					{
						if (VulkanRenderer::CreateForOffScreen_Target(renderer, width, height))
						{
							renderer->Run();
							worldManager->RunPushFrame();
							return true;
						}
						return false;
					}
					catch (const std::exception&)
					{
						return false;
					}
					});
			}
			else if (targetInitializationTask && targetInitializationTask.is_done())
			{
				auto width = static_cast<uint32_t>(viewportSize.x);
				auto height = static_cast<uint32_t>(viewportSize.y);
				if (renderer->GetWidth() != width || renderer->GetHeight() != height)
				{
					targetInitializationTask = CoroTask::Run([renderer, width, height, worldManager]() mutable -> bool {
						try
						{
							worldManager->StopPushFrame();
							renderer->Resize(width, height);
							worldManager->RunPushFrame();
							return true;
						}
						catch (const std::exception&)
						{
							return false;
						}
						});
				}
				else
				{
					try
					{
						targetInitializationFailed = !targetInitializationTask.sync_wait();
					}
					catch (const std::exception&)
					{
						targetInitializationFailed = true;
					}
					targetInitialized = true;
				}
			}
		}

		if (targetInitialized && !targetInitializationFailed && renderer->GetWidth() != 0 && renderer->GetHeight() != 0)
		{
			static std::unordered_map<VkImageView, VkDescriptorSet> imguiRegisterTexture;

			auto prevClear = [&]() {
				{
					LockGuard guard_pool(VKCONTEXT->GetDescriptorPoolRequestMutex());
					vkDeviceWaitIdle(g_Device);
					for (auto& [view, textureId] : imguiRegisterTexture)
					{
						if (textureId != VK_NULL_HANDLE)
							ImGui_ImplVulkan_RemoveTexture(textureId);
					}
				}
				imguiRegisterTexture.clear();
				};

			worldManager->ResizeVulkan(viewportSize.x, viewportSize.y, prevClear);

			worldManager->WaitImage([&](std::shared_ptr<Texture2D> tex) {
				if (!tex) return;

				VkImageView view = tex->GetImageView(vk::ImageAspectFlagBits::eColor);
				if (view == VK_NULL_HANDLE)
					return;

				VkDescriptorSet textureId;
				auto it = imguiRegisterTexture.find(view);
				if (it == imguiRegisterTexture.end())
				{
					LockGuard guard_pool(VKCONTEXT->GetDescriptorPoolRequestMutex());
					textureId =
						ImGui_ImplVulkan_AddTexture(
							view,
							VK_IMAGE_LAYOUT_GENERAL
						);

					imguiRegisterTexture[view] = textureId;
				}
				else
				{
					textureId = it->second;
				}

				ImGui::Image(
					(ImTextureID)textureId,
					viewportSize,
					ImVec2(0, 0),
					ImVec2(1, 1)
				);

				});
		}
		else
		{
			// 当前视口左上角和可用区域
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const ImVec2 size = viewportSize;

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			// 视口背景
			drawList->AddRectFilled(
				pos,
				ImVec2(pos.x + size.x, pos.y + size.y),
				IM_COL32(30, 30, 30, 255)
			);

			const ImVec2 center(
				pos.x + size.x * 0.5f,
				pos.y + size.y * 0.5f - 12.0f
			);

			if (targetInitializationFailed)
			{
				const char* message = "Scene Renderer Initialization Failed";
				const ImVec2 textSize = ImGui::CalcTextSize(message);

				drawList->AddText(
					ImVec2(center.x - textSize.x * 0.5f,
						center.y + 30.0f),
					IM_COL32(255, 100, 100, 255),
					message
				);
			}
			else
			{
				// 旋转加载圆环
				const float time = static_cast<float>(ImGui::GetTime());
				const float startAngle = time * 4.0f;
				const float radius = 16.0f;

				drawList->PathClear();
				drawList->PathArcTo(
					center,
					radius,
					startAngle,
					startAngle + 4.2f,
					32
				);
				drawList->PathStroke(
					IM_COL32(80, 170, 255, 255),
					ImDrawFlags_None,
					3.0f
				);

				// 加载文字
				const char* message = "Initializing Scene Renderer...";
				const ImVec2 textSize = ImGui::CalcTextSize(message);

				drawList->AddText(
					ImVec2(center.x - textSize.x * 0.5f,
						center.y + 30.0f),
					IM_COL32(220, 220, 220, 255),
					message
				);

				// 动态省略号：Initializing Vulkan.
				//                         ..
				//                         ...
				const int dots =
					static_cast<int>(time * 2.0f) % 3 + 1;

				char loadingText[64] = "Loading";
				for (int i = 0; i < dots; ++i)
				{
					const size_t len = strlen(loadingText);
					loadingText[len] = '.';
					loadingText[len + 1] = '\0';
				}

				const ImVec2 loadingSize =
					ImGui::CalcTextSize(loadingText);

				drawList->AddText(
					ImVec2(center.x - loadingSize.x * 0.5f,
						center.y + 52.0f),
					IM_COL32(160, 160, 160, 255),
					loadingText
				);
			}

			// 为视口占据布局空间，避免后续 ImGui 控件重叠
			ImGui::Dummy(size);
		}


		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const char* data = (const char*)payload->Data;
				if (data && strlen(data) > 0) {
					AssetGUID guid(data);
					if (auto meta = projectManager->GetDatabase()->GetAssetMetaByGuid(guid))
						HandleAssetDrop(*meta, worldManager, projectManager, viewportPos, viewportSize);
				}
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const char* data = (const char*)payload->Data;
				if (data && strlen(data) > 0)
				{
					std::string fileFullPath(data);
					std::string ext = fs::path(fileFullPath).extension().string();
					if (ext == ".lua")
					{
						auto triBuffer = worldManager->GetTriBuffer();
						auto view = triBuffer->acquireReadBuffer()->view;
						auto projection = triBuffer->acquireReadBuffer()->projection;

						glm::mat4 projOG = projection;
						projOG[1][1] *= -1;

						glm::vec3 rayOrigin, rayDirection;
						ImGuizmo::ComputeMouseRay(
							(float*)&view,
							(float*)&projOG,
							ImGui::GetMousePos(),
							viewportPos,
							viewportSize,
							(float*)&rayOrigin,
							(float*)&rayDirection
						);

						auto entity = worldManager->PickObject(rayOrigin, rayDirection);
						if (entity && worldManager->GetWorld()->IsValidWorldEntity(entity))
							HandleScriptFileDrop(fileFullPath, entity, projectManager);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		// 获取摄像机的 View 和 Projection 矩阵
		auto triBuffer = worldManager->GetTriBuffer();
		auto view = triBuffer->acquireReadBuffer()->view;
		auto projection = triBuffer->acquireReadBuffer()->projection;

		if (selectedEntity)
		{
			auto* transform = selectedEntity.tryGetComponent<Transform>();
			auto* physics = selectedEntity.tryGetComponent<Physics>();
			if (transform)
			{
				glm::mat4 projOG = projection;
				projOG[1][1] *= -1;

				auto transformMatrix = transform->GetMatrix();
				glm::mat4 deltaMatrix = glm::mat4(1.0f);

				// 显示 Gizmo 并操作
				ImGuizmo::Manipulate(
					(float*)glm::value_ptr(view),
					(float*)glm::value_ptr(projOG),
					mCurrentGizmoOperation,
					mCurrentGizmoMode,
					(float*)glm::value_ptr(transformMatrix),
					(float*)glm::value_ptr(deltaMatrix),
					useSnap ? snap : nullptr
				);

				if (ImGuizmo::IsUsing())
				{
					worldManager->GetWorld()->SubmitCommand(
						[newMatrix = transformMatrix, entity = selectedEntity]() mutable ->void {
							auto* transform = entity.tryGetComponent<Transform>();
							auto* physics = entity.tryGetComponent<Physics>();
							auto* runtime = entity.tryGetComponent<PhysicsRuntime>();
							if (!transform)
								return;

							transform->SetMatrix(newMatrix);

							if (physics && runtime)
							{
								physics->forceSyncTransform = true;
								if (runtime->body && !runtime->body->isActive())
									runtime->body->activate(true);
								if (physics->isCharacter && runtime->character && runtime->ghostObject && !runtime->ghostObject->isActive())
									runtime->ghostObject->activate(true);
							}

						}
					);
				}
			}

			ImGuiIO& io = ImGui::GetIO();
			if (io.WantCaptureKeyboard)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_T))
					mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
				if (ImGui::IsKeyPressed(ImGuiKey_E))
					mCurrentGizmoOperation = ImGuizmo::ROTATE;
				if (ImGui::IsKeyPressed(ImGuiKey_R))
					mCurrentGizmoOperation = ImGuizmo::SCALE;
				if (ImGui::IsKeyPressed(ImGuiKey_L))
					mCurrentGizmoMode = (mCurrentGizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
				if (ImGui::IsKeyPressed(ImGuiKey_Q))
					useSnap = !useSnap;
			}

			if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
			{
				// 右键菜单
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("EntityContextMenu");
			}
		}

		if (ImGui::BeginPopup("EntityContextMenu"))
		{
			if (ImGui::MenuItem("Delete"))
			{
				worldManager->GetWorld()->SubmitCommand([entity = selectedEntity, world = worldManager->GetWorld()]() -> void {
					if (!entity || !world)
						return;
					world->destroyEntity(entity);
					}
				);
				selectedEntity = Entity();
			}
			if (ImGui::MenuItem("Duplicate"))
			{
				if (Entity newEntity; worldManager->DuplicateEntity(selectedEntity, newEntity) && newEntity)
					selectedEntity = newEntity;
			}

			ImGui::EndPopup();
		}

		worldManager->SetInputActive(ImGui::IsItemHovered());
		if (ImGui::IsItemHovered())
		{
			ImGuiIO& io = ImGui::GetIO();
			ImVec2 mousePos = ImGui::GetMousePos();

			if (!ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
			{

				// 1. 鼠标左键点击 - 拾取物体
				if (ImGui::IsMouseClicked(0))
				{
					// 获取摄像机的 View 和 Projection 矩阵
					auto triBuffer = worldManager->GetTriBuffer();
					auto view = triBuffer->acquireReadBuffer()->view;
					auto projection = triBuffer->acquireReadBuffer()->projection;

					glm::mat4 projOG = projection;
					projOG[1][1] *= -1;

					// 计算射线
					glm::vec3 rayOrigin;
					glm::vec3 rayDirection;
					ImGuizmo::ComputeMouseRay(
						(float*)&view,
						(float*)&projOG,
						mousePos,
						viewportPos,
						viewportSize,
						(float*)&rayOrigin,
						(float*)&rayDirection
					);

					// 拾取
					selectedEntity = worldManager->PickObject(rayOrigin, rayDirection);

					//if (selectedEntity)
					//	worldManager->SelectObject(selectedEntity);
					//else
					//	worldManager->DeselectObject();
				}

				// 3. 鼠标右键拖拽 - 旋转摄像机
				if (ImGui::IsMouseDragging(1))
				{
					ImVec2 delta = ImGui::GetMouseDragDelta(1);
					worldManager->RotateCamera(delta.x, delta.y);
					ImGui::ResetMouseDragDelta(1);
				}

				// 4. 鼠标中键拖拽 - 平移摄像机
				if (ImGui::IsMouseDragging(2))
				{
					ImVec2 delta = ImGui::GetMouseDragDelta(2);
					worldManager->PanCamera(delta.x, delta.y);
					ImGui::ResetMouseDragDelta(2);
				}

				// 5. 滚轮缩放
				if (io.MouseWheel != 0)
				{
					worldManager->ZoomCamera(io.MouseWheel);
				}
			}
		}

		std::string sceneLabel;
		if (m_currentSceneAssetGuid.empty())
		{
			sceneLabel = "当前场景：临时场景（未保存）";
		}
		else if (projectManager && projectManager->IsProjectOpen())
		{
			if (auto database = projectManager->GetDatabase())
			{
				if (auto meta = database->GetAssetMetaByGuid(m_currentSceneAssetGuid))
					sceneLabel = "当前场景：" + meta->path;
			}
		}
		if (sceneLabel.empty())
			sceneLabel = "当前场景资产未找到：" + m_currentSceneAssetGuid;

		// ========== 显示信息 ==========
		// 左上角显示场景信息和操作模式
		std::string text = sceneLabel
			+ std::format("\nGizmo: {} | {} {}", GetOpName(mCurrentGizmoOperation), GetModeName(mCurrentGizmoMode), useSnap ? "[Snap]" : "")
			+ "\nShortcuts:"
			+ "\nQ - Toggle Snap"
			+ "\nL - Toggle World/Local"
			+ "\nT - Translate"
			+ "\nE - Rotate"
			+ "\nR - Scale";

		ImGui::SetCursorPos(ImVec2(15, 15));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::PopStyleColor();

		ImGui::PopItemFlag();
	}

	ImGui::End();
}

template<>
static bool PropertiesHelper::DrawData(RenderOption& option)
{
	bool anyChange = false;
	anyChange |= PropertiesHelper::DrawWithTitle("PostProcessFlags", option.flags);
	anyChange |= PropertiesHelper::DrawWithTitle("PostProcessParams", option.postProcessParams);
	anyChange |= PropertiesHelper::DrawWithTitle("AtmosphereParams", option.atmosphereParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSAOParams", option.ssaoParams);
	anyChange |= PropertiesHelper::DrawWithTitle("ExponentialHeightFog", option.exponentialHeightFogParams);
	anyChange |= PropertiesHelper::DrawWithTitle("RayTraceGeneralParams", option.rayTraceGeneralParams);
	anyChange |= PropertiesHelper::DrawWithTitle("RayTraceReflectParams", option.rayTraceReflectParams);
	anyChange |= PropertiesHelper::DrawWithTitle("RayTraceGIParams", option.rayTraceGIParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSReflectParams", option.ssrTraceParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSGIParams", option.ssgiTraceParams);
	return anyChange;
}

void ImguiLayout::DrawOptions(WorldManager* worldManager)
{
	ImGui::Begin("Options");
	ImGui::Text("渲染选项");
	ImGui::Separator();

	auto option = worldManager->GetOption();

	bool change = PropertiesHelper::DrawData(option);
	if (change)
		worldManager->SetOption(option);

	ImGui::End();
}

void ImguiLayout::DrawStatusBar(WorldManager* worldManager)
{
	// 获取视口信息
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	const float statusBarHeight = ImGui::GetFrameHeightWithSpacing();

	// 设置状态栏位置：屏幕底部
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statusBarHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, statusBarHeight));

	// 关键：确保状态栏在 Z 序最上层
	ImGui::SetNextWindowViewport(viewport->ID);

	// 窗口样式
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoInputs;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));  // 深灰色背景
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

	ImGui::Begin("##StatusBar", nullptr, flags);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);

	// 用 ImDrawList 在顶部画一条亮色分割线，让状态栏更明显
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();
	drawList->AddLine(
		ImVec2(windowPos.x, windowPos.y),
		ImVec2(windowPos.x + windowSize.x, windowPos.y),
		IM_COL32(80, 80, 80, 255),
		1.5f
	);

	if (ImGui::BeginTable("StatusBarTable", 3, ImGuiTableFlags_None))
	{
		ImGui::TableSetupColumn("##col0", ImGuiTableColumnFlags_WidthFixed, 200.0f);
		ImGui::TableSetupColumn("##col1", ImGuiTableColumnFlags_WidthFixed, 180.0f);
		ImGui::TableSetupColumn("##col2", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();

		// ---- 第1列：场景信息 ----
		ImGui::TableNextColumn();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "实体数量: %d", worldManager->GetWorld()->getAllEntityCount());

		// ---- 第2列：选中的物体 ----
		ImGui::TableNextColumn();
		if (selectedEntity) {
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "选择实体: %d", selectedEntity.getId());
		}
		else {
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "未选中实体");
		}

		// ---- 第3列：FPS（右对齐） ----
		ImGui::TableNextColumn();
		float fps = ImGui::GetIO().Framerate;
		float ms = 1000.0f / fps;
		float textWidth = ImGui::CalcTextSize("FPS: 000.0  (00.00ms)").x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - textWidth - 8.0f);
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "FPS: %.1f  (%.2fms)", fps, ms);

		ImGui::EndTable();
	}
	ImGui::End();
}

void ImguiLayout::DropFile(const std::string& filePathStr, ProjectManager* projectManager, const ImVec2& mousePos)
{
	fs::path filePath(filePathStr);
	if (filePath.string().empty())
		return;

	// 拖入项目文件
	if (filePath.extension().string() == ".project")
	{
		if (projectManager->IsProjectOpen()) {
			m_pendingAction = "wantOpenProject";
			memset(m_wantOpenProjectPathBuffer, 0, sizeof(m_wantOpenProjectPathBuffer));
			strcpy(m_wantOpenProjectPathBuffer, filePath.string().c_str());
			s_openSaveConfirmPopup = true;
		}
		else {
			OpenProjectAndLoadDefaultScene(filePath.string(), projectManager, WorldManager::Instance());
			m_selectedFolder.clear();
		}
	}
	else if (projectManager->IsProjectOpen())	// 导入资产
	{
		std::string targetWindow = GetWindowAtPosition(mousePos);

		if (targetWindow == "Folder")
		{
			std::string importFolerPath = m_selectedFolder.empty() || m_selectedFolder == "." ?
				projectManager->GetProjectFolderFullPath()
				: projectManager->GetProjectAssetFullPath(m_selectedFolder);
			fs::path importPath = fs::path(importFolerPath) / filePath.filename().string();
			s_assetImportPopup.SetImportPath(importPath.string());// 导入到资产浏览器当前文件夹
		}

		s_assetImportPopup.SetFilePath(filePath.string());
		s_assetImportPopup.SetVisiable(true);
	}
}

void ImguiLayout::DropFolder(const std::string& folderPath, ProjectManager* projectManager, const ImVec2& mousePos)
{
	std::vector<fs::path> files;
	try {
		for (const auto& entry : fs::directory_iterator(folderPath)) {
			if (entry.is_regular_file()) {
				files.push_back(entry.path());
			}
		}
	}
	catch (...) {
		return;
	}

	bool hasOpenProject = false;
	for (auto& filePath : files)
	{
		if (filePath.extension().string() == ".project")
		{
			if (projectManager->IsProjectOpen())
			{
				memset(m_wantOpenProjectPathBuffer, 0, sizeof(m_wantOpenProjectPathBuffer));
				strcpy(m_wantOpenProjectPathBuffer, filePath.string().c_str());
				m_pendingAction = "wantOpenProject";
				s_openSaveConfirmPopup = true;
				hasOpenProject = true;
				break;
			}
			else {
				if (OpenProjectAndLoadDefaultScene(filePath.string(), projectManager, WorldManager::Instance()))
					m_selectedFolder.clear();
				hasOpenProject = true;
				break;
			}
		}
	}

	if (!hasOpenProject)
	{

		memset(m_newProjectPathBuffer, 0, sizeof(m_newProjectPathBuffer));
		strcpy(m_newProjectPathBuffer, folderPath.c_str());
		if (projectManager->IsProjectOpen()) {
			m_pendingAction = "new";
			s_openSaveConfirmPopup = true;
		}
		else {
			s_openNewProjectPopup = true;
		}
	}
}

void ImguiLayout::HandleAssetDrop(const AssetMeta& meta, WorldManager* worldManager, ProjectManager* projectManager, const ImVec2& viewportPos, const ImVec2& viewportSize)
{
	// 获取鼠标在场景中的世界坐标位置
	ImVec2 mousePos = ImGui::GetMousePos();
	//glm::vec3 worldPos = GetMouseWorldPosition(mousePos, worldManager, viewportPos, viewportSize);


	auto assetObject = projectManager->LoadAsset(meta.guid);
	if (!assetObject)
		return;

	auto GetMouseRay =
		[&](glm::vec3& rayOrigin, glm::vec3& rayDirection)
		{
			auto triBuffer = worldManager->GetTriBuffer();
			auto view = triBuffer->acquireReadBuffer()->view;
			auto projection = triBuffer->acquireReadBuffer()->projection;

			glm::mat4 projOG = projection;
			projOG[1][1] *= -1;

			// 计算射线
			ImGuizmo::ComputeMouseRay(
				(float*)&view,
				(float*)&projOG,
				mousePos,
				viewportPos,
				viewportSize,
				(float*)&rayOrigin,
				(float*)&rayDirection
			);
		};

	auto RayCast =
		[&](const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
			World& world, Entity& entity,
			glm::vec3* hitPos = nullptr, float* distance = nullptr, glm::vec3* hitNormal = nullptr
			) -> bool
		{
			auto result = worldManager->RayCast(rayOrigin, rayDirection);
			if (!result.hit)
				return false;
			if (!world.IsValidWorldEntity(result.hitEntity))
				return false;

			entity = result.hitEntity;
			if (hitPos)
				*hitPos = result.hitPoint;
			if (distance)
				*distance = result.distance;
			if (hitNormal)
				*hitNormal = result.hitNormal;

			return true;
		};


	auto RayCastFromMouse =
		[&](World& world, Entity& entity,
			glm::vec3* hitPos = nullptr, float* distance = nullptr, glm::vec3* hitNormal = nullptr
			) -> bool
		{
			// 计算射线
			glm::vec3 rayOrigin;
			glm::vec3 rayDirection;
			GetMouseRay(rayOrigin, rayDirection);
			return RayCast(rayOrigin, rayDirection, world, entity, hitPos, distance, hitNormal);
		};

	switch (meta.type)
	{
	case AssetType::StaticMesh:
	{
		if (auto meshes = assetObject->AsStaticMesh(); !meshes.empty())
		{
			auto model = std::make_shared<Model>();
			for (auto mesh : meshes)
				model->AddMesh(mesh, std::make_shared<Material>());
			model->SetAssetGUID(meta.guid);

			auto entity = worldManager->CreateModelEntity(model);
			if (!entity)
				return;

			if (auto trans = entity.tryGetComponent<Transform>())
			{
				worldManager->GetWorld()->SubmitCommand([&](World& world)
					{
						glm::vec3 rayOrigin, rayDirection;
						GetMouseRay(rayOrigin, rayDirection);

						Entity entity;
						glm::vec3 hitPos;
						trans->position = RayCast(rayOrigin, rayDirection, world, entity, &hitPos) ? hitPos : rayOrigin + rayDirection * 15.f;

					}).get();
			}

		}
		break;
	}
	case AssetType::Texture:
	{
		if (auto texture = assetObject->AsTexture())
		{
		}
		break;
	}
	case AssetType::Material:
	{
		if (auto material = assetObject->AsMaterial())
		{
			worldManager->GetWorld()->SubmitCommand([&](World& world)
				{

					Entity entity;
					if (!RayCastFromMouse(world, entity))
						return;

					auto rModel = entity.tryGetComponent<RenderModel>();
					if (!rModel || !rModel->model)
						return;
					auto& meshInfos = rModel->model->getMeshInfos();
					if (meshInfos.size() == 1)
						meshInfos[0].material = material;

				}).get();
		}
		break;
	}
	case AssetType::Model:
	{
		if (auto model = assetObject->AsModel())
		{
			auto entity = worldManager->CreateModelEntity(model);
			if (!entity)
				return;

			if (auto trans = entity.tryGetComponent<Transform>())
			{
				worldManager->GetWorld()->SubmitCommand([&](World& world)
					{
						glm::vec3 rayOrigin, rayDirection;
						GetMouseRay(rayOrigin, rayDirection);

						Entity entity;
						glm::vec3 hitPos;
						trans->position = RayCast(rayOrigin, rayDirection, world, entity, &hitPos) ? hitPos : rayOrigin + rayDirection * 15.f;

					}).get();

			}
		}
		break;
	}
	case AssetType::Scene:
	{
		if (auto scene = assetObject->AsScene(); scene && worldManager->LoadFromSceneCodec(scene, projectManager))
		{
			m_currentSceneAssetGuid = meta.guid;
			selectedEntity = Entity();
		}
		break;
	}
	case AssetType::Audio:
	{
		if (auto audio = assetObject->AsScene())
		{
		}
		break;
	}
	case AssetType::Script:
	{
		Entity entity;
		auto world = worldManager->GetWorld();
		if (world && RayCastFromMouse(*world, entity))
			ApplyScriptAsset(entity, meta, projectManager);
	}
	}
}

void ImguiLayout::ApplyScriptAsset(Entity target, const AssetMeta& meta, ProjectManager* projectManager)
{
	if (!target || !projectManager || meta.type != AssetType::Script)
		return;
	auto asset = projectManager->LoadAsset(meta.guid);
	auto source = asset ? asset->AsScript() : nullptr;
	if (!source)
		return;
	auto world = target.getWorld();
	if (!world)
		return;
	auto assign = [target, source](World& activeWorld) {
		if (!activeWorld.IsValidWorldEntity(target))
			return;
		auto script = target.tryGetComponent<Script>();
		if (!script)
			script = &target.addComponent<Script>();
		script->data = source;
		script->needReload = true;
		};
	if (world->isRunning())
		world->SubmitCommand(assign).get();
	else
		assign(*world);
}

void ImguiLayout::HandleScriptFileDrop(const std::string& filePath, Entity target, ProjectManager* projectManager)
{
	if (filePath.empty() || !target || !projectManager || !projectManager->IsProjectOpen())
		return;

	std::error_code ec;
	fs::path fullPath = fs::weakly_canonical(filePath, ec);
	if (ec)
		fullPath = fs::absolute(filePath, ec).lexically_normal();

	fs::path destination = fullPath;
	fs::path projectPath = fs::weakly_canonical(projectManager->GetProjectFolderFullPath(), ec);
	fs::path relativePath = fullPath.lexically_relative(projectPath);
	if (ec || relativePath.empty() || relativePath.is_absolute() || *relativePath.begin() == "..")
		destination = projectPath / "Assets" / "Others" / fullPath.filename();

	s_assetImportPopup.SetFilePath(fullPath.string());
	s_assetImportPopup.SetImportPath(destination.string());
	s_assetImportPopup.SetImportCallback([target, projectManager](const AssetMeta& importedMeta) {
		ApplyScriptAsset(target, importedMeta, projectManager);
		});
	s_assetImportPopup.SetVisiable(true);
}

std::string ImguiLayout::GetWindowAtPosition(const ImVec2& pos)
{
	ImGuiContext& g = *ImGui::GetCurrentContext();

	// 从后向前遍历（最上层的窗口优先）
	for (int i = g.Windows.Size - 1; i >= 0; --i) {
		ImGuiWindow* window = g.Windows[i];

		// 跳过隐藏/折叠的窗口
		if (!window->Active || window->Collapsed) {
			continue;
		}

		// 跳过主菜单栏和状态栏等特殊窗口
		if (window->Flags & (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking)) {}

		// 检查鼠标是否在窗口区域内
		ImRect rect(window->Pos, window->Pos + window->Size);
		if (rect.Contains(pos)) {
			return window->Name;
		}
	}

	return "";
}
