#pragma once

#include <Core/Utils/Blackboard.h>
#include <EditorEngineProcessFramework/EngineProcess/EngineProcessDocumentContext.h>
#include <EnginePluginGameUi/EnginePluginGameUiDLL.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>
#include <GameUiPlugin/Elements/UiTextFit.h>

/// \brief One drag-snap alignment guide line, mirrored from the editor while dragging.
struct plUiPreviewGuide
{
  bool m_bVertical = true;
  float m_fPos = 0.0f; ///< canvas-space x (vertical) or y (horizontal)
  float m_fMin = 0.0f; ///< extent along the other axis
  float m_fMax = 0.0f;
};

class PL_ENGINEPLUGINGAMEUI_DLL plUiDocumentContext : public plEngineProcessDocumentContext
{
  PL_ADD_DYNAMIC_REFLECTION(plUiDocumentContext, plEngineProcessDocumentContext);

public:
  plUiDocumentContext();
  ~plUiDocumentContext();

  const plUiDocumentResourceHandle& GetResource() const { return m_hMainResource; }

  const plDynamicArray<plUiPreviewGuide>& GetSnapGuides() const { return m_SnapGuides; }

  /// \brief Index paths (into the root element's children) of the elements selected in the editor.
  const plDynamicArray<plDynamicArray<plUInt32>>& GetSelectedElementPaths() const { return m_SelectedPaths; }

  const plComponentHandle& GetCanvasComponent() const { return m_hCanvasComponent; }
  bool GetShowSafeArea() const { return m_bShowSafeArea; }
  bool GetShowNavGraph() const { return m_bShowNavGraph; }
  bool HasDevicePreset() const { return m_bHasDevicePreset; }

  /// \brief The Text fit check of the preview as of the last update; the view context marks the texts that do not
  /// fit, the editor lists them.
  const plDynamicArray<plUiTextFitIssue>& GetTextFitIssues() const { return m_TextFitIssues; }

protected:
  virtual void OnInitialize() override;
  virtual void HandleMessage(const plEditorEngineDocumentMsg* pMsg) override;
  virtual void UpdateDocumentContext() override; ///< pushes draw stats to the editor when they change

  virtual plEngineProcessViewContext* CreateViewContext() override;
  virtual void DestroyViewContext(plEngineProcessViewContext* pContext) override;
  virtual bool UpdateThumbnailViewContext(plEngineProcessViewContext* pThumbnailViewContext) override;

private:
  /// Runs the Text fit check at the view's size and sends the issues to the editor when they changed.
  void UpdateTextFit(const plUiCanvasComponent& component);
  plVec2 GetPreviewViewportSize() const;

  plDynamicArray<plUiTextFitIssue> m_TextFitIssues;
  plString m_sLastSentTextFit;

  plGameObject* m_pMainObject = nullptr;
  plComponentHandle m_hCanvasComponent;
  plUiDocumentResourceHandle m_hMainResource;
  plSharedPtr<plBlackboard> m_pMockBlackboard;
  plEvent<const plBlackboard::EntryEvent&>::Unsubscriber m_MockBoardUnsubscriber;
  plDynamicArray<plDynamicArray<plUInt32>> m_SelectedPaths;
  plDynamicArray<plUiPreviewGuide> m_SnapGuides;
  bool m_bShowSafeArea = false;
  bool m_bShowNavGraph = false;
  bool m_bHasDevicePreset = false;
  plUInt32 m_uiLastSentBatches = 0xFFFFFFFF;
  plUInt32 m_uiLastSentQuads = 0xFFFFFFFF;
};
