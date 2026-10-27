#pragma once

#include <EditorPluginGameUi/UiFontAsset/UiFontAsset.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.h>

/// \brief The UI font window: the font's properties, with the baked font previewed below them (W5-D12).
class plUiFontAssetDocumentWindow : public plDocumentWindow
{
public:
  plUiFontAssetDocumentWindow(plDocument* pDocument);
};
