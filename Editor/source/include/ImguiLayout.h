#pragma once

#include "stdafx.h"

#include "WorldManager.h"
#include "Project/ProjectManager.h"
#include "AssetImportPopup.h"


class ImguiLayout
{
public:
	static void DrawMainMenu(GLFWwindow* window, ProjectManager* projectManager);
	static void SetDockspace(bool firstFrame);
	static void DrawSceneObjectList(WorldManager* worldManager);
	static void DrawObjectProperties();
	static void DrawAssetBrowser(ProjectManager* projectManager);
	static void DrawSceneView(WorldManager* worldManager, ProjectManager* projectManager);
	static void DrawOptions(WorldManager* worldManager);
	static void DrawStatusBar(WorldManager* worldManager);

public:
	static void DropFile(const std::string& filePath, ProjectManager* projectManager, const ImVec2& mousePos);
	static void DropFolder(const std::string& folderPath, ProjectManager* projectManager, const ImVec2& mousePos);

	static void HandleAssetDrop(const AssetMeta& meta, WorldManager* worldManager, ProjectManager* projectManager, const ImVec2& viewportPos, const ImVec2& viewportSize);

private:
	static void DrawFolderTree(const std::string& currentPath, const std::string& parentPathStr, const std::string& rootPathStr);
	static void DrawFolderContent(ProjectManager* projectManager, const std::string& currentPath);
	static std::string GetWindowAtPosition(const ImVec2& pos);

public:
	static ImGuizmo::OPERATION mCurrentGizmoOperation;
	static ImGuizmo::MODE mCurrentGizmoMode;
	static bool useSnap;
	static float snap[3];
	static Entity selectedEntity;

	static char m_newProjectPathBuffer[1000];
	static char m_newProjectName[1000];

	static std::string m_pendingAction;

	static char m_wantOpenProjectPathBuffer[1000];

	static bool s_openSaveConfirmPopup;
	static bool s_openNewProjectPopup;

	static AssetType importAssetType;

	static std::string m_selectedFolder;

	static AssetImportPopup s_assetImportPopup;
};
