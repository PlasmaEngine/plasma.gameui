#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetDocument.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontAssetWindow.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontPreviewView.h>
#include <Foundation/Strings/TranslationLookup.h>

plUiFontAssetDocumentWindow::plUiFontAssetDocumentWindow(plDocument* pDocument)
  : plDocumentWindow(pDocument)
{
  SetMenuBarActions("UiFontAssetMenuBar");
  SetToolBarActions("UiFontAssetToolBar");

  AddPropertiesPanel({"Properties", plTranslate("GameUi.Panel.FontProperties")});

  // W5-D12: the baked font, docked below the properties (plUiDocumentShellPanels)
  AddPanel<plUiFontPreviewView>({"FontPreview", "Preview"}, static_cast<plAssetDocument*>(pDocument));

  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);
}
