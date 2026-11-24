#pragma once

#include "stdafx.h"

#include "EditorApp.h"
#include "Project/ProjectManager.h"
#include "AssetImportPopup.h"
#include "CommonData/AssetGUIDDef.h"

#include <functional>


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

	// 贴图 <-> 精灵互转：都是用同一张原图（不复制文件）走一遍导入弹窗，配置默认从原资产带过来，
	// 用户可以改完再点确定。导入成功后回调拿到新资产（取消导入就不回调）。原资产不对或读不到原图时返回 false。
	// 同一张图已经有同类资产（元文件名"前缀_编号_原图文件名"）时先弹提示：可以用已有的、仍然新建或取消
	static bool RequestSpriteFromTexture(const AssetGUID& textureGuid, ProjectManager* projectManager, std::function<void(const AssetMeta&)> onCreated = {});
	static bool RequestTextureFromSprite(const AssetGUID& spriteGuid, ProjectManager* projectManager, std::function<void(const AssetMeta&)> onCreated = {});

private:
	static bool RequestProjectFlow(ProjectManager* projectManager, const std::string& action, bool needSaveConfirm);

	static void DrawFolderTree(const std::string& currentPath, const std::string& parentPathStr, const std::string& rootPathStr);
	static void DrawFolderContent(ProjectManager* projectManager, const std::string& currentPath);
	static std::string GetWindowAtPosition(const ImVec2& pos);
	static bool SaveCurrentScene(ProjectManager* projectManager);
	static void DrawMaterialEditor(ProjectManager* projectManager);
	// 贴图 <-> 精灵互转时，同一张图已经有同类资产的提示框
	static void DrawImageConvertConfirm();

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
	static bool s_openStopPlayConfirmPopup;
	static bool s_pendingNeedSaveConfirm;

	static AssetType importAssetType;

	static std::string m_selectedFolder;

	static AssetImportPopup s_assetImportPopup;
	static AssetGUID m_currentSceneAssetGuid;
};
