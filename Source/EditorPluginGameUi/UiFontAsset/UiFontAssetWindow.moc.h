#pragma once

#include <EditorPluginGameUi/UiFontAsset/UiFontAsset.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.moc.h>

class plQtUiFontAssetDocumentWindow : public plQtDocumentWindow
{
  Q_OBJECT

public:
  plQtUiFontAssetDocumentWindow(plDocument* pDocument);
};
