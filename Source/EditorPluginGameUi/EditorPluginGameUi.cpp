#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <EditorPluginGameUi/UiImport/UiImportFigma.h>
#include <GuiFoundation/Action/ActionManager.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/StandardMenus.h>

static plActionDescriptorHandle s_hImportFromFigma;

static void MapGameUiProjectMenuActions(plStringView sMapping)
{
  plActionMap* pMap = plActionMapManager::GetActionMap(sMapping);
  if (pMap == nullptr)
    return;

  pMap->MapAction(s_hImportFromFigma, "G.Project.Assets", 2.5f);
}

void OnLoadPlugin()
{
  // "Import UI from Figma..." in every document window's Project menu.
  // Document scope (NOT Global): the action is mapped into many per-document menus, and a
  // Global proxy is cached once with the first window's document context - the next window
  // then trips the "invalid document pointer" assert in plQtProxy::GetProxy. Document scope
  // caches one proxy per document, which is the pattern all per-document menu actions use.
  {
    s_hImportFromFigma = PL_REGISTER_ACTION_1("GameUi.ImportFigma", plActionScope::Document, "GameUi", "", plUiFigmaImportAction, 0);
    plProjectActions::AddProjectMenuMapper(plMakeDelegate(&MapGameUiProjectMenuActions));
  }
  // UI Document asset
  {
    // Menu Bar
    {
      plActionMapManager::RegisterActionMap("UiDocumentAssetMenuBar").AssertSuccess();
      plStandardMenus::MapActions("UiDocumentAssetMenuBar", plStandardMenuTypes::Default | plStandardMenuTypes::Edit);
      plProjectActions::MapActions("UiDocumentAssetMenuBar");
      plDocumentActions::MapMenuActions("UiDocumentAssetMenuBar");
      plAssetActions::MapMenuActions("UiDocumentAssetMenuBar");
      plDocumentActions::MapToolsActions("UiDocumentAssetMenuBar");
      plCommandHistoryActions::MapActions("UiDocumentAssetMenuBar");
    }

    // Tool Bar
    {
      plActionMapManager::RegisterActionMap("UiDocumentAssetToolBar").AssertSuccess();
      plDocumentActions::MapToolbarActions("UiDocumentAssetToolBar");
      plCommandHistoryActions::MapActions("UiDocumentAssetToolBar", "");
      plAssetActions::MapToolBarActions("UiDocumentAssetToolBar", true);
    }
  }

  // UI Font asset
  {
    // Menu Bar
    {
      plActionMapManager::RegisterActionMap("UiFontAssetMenuBar").AssertSuccess();
      plStandardMenus::MapActions("UiFontAssetMenuBar", plStandardMenuTypes::Default | plStandardMenuTypes::Edit);
      plProjectActions::MapActions("UiFontAssetMenuBar");
      plDocumentActions::MapMenuActions("UiFontAssetMenuBar");
      plAssetActions::MapMenuActions("UiFontAssetMenuBar");
      plDocumentActions::MapToolsActions("UiFontAssetMenuBar");
      plCommandHistoryActions::MapActions("UiFontAssetMenuBar");
    }

    // Tool Bar
    {
      plActionMapManager::RegisterActionMap("UiFontAssetToolBar").AssertSuccess();
      plDocumentActions::MapToolbarActions("UiFontAssetToolBar");
      plCommandHistoryActions::MapActions("UiFontAssetToolBar", "");
      plAssetActions::MapToolBarActions("UiFontAssetToolBar", true);
    }
  }

  // UI Style asset
  {
    // Menu Bar
    {
      plActionMapManager::RegisterActionMap("UiStyleAssetMenuBar").AssertSuccess();
      plStandardMenus::MapActions("UiStyleAssetMenuBar", plStandardMenuTypes::Default | plStandardMenuTypes::Edit);
      plProjectActions::MapActions("UiStyleAssetMenuBar");
      plDocumentActions::MapMenuActions("UiStyleAssetMenuBar");
      plAssetActions::MapMenuActions("UiStyleAssetMenuBar");
      plDocumentActions::MapToolsActions("UiStyleAssetMenuBar");
      plCommandHistoryActions::MapActions("UiStyleAssetMenuBar");
    }

    // Tool Bar
    {
      plActionMapManager::RegisterActionMap("UiStyleAssetToolBar").AssertSuccess();
      plDocumentActions::MapToolbarActions("UiStyleAssetToolBar");
      plCommandHistoryActions::MapActions("UiStyleAssetToolBar", "");
      plAssetActions::MapToolBarActions("UiStyleAssetToolBar", true);
    }
  }
}

void OnUnloadPlugin()
{
  plProjectActions::RemoveProjectMenuMapper(plMakeDelegate(&MapGameUiProjectMenuActions));
  plActionManager::UnregisterAction(s_hImportFromFigma);
}

PL_PLUGIN_ON_LOADED()
{
  OnLoadPlugin();
}

PL_PLUGIN_ON_UNLOADED()
{
  OnUnloadPlugin();
}
