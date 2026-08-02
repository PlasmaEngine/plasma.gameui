#pragma once

#include <EditorEngineProcessFramework/EngineProcess/ViewRenderSettings.h>
#include <EditorFramework/DocumentWindow/EngineDocumentWindow.moc.h>
#include <EditorFramework/IPC/EngineProcessConnection.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiAnimationsPanel.moc.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiBindingsPanel.moc.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAsset.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiStatesPanel.moc.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

#include <QTreeWidget>

#include <functional>

class plQtEngineViewWidget;
class QTreeWidgetItem;
class QDoubleSpinBox;
class QLabel;
class QAction;
class QComboBox;
class QTableWidget;
struct plSelectionManagerEvent;

/// \brief Ruler strip around the canvas view, showing canvas-space pixel coordinates.
class plQtUiRulerWidget : public QWidget
{
public:
  plQtUiRulerWidget(Qt::Orientation orientation, QWidget* pParent);

  /// (out: canvas->widget scale, canvas origin in widget coordinates along this ruler's axis)
  std::function<void(float&, float&)> m_GetTransform;

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;

private:
  Qt::Orientation m_Orientation;
};

/// \brief Hierarchy tree with drag & drop reordering/reparenting; the drop is forwarded to
/// the document instead of moving the Qt items directly.
class plQtUiHierarchyTreeWidget : public QTreeWidget
{
  Q_OBJECT

public:
  explicit plQtUiHierarchyTreeWidget(QWidget* pParent);

  /// (dragged items, new parent item or nullptr for root, insertion index or -1 for append)
  std::function<void(const QList<QTreeWidgetItem*>&, QTreeWidgetItem*, int)> m_OnMoveItems;

  std::function<void()> m_OnDeleteKey;

protected:
  virtual void dropEvent(QDropEvent* pEvent) override;
  virtual void keyPressEvent(QKeyEvent* pEvent) override;
};

class plQtUiDocumentAssetDocumentWindow : public plQtEngineDocumentWindow
{
  Q_OBJECT

public:
  plQtUiDocumentAssetDocumentWindow(plAssetDocument* pDocument);
  ~plQtUiDocumentAssetDocumentWindow();

  struct ElementListEntry
  {
    plUuid m_Guid;
    QString m_sLabel;
    const plDocumentObject* m_pObject = nullptr;
  };

  static void GetBindablePropertiesForType(const plRTTI* pType, plDynamicArray<plString>& out_bindingProps);
  static const char* GetBindingPropertyDisplayName(plStringView sBindingProperty);

protected:
  virtual void InternalRedraw() override;
  virtual void ProcessMessageEventHandler(const plEditorEngineDocumentMsg* pMsg) override;
  virtual bool eventFilter(QObject* pObject, QEvent* pEvent) override;

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
  plVec2 WidgetToCanvasPos(const QPoint& viewPos) const;
  void HitTestRecursive(const plUiElement* pElement, const plRectFloat& rect, const plRectFloat& parentRect, const plVec2& vPos, plHybridArray<plUInt32, 8>& ref_path, HitResult& ref_result) const;
  bool HitTest(const plVec2& vRefPos, HitResult& out_result) const;
  bool GetSelectedElementRect(HitResult& out_result) const;

  void BeginDrag(const QPoint& viewPos);
  void UpdateDrag(const QPoint& viewPos);
  void EndDrag(bool bApply);

  // ————— Anchors edit mode —————

  bool IsAnchorsMode() const;
  void BeginAnchorDrag(const QPoint& viewPos); ///< hit-tests the selection's anchor diamonds
  void UpdateAnchorSummary(const plUiElement* pElement); ///< "Anchor: Left-Mid" label in the rect panel

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

  QWidget* CreatePaletteWidget(QWidget* pParent);
  QWidget* CreateHierarchyWidget(QWidget* pParent);
  void RebuildHierarchy();
  void SyncHierarchySelection();
  void OnHierarchySelectionChanged();
  void OnHierarchyItemChanged(QTreeWidgetItem* pItem, int iColumn);
  void OnHierarchyContextMenu(const QPoint& pos);
  void OnHierarchyItemsMoved(const QList<QTreeWidgetItem*>& items, QTreeWidgetItem* pNewParentItem, int iIndex);
  void AddElement(const plRTTI* pType);
  void AddElementAt(const plRTTI* pType, const QPoint& viewPos); ///< palette drop: place at the cursor
  void RemoveSelectedElements();

  // ————— canvas toolbar / interact mode —————

  void CreateCanvasToolBar();
  void SetInteractMode(bool bEnabled);

  // ————— localization preview —————

  void PopulateLanguageCombo(); ///< subfolders of <project>/Localization and <project>/Editor/Localization
  void SendLanguageToEngine();  ///< "UiLanguage" folders + "UiHighlightUntranslated", then UpdatePreview
  void SendInteractMouseState(const QPoint& viewPos, bool bButtonDown, float fWheelTicks = 0.0f);
  void SendCanvasViewToEngine();
  void RefreshCanvasView(); ///< sends the view state and repaints the rulers

  // ————— bindings & mock data —————

  void UpdateBindingsList();
  void SendMockBlackboardToEngine();
  void EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e);

  void OnAddBindingRequested();
  void OnRemoveBindingRequested(const plUuid& elementGuid, const QString& sBindingProperty);
  void OnBindingEntryEdited(const plUuid& elementGuid, const QString& sBindingProperty, const QString& sNewEntry);
  void OnBindingRowActivated(const plUuid& elementGuid);

  // ————— mock scenario presets (stored on the asset, tool-managed) —————

  void UpdateScenariosList();
  void OnScenarioSaveRequested(const QString& sName);
  void OnScenarioDeleteRequested(const QString& sName);
  void OnScenarioSelected(const QString& sName);
  const plDocumentObject* FindScenarioObject(const QString& sName) const;

  // ————— animation timeline panel —————

  void UpdateAnimationsList();
  void OnAddClipRequested();
  void OnRemoveClipRequested(const plUuid& clipGuid);
  void OnClipActivated(const plUuid& clipGuid);
  void OnAddTrackRequested(const plUuid& clipGuid);
  void OnRemoveTrackRequested(const plUuid& trackGuid);
  void OnAddKeyRequested(const plUuid& trackGuid, float fTime, bool bSelectKey = true);
  void OnRemoveKeyRequested(const plUuid& keyGuid);
  void OnKeyMoved(const plUuid& keyGuid, float fNewTime);
  void OnKeyEdited(const plUuid& keyGuid, float fTime, float fValue, const plColor& color, int iEasing);
  void OnClipScrubbed(const QString& sClipName, float fTime);
  void OnClipPlay(const QString& sClipName);
  void OnClipStop();

  // ————— states & transitions panel —————

  void UpdateStatesPanel();
  void OnElementStringPropertyChanged(const plUuid& elementGuid, const char* szProperty, const QString& sValue);
  void OnAddStateRequested(const plUuid& elementGuid);
  void OnRemoveStateRequested(const plUuid& stateGuid);
  void OnStateEdited(const plUuid& stateGuid, const QString& sName, const QString& sEntry, int iCondition, double fValue, const QString& sValueText, int iVisibility, const QString& sPlayClip);
  void OnMockTestValue(const QString& sEntry, const QString& sValue);

  void CollectElementList(plDynamicArray<ElementListEntry>& out_elements) const;

  /// Reads the referenced Blackboard Template's compiled entries (design-time binding contract).
  void LoadTemplateEntries();

  // ————— rect transform panel + status bar —————

  QWidget* CreateRectTransformWidget(QWidget* pParent);
  void ApplyAnchorPreset(plInt8 iCol, plInt8 iRow); ///< 0..2 = left/center/right (top/middle/bottom), 3 = stretch
  void OnOffsetEditingFinished();
  void UpdateSelectionUi();

  // ————— effective-style inspector (which cascade level set each value) —————

  QWidget* CreateEffectiveStyleWidget(QWidget* pParent);
  void UpdateEffectiveStyle();
  /// reads the referenced UI Style asset's COMPILED output (LoadTemplateEntries pattern);
  /// returns false when unresolvable / not yet transformed
  bool LoadCompiledStyle(const plString& sStyleRef, class plUiStyle& out_style) const;

  plEngineViewConfig m_ViewConfig;
  plQtEngineViewWidget* m_pViewWidget = nullptr;
  plUiDocumentAssetDocument* m_pAssetDoc = nullptr;
  plQtUiHierarchyTreeWidget* m_pHierarchy = nullptr;
  plQtUiBindingsPanel* m_pBindingsPanel = nullptr;
  plQtUiAnimationsPanel* m_pAnimationsPanel = nullptr;
  plQtUiStatesPanel* m_pStatesPanel = nullptr;

  plDynamicArray<plQtUiBindingsPanel::MockEntry> m_TemplateEntries;
  plString m_sLoadedTemplate; ///< template ref the entries were loaded from
  bool m_bIgnoreHierarchySelection = false;

  QAction* m_pSelectModeAction = nullptr;  ///< canvas tool: direct move/resize (default)
  QAction* m_pAnchorsModeAction = nullptr; ///< canvas tool: drag the anchor diamonds
  QAction* m_pInteractAction = nullptr;
  QAction* m_pSnapAction = nullptr;
  QAction* m_pSafeAreaAction = nullptr;
  QAction* m_pNavGraphAction = nullptr;
  QAction* m_pCheckerboardAction = nullptr;
  QComboBox* m_pDevicePresetCombo = nullptr;
  QComboBox* m_pZoomCombo = nullptr;
  QComboBox* m_pLanguageCombo = nullptr;
  QAction* m_pHighlightUntranslatedAction = nullptr;
  plQtUiRulerWidget* m_pHRuler = nullptr;
  plQtUiRulerWidget* m_pVRuler = nullptr;
  bool m_bInteractMode = false;
  bool m_bLastInteractButtonDown = false;

  // canvas view state (mirrored to the engine preview)
  plVec2U32 m_vDeviceResolution = plVec2U32::MakeZero(); ///< 0 = fill viewport
  float m_fZoom = 0.0f;                                  ///< 0 = fit
  plVec2 m_vPan = plVec2::MakeZero();
  bool m_bPanning = false;
  QPoint m_PanStartPos;
  plVec2 m_vPanStartValue = plVec2::MakeZero();

  QWidget* m_pRectTransformWidget = nullptr;
  QWidget* m_pEffectiveStyleWidget = nullptr;
  QLabel* m_pEffectiveStyleTitle = nullptr;
  QTableWidget* m_pEffectiveStyleTable = nullptr;
  QDoubleSpinBox* m_pOffsetSpin[4] = {};
  QLabel* m_pOffsetLabel[4] = {};
  QLabel* m_pStatusLabel = nullptr;
  QLabel* m_pEngineStatusLabel = nullptr; ///< "engine preview connected/restarting" dot
  QLabel* m_pDrawStatsLabel = nullptr;    ///< "draw: N batches · M quads"
  bool m_bIgnoreRectPanelChanges = false;

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
  QPoint m_DragStartPos;
  plRectFloat m_DragStartRect = plRectFloat(0, 0, 0, 0); ///< dragged element's rect at drag start (canvas space)
  plDynamicArray<plRectFloat> m_SnapRects;               ///< snap candidates collected at drag start
  plString m_sLastSentGuides;                            ///< last "UiGuides" payload (dedupe)

  // anchor drag: which anchor component each axis edits; 0 = untouched, -1 = min, 1 = max,
  // 2 = coincident diamond, decided by the first clear drag direction
  plInt8 m_iAnchorDragX = 0;
  plInt8 m_iAnchorDragY = 0;
  plRectFloat m_DragStartParentRect = plRectFloat(0, 0, 0, 0);
  QLabel* m_pAnchorSummaryLabel = nullptr;
  bool m_bDragContainerChild = false; ///< parent is a stack/grid: offsets.x/y are dead, z/w = preferred size
  plVec4 m_vDragStartOffsets = plVec4::MakeZero();
  plVec2 m_vDragAnchorMin = plVec2::MakeZero();
  plVec2 m_vDragAnchorMax = plVec2::MakeZero();
  plUuid m_DragObjectGuid;
  bool m_bDragTransactionOpen = false;
};
