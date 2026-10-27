#pragma once

#include <EditorFramework/DocumentWindow/EngineDocumentWindow.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <EditorPluginGameUi/UiStyleAsset/UiStyleAsset.h>

class plEngineViewWidget;
struct plEditorInputEvent;

/// \brief UI Style asset editor: property grid + LIVE engine-rendered showcase
/// (button/toggle/bar/slider/text using the style — see plUiStyleContext).
///
/// The preview is always interactive: mouse input over the viewport is forwarded so
/// hover/pressed/checked states and slider drags render with the edited values.
class plUiStyleAssetDocumentWindow : public plEngineDocumentWindow
{
public:
  plUiStyleAssetDocumentWindow(plAssetDocument* pDocument);
  ~plUiStyleAssetDocumentWindow();

protected:
  virtual void InternalRedraw() override;

private:
  void SendRedrawMsg();

  /// \brief Pushes the current style into the engine process resource, so the preview updates without a transform.
  void UpdatePreview();
  void RestoreResource();

  void PropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e);

  /// The view's mouse and wheel input goes to the showcase (no camera controls here).
  void OnViewInput(const plEditorInputEvent& e);
  void SendMouseState(const plVec2& vViewPos, bool bButtonDown, float fWheelTicks = 0.0f);

  plEngineViewConfig m_ViewConfig;
  plEngineViewWidget* m_pViewWidget = nullptr;
  plUiStyleAssetDocument* m_pAssetDoc = nullptr;
};
