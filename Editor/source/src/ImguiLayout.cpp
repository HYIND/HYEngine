#include "ImguiLayout.h"

#include "Helper/FileIO.h"
#include "glm/gtc/type_ptr.hpp"
#include "CommonComponent.h"
#include "PropertiesHelper.h"
#include "IconManager.h"
#include <filesystem>

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
	float textHeight = ImGui::GetFontSize() * 2;  // 获取当前字体高度（单行）
	float windowWidth = ImGui::GetContentRegionAvail().x;
	float rowHeight = iconSize + textHeight + padding;  // 固定行高

	int columns = std::max(1, (int)(windowWidth / (iconSize + padding)));

	int totalItems = items.size();
	int totalRows = (totalItems + columns - 1) / columns;

	ImGui::Columns(columns, nullptr, false);

	auto* iconMgr = IconManager::Get();

	auto DrawFolder = [&](const Item& item)-> void
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

			if (isHovered)
			{
				drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
					IM_COL32(70, 130, 200, 150));  // 蓝色
			}

			ImGui::TextWrapped("%s", item.name.c_str());

			if (isDbClicked)
			{
				fs::path fullFolderPath = currentPath / fs::path(item.name);
				fs::path relativeRootPath = fs::relative(fullFolderPath, fs::path(projectManager->GetProjectFolderFullPath()));
				m_selectedFolder = relativeRootPath.string();
			}

			ImGui::NextColumn();
			ImGui::PopID();
		};

	auto DrawFile = [&](const Item& item)-> void {
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
			drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
				IM_COL32(70, 130, 200, 150));  // 蓝色

			if (ImGui::IsMouseDragging(0))
			{

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
		}

		ImGui::TextWrapped("%s", item.name.c_str());


		if (isDbClicked)
		{
		}

		if (isRightClicked) {
			ImGui::OpenPopup("file_context_menu");
		}

		if (ImGui::BeginPopupContextItem("file_context_menu"))
		{
			// 菜单项
			if (ImGui::MenuItem("打开")) {
				//// 打开文件
				//OpenFile(file);
			}

			ImGui::Separator();

			if (ImGui::MenuItem("复制路径")) {
				ImGui::SetClipboardText(fullFilePath.string().c_str());
			}

			if (ImGui::MenuItem("复制文件名")) {
				ImGui::SetClipboardText(item.name.c_str());
			}

			ImGui::Separator();

			std::string deleteStr = isAssetFile ? "删除资产" : "删除文件";
			if (ImGui::MenuItem(deleteStr.c_str()))
			{
				try {
					fs::remove_all(fullFilePath);
					if (isAssetFile)
						projectManager->DeleteAsset(meta.guid);
				}
				catch (const fs::filesystem_error& e) {
					ImGui::OpenPopup("删除失败");
				}
			}


			if (ImGui::MenuItem("重命名")) {
				//// 重命名
				//m_renameTarget = file;
				//m_showRenamePopup = true;
			}

			ImGui::Separator();

			if (ImGui::MenuItem("属性")) {
				// 显示文件属性
				//ShowFileProperties(file);
			}

			ImGui::EndPopup();
		}

		ImGui::NextColumn();
		ImGui::PopID();
		};

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

	float totalHeight = totalRows * rowHeight;
	float currentHeight = ImGui::GetCursorPosY();
	if (totalHeight > currentHeight) {
		ImGui::Dummy(ImVec2(0, totalHeight - currentHeight));
	}
}

void ImguiLayout::DrawMainMenu(GLFWwindow* window, ProjectManager* projectManager)
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
			if (FileIO::OpenOneFile(filePath))
			{
				projectManager->OpenProject(filePath);
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
				projectManager->OpenProject(filePath);
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
					if (FileIO::OpenOneFile(filePath))
					{
						projectManager->OpenProject(filePath);
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

		if (ImGui::BeginMenu("窗口"))
		{
			ImGui::MenuItem("场景", nullptr, true);
			ImGui::MenuItem("场景对象", nullptr, true);
			ImGui::MenuItem("对象属性", nullptr, true);
			ImGui::MenuItem("项目管理", nullptr, true);
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
	if (s_assetImportPopup.GetVisiable()) {
		s_assetImportPopup.DrawPopup(projectManager);
	}
}

void ImguiLayout::SetDockspace(bool firstFrame)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// 设置窗口位置和大小：从菜单栏下方开始，填满剩余区域
	float menuBarHeight = ImGui::GetFrameHeight();
	float statusBarHeight = ImGui::GetFrameHeightWithSpacing();

	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight - statusBarHeight));

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
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(viewport->Size.x, viewport->Size.y - menuBarHeight - statusBarHeight));

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
					Entity newEntity;
					worldManager->GetWorld()->SubmitCommand([&newEntity, entity = item.entity, world = worldManager->GetWorld()]() -> void {
						if (!entity || !world)
							return;
						newEntity = world->DuplicateEntity(entity);
						if (newEntity.hasComponent<NameTag>())
						{
							auto& tag = newEntity.getComponent<NameTag>();
							tag.name += "_copy";
						}
						if (auto renderModel = newEntity.tryGetComponent<RenderModel>(); renderModel && renderModel->model)
							renderModel->model = renderModel->model->Clone(true, true, true);
						}
					).get();

					if (newEntity)
						selectedEntity = newEntity;
				}

				ImGui::EndPopup();
			}
		}

		ImGui::PopID();
	}

	ImGui::End();
}

void ImguiLayout::DrawObjectProperties()
{
	ImGui::Begin("Properties");
	ImGui::Text("属性");
	ImGui::Separator();

	if (selectedEntity)
	{
		ImGui::Text("已选择: %d", selectedEntity.getId());
		ImGui::Separator();
		auto components = PropertiesHelper::GetAllComponents(selectedEntity);	// 获取所有组件
		PropertiesHelper::DrawAllProperties(selectedEntity, components);		// 绘制所有属性
	}
	else
	{
		ImGui::Text("未选中实体");
		ImGui::Separator();
	}

	ImGui::End();
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

	// 显示场景渲染
	if (viewportSize.x > 0 && viewportSize.y > 0)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);

		{
			auto hdc = wglGetCurrentDC();
			auto hglrc = wglGetCurrentContext();

			{
				auto guard = THREADCONTEXT->GetBindGuard();

				static bool first = true;
				if (first)
				{
					WorldManager::Instance()->InitOpenGLRender(viewportSize.x, viewportSize.y);
					first = false;
				}

				worldManager->ResizeOpenGL(viewportSize.x, viewportSize.y);
				worldManager->RenderFrame();
			}
			wglMakeCurrent(hdc, hglrc);
		}

		GLuint renderTexture = worldManager->GetOpenGLRener()->GetColorBuffer();
		if (renderTexture != 0)
		{
			ImGui::Image(
				(void*)(intptr_t)renderTexture,
				viewportSize,
				ImVec2(0, 1),
				ImVec2(1, 0)
			);
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
			{
				const char* data = (const char*)payload->Data;
				if (data && strlen(data) > 0) {
					AssetGUID guid(data);
					if (auto meta = projectManager->GetDatabase()->GetAssetMetaByGuid(guid))
					{
						auto hdc = wglGetCurrentDC();
						auto hglrc = wglGetCurrentContext();

						{
							auto guard = THREADCONTEXT->GetBindGuard();
							HandleAssetDrop(*meta, worldManager, projectManager, viewportPos, viewportSize);
						}
						wglMakeCurrent(hdc, hglrc);
					}
				}
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE"))
			{
				const char* data = (const char*)payload->Data;
				if (data && strlen(data) > 0) {
					fs::path filePath(data);
					//HandleAssetDrop(assetPath, worldManager, viewportPos, viewportSize);
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
				auto transformMatrix = transform->getMatrix();
				glm::mat4 deltaMatrix = glm::mat4(1.0f);

				// 显示 Gizmo 并操作
				ImGuizmo::Manipulate(
					(float*)glm::value_ptr(view),
					(float*)glm::value_ptr(projection),
					mCurrentGizmoOperation,
					mCurrentGizmoMode,
					(float*)glm::value_ptr(transformMatrix),
					(float*)glm::value_ptr(deltaMatrix),
					useSnap ? snap : nullptr
				);

				if (ImGuizmo::IsUsing())
				{
					worldManager->GetWorld()->SubmitCommand(
						[newMatrix = transformMatrix, entity = selectedEntity]()->void {
							auto* transform = entity.tryGetComponent<Transform>();
							auto* physics = entity.tryGetComponent<Physics>();
							if (!transform)
								return;
							transform->setMatrix(newMatrix);

							if (physics)
							{
								physics->forceSyncTransform = true;
								if (physics->body && !physics->body->isActive())
									physics->body->activate(true);
								if (physics->isCharacter && physics->character && physics->ghostObject && !physics->ghostObject->isActive())
									physics->ghostObject->activate(true);
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
				Entity newEntity;
				worldManager->GetWorld()->SubmitCommand([&newEntity, entity = selectedEntity, world = worldManager->GetWorld()]() -> void {
					if (!entity || !world)
						return;
					newEntity = world->DuplicateEntity(entity);
					if (newEntity.hasComponent<NameTag>())
					{
						auto& tag = newEntity.getComponent<NameTag>();
						tag.name += "_copy";
					}
					if (auto renderModel = newEntity.tryGetComponent<RenderModel>(); renderModel && renderModel->model)
						renderModel->model = renderModel->model->Clone(true, true, true);
					}
				).get();

				if (newEntity)
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

					// 计算射线
					glm::vec3 rayOrigin;
					glm::vec3 rayDirection;
					ImGuizmo::ComputeMouseRay(
						(float*)&view,
						(float*)&projection,
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

		// ========== 显示信息 ==========
		// 左上角显示操作模式
		std::string text =
			std::format("Gizmo: {} | {} {}", GetOpName(mCurrentGizmoOperation), GetModeName(mCurrentGizmoMode), useSnap ? "[Snap]" : "")
			+ "\nShortcuts:"
			+ "\nQ - Toggle Snap"
			+ "\nL - Toggle World/Local"
			+ "\nT - Translate"
			+ "\nE - Rotate"
			+ "\nR - Scale";

		ImGui::SetCursorPos(ImVec2(15, 15));
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), text.c_str());

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
	anyChange |= PropertiesHelper::DrawWithTitle("DepthFogParams", option.depthFogParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSGITraceParams", option.ssgiTraceParams);
	anyChange |= PropertiesHelper::DrawWithTitle("SSRTraceParams", option.ssrTraceParams);
	anyChange |= PropertiesHelper::DrawWithTitle("RayTraceReflectParams", option.rayTraceParams);
	return anyChange;
}

void ImguiLayout::DrawOptions(WorldManager* worldManager)
{
	ImGui::Begin("Options");
	ImGui::Text("视频选项");
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
		| ImGuiWindowFlags_NoNavFocus;

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

	// 状态栏内容 - 使用多列布局
	ImGui::Columns(3, "StatusBarColumns", false);
	ImGui::SetColumnWidth(0, 200.0f);
	ImGui::SetColumnWidth(1, 180.0f);
	// 第3列自动适应剩余宽度

	// ---- 第1列：场景信息 ----
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "实体数量: %d", worldManager->GetWorld()->getAllEntityCount());

	// ---- 第2列：选中的物体 ----
	ImGui::NextColumn();
	if (selectedEntity) {
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "选择实体: %d", selectedEntity.getId());
	}
	else {
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "未选中实体");
	}

	// ---- 第3列：FPS（右对齐） ----
	ImGui::NextColumn();
	float fps = ImGui::GetIO().Framerate;
	float ms = 1000.0f / fps;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - 150.0f);
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "FPS: %.1f  (%.2fms)", fps, ms);

	ImGui::Columns(1);
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
			projectManager->OpenProject(filePath.string());
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
				if (projectManager->OpenProject(filePath.string()))
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

	switch (meta.type)
	{
	case AssetType::StaticMesh:
	{
		if (auto meshes = assetObject->AsStaticMesh(); !meshes.empty())
		{
			auto model = std::make_shared<Model>();
			for (auto mesh : meshes)
				model->AddMesh(mesh, std::make_shared<Material>());

			auto entity = worldManager->CreateModelEntity(model);
			if (!entity)
				return;

			if (auto trans = entity.tryGetComponent<Transform>())
			{
				worldManager->GetWorld()->SubmitCommand([&]
					{
						auto triBuffer = worldManager->GetTriBuffer();
						auto view = triBuffer->acquireReadBuffer()->view;
						auto projection = triBuffer->acquireReadBuffer()->projection;

						// 计算射线
						glm::vec3 rayOrigin;
						glm::vec3 rayDirection;
						ImGuizmo::ComputeMouseRay(
							(float*)&view,
							(float*)&projection,
							mousePos,
							viewportPos,
							viewportSize,
							(float*)&rayOrigin,
							(float*)&rayDirection
						);

						auto result = worldManager->RayCast(rayOrigin, rayDirection);
						trans->position = result.hit ? result.hitPoint : rayOrigin + rayDirection * 15.f;

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
				worldManager->GetWorld()->SubmitCommand([&]
					{
						auto triBuffer = worldManager->GetTriBuffer();
						auto view = triBuffer->acquireReadBuffer()->view;
						auto projection = triBuffer->acquireReadBuffer()->projection;

						// 计算射线
						glm::vec3 rayOrigin;
						glm::vec3 rayDirection;
						ImGuizmo::ComputeMouseRay(
							(float*)&view,
							(float*)&projection,
							mousePos,
							viewportPos,
							viewportSize,
							(float*)&rayOrigin,
							(float*)&rayDirection
						);

						auto result = worldManager->RayCast(rayOrigin, rayDirection);
						trans->position = result.hit ? result.hitPoint : rayOrigin + rayDirection * 15.f;

					}).get();

			}
		}
		break;
	}
	case AssetType::Scene:
	{
		if (auto scene = assetObject->AsScene())
		{
			//worldManager->LoadScene(scene);
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
	}
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
