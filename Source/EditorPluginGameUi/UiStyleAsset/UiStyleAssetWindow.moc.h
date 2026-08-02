#pragma once

#include <EditorFramework/DocumentWindow/EngineDocumentWindow.moc.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <EditorPluginGameUi/UiStyleAsset/UiStyleAsset.h>

class plQtEngineViewWidget;

/// \brief UI Style asset editor: property grid + LIVE engine-rendered showcase
/// (button/toggle/bar/slider/text using the style — see plUiStyleContext).
///
/// The preview is always interactive: mouse input over the viewport is forwarded so
/// hover/pressed/checked states and slider drags render with the edited values.
class plQtUiStyleAssetDocumentWindow : public plQtEngineDocumentWindow
{
  Q_OBJECT

public:
  plQtUiStyleAssetDocumentWindow(plAssetDocument* pDocument);
  ~plQtUiStyleAssetDocumentWindow();

protected:
  virtual void InternalRedraw() override;
  virtual bool eventFilter(QObject* pObject, QEvent* pEvent) override;

private:
  void SendRedrawMsg();

  /// \brief Pushes the current style into the engine process resource, so the preview updates without a transform.
  void UpdatePreview();
  void RestoreResource();

  void PropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e);
  void SendMouseState(const QPoint& viewPos, bool bButtonDown, float fWheelTicks = 0.0f);

  plEngineViewConfig m_ViewConfig;
  plQtEngineViewWidget* m_pViewWidget = nullptr;
  plUiStyleAssetDocument* m_pAssetDoc = nullptr;
};
