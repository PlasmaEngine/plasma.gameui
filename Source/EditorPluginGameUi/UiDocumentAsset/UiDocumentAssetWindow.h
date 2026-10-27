#pragma once

#include <EditorEngineProcessFramework/EngineProcess/ViewRenderSettings.h>
#include <EditorFramework/DocumentWindow/EngineDocumentWindow.h>
#include <EditorFramework/InputContexts/EditorInputEvent.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiAnimationsPanel.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiBindingsPanel.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiCanvasViews.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAsset.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiLocalization.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiPropertyViews.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiStatesPanel.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiTextFitViews.h>
#include <EditorUi/Runtime.h>
#include <GameUiPlugin/Elements/UiTextFit.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

#include <functional>

class plEngineViewWidget;
class plTranslatorFromFiles;
class plUiBindingsView;
class plUiHierarchyView;
class plUiPaletteView;
class plUiStatesView;
struct plSelectionManagerEvent;

/// \brief The UI document window: the canvas view with its rulers, status row and toolbar row, and the palette,
/// hierarchy, rect transform, effective style, bindings, states and animations panels around it
/// (plUiDocumentShellPanels arranges them in the engine shell).
class plUiDocumentAssetDocumentWindow : public plEngineDocumentWindow
{
public:
  plUiDocumentAssetDocumentWindow(plAssetDocument* pDocument);
  ~plUiDocumentAssetDocumentWindow();

  struct ElementListEntry
  {
    plUuid m_Guid;
    plString m_sLabel;
    const plDocumentObject* m_pObject = nullptr;
  };

  static void GetBindablePropertiesForType(const plRTTI* pType, plDynamicArray<plString>& out_bindingProps);
  static const char* GetBindingPropertyDisplayName(plStringView sBindingProperty);

  /// The canvas chrome the engine shell shows along the view's edges: the rulers and the status row. The window owns
  /// them; the shell lends them to its view panel.
  plUiRulerView* GetHorizontalRuler() const { return m_pHRulerView.Borrow(); }
  plUiRulerView* GetVerticalRuler() const { return m_pVRulerView.Borrow(); }
  plUiCanvasStatusRow* GetCanvasStatusRow() const { return m_pCanvasStatusRow.Borrow(); }

protected:
  virtual void InternalRedraw() override;
  virtual void ProcessMessageEventHandler(const plEditorEngineDocumentMsg* pMsg) override;

private:
  void SendRedrawMsg();

  /// \brief Pushes the current widget tree into the engine process resource, so the preview updates without a transform.
  void UpdatePreview();
  void RestoreResource();

  void PropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void StructureEventHandler(const plDocumentObjectStructureEvent& e);
  void SelectionEventHandler(const plSelectionManagerEvent& e);

  // ————— selection → engine gizmos —————

  void SendSelectionToEngine();
  bool ComputeElementPath(const plDocumentObject* pObject, plHybridArray<plUInt32, 8>& out_path) const;
  const plDocumentObject* GetPropertiesObject() const;
  const plDocumentObject* GetElementDocObject(plArrayPtr<const plUInt32> path) const;

  // ————— canvas mouse editing —————

  struct HitResult
  {
    const plUiElement* m_pElement = nullptr;
    plHybridArray<plUInt32, 8> m_Path;
    plRectFloat m_Rect = plRectFloat(0, 0, 0, 0);       ///< in reference (unscaled) canvas space
    plRectFloat m_ParentRect = plRectFloat(0, 0, 0, 0); ///< parent rect in reference space
  };

  /// canvas-space to engine-screen-space mapping, mirroring plUiCanvasComponent::ComputeCanvasTransform
  void GetCanvasTransform(float& out_fScale, plVec2& out_vOffset, plVec2& out_vCanvasSize) const;
  float GetCanvasScale() const;
  plRectFloat GetCanvasRect() const;                   ///< in canvas (reference) space
  plVec2 WidgetToCanvasPos(const plVec2I32& viewPos) const;
  void HitTestRecursive(const plUiElement* pElement, const plRectFloat& rect, const plRectFloat& parentRect, const plVec2& vPos, plHybridArray<plUInt32, 8>& ref_path, HitResult& ref_result) const;
  bool HitTest(const plVec2& vRefPos, HitResult& out_result) const;
  bool GetSelectedElementRect(HitResult& out_result) const;

  void BeginDrag(const plVec2I32& viewPos);
  void UpdateDrag(const plVec2I32& viewPos);
  void EndDrag(bool bApply);

  // ————— canvas input —————

  /// One input event on the canvas, in logical units of the view.
  struct CanvasInput
  {
    enum class Type : plUInt8
    {
      Press,
      Release,
      Move,
      Wheel,
      Key,
    };

    Type m_Type = Type::Move;
    plUInt8 m_uiButton = plEditorInputEvent::NoButton; ///< the button pressed or released
    bool m_bLeftHeld = false;                          ///< the left button is down after the event
    plVec2I32 m_Position = plVec2I32::MakeZero();
    float m_fWheelTicks = 0.0f;
    plEditorPlatformKey m_Key = plEditorPlatformKey::Unknown; ///< for Type::Key
    plUInt32 m_uiCharacter = 0;                               ///< a typed Unicode code point, for Type::Key
  };

  /// What the canvas does with input: panning, zoom, Delete, the interact preview and the edit drags. True when used.
  bool HandleCanvasInput(const CanvasInput& input);

  /// The view's input from the engine shell's view slot.
  void OnViewInput(const plEditorInputEvent& e);

  // ————— Anchors edit mode —————

  bool IsAnchorsMode() const;
  void BeginAnchorDrag(const plVec2I32& viewPos); ///< hit-tests the selection's anchor diamonds

  // ————— drag snapping (Snap toolbar toggle) —————

  /// one matched alignment, drawn as a guide line by the engine preview
  struct SnapGuide
  {
    bool m_bVertical = true;
    float m_fPos = 0.0f; ///< canvas-space x (vertical) or y (horizontal)
    float m_fMin = 0.0f; ///< line extent along the other axis
    float m_fMax = 0.0f;
  };

  /// gathers the canvas rects of every element except the dragged subtree (+ canvas rect)
  void CollectSnapRects(const plHybridArray<plUInt32, 8>& excludePath);
  /// adjusts the drag deltas so the dragged rect/edge aligns with the nearest candidate
  void SnapDragDeltas(float& ref_fDx, float& ref_fDy, plHybridArray<SnapGuide, 4>& out_guides) const;
  void SendGuidesToEngine(plArrayPtr<const SnapGuide> guides);

  // ————— hierarchy panel + palette —————

  void CreatePalettePanel();
  void CreateHierarchyPanel();
  void RebuildHierarchy();
  void SyncHierarchySelection();
  void SelectElements(plArrayPtr<const plUuid> elements);
  void ToggleElementFlag(const plDocumentObject* pObject, bool bLock); ///< Visible, or EditorLocked with bLock
  void MoveElements(plArrayPtr<const plDocumentObject* const> objects, const plDocumentObject* pNewParent, int iIndex);
  void AddElement(const plRTTI* pType);
  void AddElementAt(const plRTTI* pType, const plVec2I32& viewPos); ///< palette drop: place at the cursor
  void RemoveSelectedElements();

  // ————— canvas view, chrome and toolbar / interact mode —————

  void CreateCanvasView();
  void CreateCanvasChrome(); ///< the rulers and the status row
  void CreateCanvasToolBar();
  void OnCanvasToolBarChanged(plUiCanvasToolBarView::Control control, plInt32 iValue);
  void SetDevicePreset(plInt32 iPreset);
  void SetZoomPreset(plInt32 iPreset);
  void SyncCanvasToolBar(); ///< shows the canvas state in the toolbar
  void SetInteractMode(bool bEnabled);

  // ————— localization preview —————

  void CollectLanguages();     ///< subfolders of <project>/Localization and <project>/Editor/Localization
  void SendLanguageToEngine(); ///< "UiLanguage" folders, "UiPseudo" and "UiHighlightUntranslated", then UpdatePreview

  /// The language the preview translates with: the selected one, or English under the pseudo-localization.
  const plGameUiLocalization::Language* GetPreviewLanguage() const;

  /// The editor's own layout (hit tests, selection rects) measures the texts the preview shows.
  void UpdateLayoutContext();

  /// The strip's label of a language: its native name when the editor font can draw it, else its English name.
  plString GetLanguageLabel(plStringView sLanguage) const;

  // ————— fit content and the Text fit panel —————

  /// One Text fit issue of the preview, as the engine sent it ("UiTextFit").
  struct TextFitIssue
  {
    plUiTextFitIssue::Kind m_Kind = plUiTextFitIssue::Kind::Overflow;
    plHybridArray<plUInt32, 8> m_Path;
    float m_fNeeded = 0.0f;
    float m_fAvailable = 0.0f;
    float m_fShrunkSize = 0.0f;
    plString m_sFont; ///< resource id (the font asset's guid)
    plString m_sText;
    plDynamicArray<plUInt32> m_MissingGlyphs;
  };

  void CreateTextFitPanels();
  void ParseTextFit(plStringView sPayload);
  void UpdateTextFitPanel();
  void UpdateSizeFitPanel();

  /// The text element an element shows: itself, or its first text child (a button's label).
  const plDocumentObject* FindLabelObject(const plDocumentObject* pObject) const;
  plString GetIssueTitle(const TextFitIssue& issue) const;
  plString GetIssueDetail(const TextFitIssue& issue) const;
  plString GetFontName(plStringView sFontResource) const;

  void OnSizeModeChanged(bool bFit);
  void OnWidthLimitsChanged(float fMin, float fMax, bool bFinal);
  void OnOverflowChanged(plInt32 iOverflow);
  void OnTextFitIssueActivated(plUInt32 uiIndex);
  void OnAddCharacters(plUInt32 uiIndex);

  /// Fit content with the default limits, inside the caller's transaction.
  void ApplyFitContent(const plDocumentObject* pObject);
  void FitAllButtons();
  void SendInteractMouseState(const plVec2I32& viewPos, bool bButtonDown, float fWheelTicks = 0.0f);
  void SendCanvasViewToEngine();
  void RefreshCanvasView(); ///< sends the view state and updates the selection readouts

  // ————— bindings & mock data —————

  void CreateBindingsPanel();
  void UpdateBindingsList();
  void SendMockBlackboardToEngine();
  void EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e);

  void OnAddBindingRequested();
  void OnRemoveBindingRequested(const plUuid& elementGuid, plStringView sBindingProperty);
  void OnBindingEntryEdited(const plUuid& elementGuid, plStringView sBindingProperty, plStringView sNewEntry);
  void OnBindingRowActivated(const plUuid& elementGuid);

  // ————— mock scenario presets (stored on the asset, tool-managed) —————

  void UpdateScenariosList();
  void OnScenarioSaveRequested(plStringView sName);
  void OnScenarioDeleteRequested(plStringView sName);
  void OnScenarioSelected(plStringView sName);
  const plDocumentObject* FindScenarioObject(plStringView sName) const;

  // ————— animation timeline panel —————

  void CreateAnimationsPanel();
  void UpdateAnimationsList();
  void OnClipRenamed(const plUuid& clipGuid, plStringView sName);
  void OnClipDurationChanged(const plUuid& clipGuid, float fDuration);
  void OnAnimationEditsBegin(plStringView sName);
  void OnAnimationEditsEnd(bool bCommit);
  void OnAddClipRequested();
  void OnRemoveClipRequested(const plUuid& clipGuid);
  void OnClipActivated(const plUuid& clipGuid);
  void OnAddTrackRequested(const plUuid& clipGuid);
  void OnRemoveTrackRequested(const plUuid& trackGuid);
  void OnAddKeyRequested(const plUuid& trackGuid, float fTime, bool bSelectKey = true);
  void OnRemoveKeyRequested(const plUuid& keyGuid);
  void OnKeyMoved(const plUuid& keyGuid, float fNewTime);
  void OnKeyEdited(const plUiAnimationsPanel::KeyInfo& key);
  void OnClipScrubbed(plStringView sClipName, float fTime);
  void OnClipPlay(plStringView sClipName);
  void OnClipStop();

  // ————— states & transitions panel —————

  void CreateStatesPanel();
  void UpdateStatesPanel();
  void OnElementStringPropertyChanged(const plUuid& elementGuid, const char* szProperty, plStringView sValue);
  void OnAddStateRequested(const plUuid& elementGuid);
  void OnRemoveStateRequested(const plUuid& stateGuid);
  void OnStateEdited(const plUiStatesPanelBase::StateRow& state);
  void OnMockTestValue(plStringView sEntry, plStringView sValue);

  void CollectElementList(plDynamicArray<ElementListEntry>& out_elements) const;

  /// Reads the referenced Blackboard Template's compiled entries (design-time binding contract).
  void LoadTemplateEntries();

  // ————— rect transform panel + status bar —————

  void CreateRectTransformPanel();
  void ApplyAnchorPreset(plInt8 iCol, plInt8 iRow); ///< 0..2 = left/center/right (top/middle/bottom), 3 = stretch
  void ApplyOffsets(const plVec4& vOffsets); ///< "Edit UI Rect"
  void UpdateSelectionUi();

  /// "Anchor: Left-Top", or the anchor fractions when they are no preset.
  static plString ComputeAnchorSummary(const plUiElement* pElement);

  /// A message in the canvas status row.
  void SetCanvasStatusMessage(plStringView sMessage);

  // ————— effective-style inspector (which cascade level set each value) —————

  void UpdateEffectiveStyle();
  /// reads the referenced UI Style asset's COMPILED output (LoadTemplateEntries pattern);
  /// returns false when unresolvable / not yet transformed
  bool LoadCompiledStyle(const plString& sStyleRef, class plUiStyle& out_style) const;

  plEngineViewConfig m_ViewConfig;
  plEngineViewWidget* m_pViewWidget = nullptr;
  plUiDocumentAssetDocument* m_pAssetDoc = nullptr;

  // the panels: the window's panel contents, except the bindings and states views, which keep the state their
  // contents draw
  plUniquePtr<plUiBindingsView> m_pBindingsView;
  plUniquePtr<plUiStatesView> m_pStatesView;
  plUiBindingsPanelBase* m_pBindingsPanel = nullptr;
  plUiStatesPanelBase* m_pStatesPanel = nullptr;
  plUiAnimationsPanel* m_pAnimationsPanel = nullptr;
  plUiCanvasToolBarView* m_pCanvasToolBarView = nullptr;
  plUiPaletteView* m_pPaletteView = nullptr;
  plUiHierarchyView* m_pHierarchyView = nullptr;
  plUiRectTransformView* m_pRectTransformView = nullptr;
  plUiEffectiveStyleView* m_pEffectiveStyleView = nullptr;
  plUiSizeFitView* m_pSizeFitView = nullptr;
  plUiTextFitView* m_pTextFitView = nullptr;

  // the canvas chrome (GetHorizontalRuler etc.)
  plUniquePtr<plUiRulerView> m_pHRulerView;
  plUniquePtr<plUiRulerView> m_pVRulerView;
  plUniquePtr<plUiCanvasStatusRow> m_pCanvasStatusRow;

  // canvas toolbar state
  bool m_bAnchorsMode = false;
  bool m_bSnap = true;
  bool m_bSafeArea = false;
  bool m_bNavGraph = false;
  bool m_bCheckerboard = false;
  bool m_bHighlightUntranslated = false;
  plInt32 m_iDevicePreset = 0;
  plString m_sLanguage; ///< empty = no language
  plDynamicArray<plString> m_Languages;
  plDynamicArray<plGameUiLocalization::Language> m_LanguageInfos;
  bool m_bPseudoLocalization = false; ///< the strip's "Pseudo +40%"

  plUniquePtr<plTranslatorFromFiles> m_pPreviewTranslator;
  plUiLayoutContext m_LayoutContext;

  plDynamicArray<TextFitIssue> m_TextFitIssues;
  bool m_bWidthTransactionOpen = false;

  plDynamicArray<plUiBindingsPanelBase::MockEntry> m_TemplateEntries;
  plString m_sLoadedTemplate; ///< template ref the entries were loaded from
  bool m_bIgnoreHierarchySelection = false;

  bool m_bInteractMode = false;
  bool m_bLastInteractButtonDown = false;

  // canvas view state (mirrored to the engine preview)
  plVec2U32 m_vDeviceResolution = plVec2U32::MakeZero(); ///< 0 = fill viewport
  float m_fZoom = 0.0f;                                  ///< 0 = fit
  plVec2 m_vPan = plVec2::MakeZero();
  bool m_bPanning = false;
  plUInt8 m_uiPanButton = plEditorInputEvent::NoButton; ///< the button that started the pan
  plVec2I32 m_PanStartPos = plVec2I32::MakeZero();
  plVec2 m_vPanStartValue = plVec2::MakeZero();

  plEditorUiTimerGroup m_Timers; ///< engine process events handled after the frame

  void SetEngineConnectedIndicator(bool bConnected);

  enum class DragMode
  {
    None,
    Move,
    Resize,
    Anchor ///< Anchors edit mode: dragging one of the selection's anchor diamonds
  };

  DragMode m_DragMode = DragMode::None;
  plInt8 m_iResizeX = 0; ///< -1 = left edge, 0 = none, 1 = right edge
  plInt8 m_iResizeY = 0; ///< -1 = top edge, 0 = none, 1 = bottom edge
  plVec2I32 m_DragStartPos = plVec2I32::MakeZero();
  plRectFloat m_DragStartRect = plRectFloat(0, 0, 0, 0); ///< dragged element's rect at drag start (canvas space)
  plDynamicArray<plRectFloat> m_SnapRects;               ///< snap candidates collected at drag start
  plString m_sLastSentGuides;                            ///< last "UiGuides" payload (dedupe)

  // anchor drag: which anchor component each axis edits; 0 = untouched, -1 = min, 1 = max,
  // 2 = coincident diamond, decided by the first clear drag direction
  plInt8 m_iAnchorDragX = 0;
  plInt8 m_iAnchorDragY = 0;
  plRectFloat m_DragStartParentRect = plRectFloat(0, 0, 0, 0);
  bool m_bDragContainerChild = false; ///< parent is a stack/grid: offsets.x/y are dead, z/w = preferred size
  plVec4 m_vDragStartOffsets = plVec4::MakeZero();
  plVec2 m_vDragAnchorMin = plVec2::MakeZero();
  plVec2 m_vDragAnchorMax = plVec2::MakeZero();
  plUuid m_DragObjectGuid;
  bool m_bDragTransactionOpen = false;
};
