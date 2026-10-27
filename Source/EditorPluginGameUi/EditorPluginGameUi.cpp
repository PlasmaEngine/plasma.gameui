#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentShellPanels.h>
#include <EditorPluginGameUi/UiImport/UiImportFigma.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Configuration/Plugin.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GuiFoundation/Action/ActionManager.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/StandardMenus.h>
#include <GuiFoundation/PropertyGrid/PropertyMetaState.h>

static plActionDescriptorHandle s_hImportFromFigma;
static plActionDescriptorHandle s_hGameUiToolBarCategory;

/// The package's icon folder: the icons under their ids (":/AssetIcons/GameUi.svg" is
/// "EditorIcons/AssetIcons/GameUi.svg", ":/GameUiIcons/Icons/Anchor.svg" is "EditorIcons/GameUiIcons/Icons/Anchor.svg").
/// The plugin binary lives in <package>/Bin/<configuration>.
static plString s_sIconFolder;

static void RegisterIconFolder()
{
  plStringBuilder sPlugin, sCopy;
  plPlugin::GetPluginPaths("plEditorPluginGameUi", sPlugin, sCopy, 0);

  plStringBuilder sFolder = sPlugin.GetFileDirectory();
  sFolder.AppendPath("..", "..", "EditorIcons");
  sFolder.MakeCleanPath();

  s_sIconFolder = sFolder;
  plEditorUi::AddIconFolder(s_sIconFolder);
}

static void UnregisterIconFolder()
{
  plEditorUi::RemoveIconFolder(s_sIconFolder);
  s_sIconFolder.Clear();
}

static void MapGameUiProjectMenuActions(plStringView sMapping)
{
  plActionMap* pMap = plActionMapManager::GetActionMap(sMapping);
  if (pMap == nullptr)
    return;

  pMap->MapAction(s_hImportFromFigma, "G.Project.Assets", 2.5f);
}

static void SetVisible(plMap<plString, plPropertyUiState>& ref_props, const char* szProperty, bool bVisible)
{
  ref_props[szProperty].m_Visibility = bVisible ? plPropertyUiState::Default : plPropertyUiState::Invisible;
}

static plInt64 GetEnumValue(const plDocumentObject* pObject, const char* szProperty)
{
  return pObject->GetTypeAccessor().GetValue(szProperty).ConvertTo<plInt64>();
}

/// A screen canvas has a scale mode, a world panel a size in metres, a reach and a depth mode.
static void CanvasComponentMetaState(plPropertyMetaStateEvent& e)
{
  if (!e.m_pObject->GetTypeAccessor().GetType()->IsDerivedFrom<plUiCanvasComponent>())
    return;

  auto& props = *e.m_pPropertyStates;
  const bool bWorld = GetEnumValue(e.m_pObject, "Space") != plUiCanvasSpace::Screen;

  SetVisible(props, "ScaleMode", !bWorld);
  SetVisible(props, "ReferenceHeight", !bWorld);
  SetVisible(props, "PixelsPerMetre", bWorld);
  SetVisible(props, "WorldSize", bWorld);
  SetVisible(props, "Reach", bWorld);
  SetVisible(props, "Depth", bWorld);
}

/// The gradient's colours show with a gradient, its stops with the angled and radial ones; the size limits with Fit
/// content, the smallest font size with Shrink.
static void UiElementMetaState(plPropertyMetaStateEvent& e)
{
  const plRTTI* pType = e.m_pObject->GetTypeAccessor().GetType();
  if (!pType->IsDerivedFrom<plUiElement>())
    return;

  auto& props = *e.m_pPropertyStates;

  if (pType->IsDerivedFrom<plUiVisualElement>())
  {
    const plInt64 iGradient = GetEnumValue(e.m_pObject, "GradientMode");
    SetVisible(props, "GradientColor", iGradient != plUiGradientMode::None);
    SetVisible(props, "GradientStops", iGradient == plUiGradientMode::Angled || iGradient == plUiGradientMode::Radial);
    SetVisible(props, "GradientAngle", iGradient == plUiGradientMode::Angled);
  }

  const bool bFit = GetEnumValue(e.m_pObject, "SizeMode") == plUiSizeMode::FitContent;
  SetVisible(props, "MinSize", bFit);
  SetVisible(props, "MaxSize", bFit);

  if (pType->IsDerivedFrom<plUiText>())
    SetVisible(props, "MinFontSize", GetEnumValue(e.m_pObject, "Overflow") == plUiTextOverflow::Shrink);
}

void OnLoadPlugin()
{
  RegisterIconFolder();

  plPropertyMetaState::GetSingleton()->m_Events.AddEventHandler(&CanvasComponentMetaState);
  plPropertyMetaState::GetSingleton()->m_Events.AddEventHandler(&UiElementMetaState);

  // "Import UI from Figma..." in every document window's Project menu.
  // Document scope (NOT Global): the action is mapped into many per-document menus, and a
  // Global action is created once with the first window's document context, while every window
  // needs its own document. Document scope makes one action per document, which is the pattern
  // all per-document menu actions use.
  {
    s_hImportFromFigma = PL_REGISTER_ACTION_1("GameUi.ImportFigma", plActionScope::Document, "GameUi", "", plUiFigmaImportAction, 0);
    plProjectActions::AddProjectMenuMapper(plMakeDelegate(&MapGameUiProjectMenuActions));

    s_hGameUiToolBarCategory = PL_REGISTER_CATEGORY("GameUiCategory");
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

      // Import UI from Figma... after the asset actions, in a group of its own
      plActionMap* pMap = plActionMapManager::GetActionMap("UiDocumentAssetToolBar");
      pMap->MapAction(s_hGameUiToolBarCategory, "", 11.0f);
      pMap->MapAction(s_hImportFromFigma, "GameUiCategory", 1.0f);
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

  // the UI document's panels in the engine shell
  plUiDocumentShellPanels::Register();
}

void OnUnloadPlugin()
{
  plUiDocumentShellPanels::Unregister();
  plProjectActions::RemoveProjectMenuMapper(plMakeDelegate(&MapGameUiProjectMenuActions));
  plActionManager::UnregisterAction(s_hImportFromFigma);
  plActionManager::UnregisterAction(s_hGameUiToolBarCategory);
  plPropertyMetaState::GetSingleton()->m_Events.RemoveEventHandler(&UiElementMetaState);
  plPropertyMetaState::GetSingleton()->m_Events.RemoveEventHandler(&CanvasComponentMetaState);
  UnregisterIconFolder();
}

PL_PLUGIN_ON_LOADED()
{
  OnLoadPlugin();
}

PL_PLUGIN_ON_UNLOADED()
{
  OnUnloadPlugin();
}
