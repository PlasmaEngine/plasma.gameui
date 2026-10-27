#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessViewContext.h>

class plDebugRendererContext;
class plUiDocumentContext;

class plUiDocumentViewContext : public plEngineProcessViewContext
{
public:
  plUiDocumentViewContext(plUiDocumentContext* pUiDocumentContext);
  ~plUiDocumentViewContext();

  bool UpdateThumbnailCamera(const plBoundingBoxSphere& bounds);

  /// Draws the selection, anchors, guides and canvas frame for this frame, at the window size of the last redraw.
  /// The document context calls it every frame: debug drawings last one frame, and a view can render in a frame
  /// whose redraw message came in after the drawings of that frame were taken.
  void DrawSelectionGizmos() const;

  /// The view's size in screen px, as of the last redraw.
  plVec2 GetWindowSize() const { return plVec2(m_fWindowWidth, m_fWindowHeight); }

protected:
  virtual plViewHandle CreateView() override;
  virtual void SetCamera(const plViewRedrawMsgToEngine* pMsg) override;

  void DrawSelectionGizmos(float fViewWidth, float fViewHeight) const;

  /// A red dot at the top right of every text the Text fit check found a problem with (shrunk texts fit, so they get
  /// none).
  void DrawTextFitMarks(const plDebugRendererContext& context, float fScale, const plVec2& vOffset) const;

  plUiDocumentContext* m_pUiDocumentContext;
  float m_fWindowWidth = 0.0f;  ///< of the last redraw
  float m_fWindowHeight = 0.0f;
};
