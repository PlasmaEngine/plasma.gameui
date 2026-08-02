#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessViewContext.h>

class plUiDocumentContext;

class plUiDocumentViewContext : public plEngineProcessViewContext
{
public:
  plUiDocumentViewContext(plUiDocumentContext* pUiDocumentContext);
  ~plUiDocumentViewContext();

  bool UpdateThumbnailCamera(const plBoundingBoxSphere& bounds);

protected:
  virtual plViewHandle CreateView() override;
  virtual void SetCamera(const plViewRedrawMsgToEngine* pMsg) override;

  void DrawSelectionGizmos(float fViewWidth, float fViewHeight) const;

  plUiDocumentContext* m_pUiDocumentContext;
};
