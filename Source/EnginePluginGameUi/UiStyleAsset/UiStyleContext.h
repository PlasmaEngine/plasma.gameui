#pragma once

#include <Core/Utils/Blackboard.h>
#include <EditorEngineProcessFramework/EngineProcess/EngineProcessDocumentContext.h>
#include <EnginePluginGameUi/EnginePluginGameUiDLL.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>

/// \brief Engine-side preview of a UI Style asset.
///
/// Builds a small SHOWCASE document in code (button, disabled button, toggle, progress
/// bar, slider, text — all referencing the edited style) and renders it through a normal
/// plUiCanvasComponent. Mouse input is always injected, so hover/pressed/checked states
/// preview live. The editor pushes style edits via plResourceUpdateMsgToEngine
/// ("UI Style"), which hot-reloads the style resource the showcase elements point at.
class PL_ENGINEPLUGINGAMEUI_DLL plUiStyleContext : public plEngineProcessDocumentContext
{
  PL_ADD_DYNAMIC_REFLECTION(plUiStyleContext, plEngineProcessDocumentContext);

public:
  plUiStyleContext();
  ~plUiStyleContext();

protected:
  virtual void OnInitialize() override;
  virtual void HandleMessage(const plEditorEngineDocumentMsg* pMsg) override;

  virtual plEngineProcessViewContext* CreateViewContext() override;
  virtual void DestroyViewContext(plEngineProcessViewContext* pContext) override;

private:
  plGameObject* m_pMainObject = nullptr;
  plComponentHandle m_hCanvasComponent;
  plUiStyleResourceHandle m_hStyle;
  plUiDocumentResourceHandle m_hShowcaseDocument;
  plSharedPtr<plBlackboard> m_pBlackboard;
};
