#pragma once

#include "stdafx.h"

#include "EditorApp.h"
#include "Project/ProjectManager.h"
#include "AssetImportPopup.h"
#include "CommonData/AssetGUIDDef.h"


class ImguiLayout
{
public:
	static void DrawMainMenu(GLFWwindow* window, EditorApp* editorApp, ProjectManager* projectManager);
	static void DrawPlayControls(EditorApp* editorApp, ProjectManager* projectManager);
	static void SetDockspace(bool firstFrame);
	static void DrawSceneObjectList(EditorApp* editorApp);
	static void DrawObjectProperties(ProjectManager* projectManager);
	static void DrawAssetBrowser(ProjectManager* projectManager);
	static void DrawSceneView(EditorApp* editorApp, ProjectManager* projectManager);
	static void DrawOptions(EditorApp* editorApp);
	static void DrawStatusBar(EditorApp* editorApp);

public:
	static void DropFile(const std::string& filePath, ProjectManager* projectManager, const ImVec2& mousePos);
	static void DropFolder(const std::string& folderPath, ProjectManager* projectManager, const ImVec2& mousePos);

	static void HandleAssetDrop(const AssetMeta& meta, EditorApp* editorApp, ProjectManager* projectManager, const ImVec2& viewportPos, const ImVec2& viewportSize);
	static void HandleScriptFileDrop(const std::string& filePath, Entity target, ProjectManager* projectManager);
	static void ApplyScriptAsset(Entity target, const AssetMeta& meta, ProjectManager* projectManager);
	static void HandleUIAssetFileDrop(const std::string& filePath, Entity target, ProjectManager* projectManager);
	static void ApplyUIAsset(Entity target, const AssetMeta& meta, ProjectManager* projectManager);
	static void OpenMaterialEditor(const AssetGUID& guid);

private:
	static void DrawFolderTree(const std::string& currentPath, const std::string& parentPathStr, const std::string& rootPathStr);
	static void DrawFolderContent(ProjectManager* projectManager, const std::string& currentPath);
	static std::string GetWindowAtPosition(const ImVec2& pos);
	static bool SaveCurrentScene(ProjectManager* projectManager);
	static void DrawMaterialEditor(ProjectManager* projectManager);

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
	static AssetGUID m_currentSceneAssetGuid;
};
