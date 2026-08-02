#pragma once

#include <GameUiPlugin/Components/UiBindingState.h>
#include <GameUiPlugin/Components/UiMessages.h>
#include <GameUiPlugin/GameUiPluginDLL.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>

#include <Core/Messages/EventMessage.h>
#include <Core/World/ComponentManager.h>
#include <RendererCore/Components/RenderComponent.h>

struct plMsgExtractRenderData;
class plUiElement;
class plUiButton;

/// \brief How a plUiCanvasComponent maps its reference coordinates to the actual viewport.
struct PL_GAMEUIPLUGIN_DLL plUiCanvasScaleMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    ConstantPixel,   ///< 1 UI pixel = 1 screen pixel, no scaling.
    ScaleWithHeight, ///< UI is authored against ReferenceHeight and scaled by viewportHeight / referenceHeight.

    Default = ScaleWithHeight
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiCanvasScaleMode);

/// \brief Actions of the "UiNav" plInputManager action set (see GameUiPlugin Startup.cpp).
struct plUiNavAction
{
  enum Enum : plUInt8
  {
    Up,
    Down,
    Left,
    Right,
    Accept,
    Cancel,

    Count
  };
};

/// \brief One focusable element instance with its drawn rect (canvas reference space).
struct plUiFocusable
{
  const plUiElement* m_pElement = nullptr;
  plUInt32 m_uiSalt = 0;
  plRectFloat m_Rect = plRectFloat(0, 0, 0, 0);
  const plUiElement* m_pDocumentRoot = nullptr;

  struct RouteEntry
  {
    const plUiSubDocument* m_pPlacement = nullptr;
    const plUiElement* m_pParentDocumentRoot = nullptr;
    plUInt32 m_uiParentSalt = 0;
  };

  plHybridArray<RouteEntry, 8> m_Route;

  // when the focusable sits inside a plUiItemList row: the list + row index (else -1)
  const plUiItemList* m_pItemList = nullptr;
  plInt32 m_iItemIndex = -1;

  plUInt32 m_uiDynamicInstanceId = 0; ///< spawned-instance id containing the focusable (0 = none)
};

/// \brief One dynamically spawned UI document instance (plUiCanvasComponent::CreateUiInstance).
struct plUiDynamicInstance
{
  plUInt32 m_uiId = 0;
  plUInt32 m_uiSalt = 0;  ///< stable instance salt (hash of the id — survives removals)
  plString m_sAnchorName; ///< name of the canvas-document element the instance is parented to
  plUiDocumentResourceHandle m_hDocument;
};

/// \brief Editing keys forwarded to a focused plUiTextInput (arrows/Enter/Escape travel
/// as plUiNavAction; typed characters as codepoints).
struct plUiEditKey
{
  enum Enum : plUInt8
  {
    Backspace,
    Delete,
    Home,
    End,

    Count
  };
};

/// \brief Live editing state of one focused plUiTextInput instance.
struct plUiTextEditState
{
  plString m_sText;
  plInt32 m_iCaret = 0;  ///< caret position in CHARACTERS (not bytes)
  float m_fBlink = 0.0f; ///< seconds since the last edit (caret blink phase)
};

/// \brief Editor-preview canvas view settings (device-resolution simulation, zoom, pan, overlays).
struct plUiCanvasPreviewView
{
  plVec2U32 m_vResolution = plVec2U32::MakeZero(); ///< simulated device resolution; 0 = fill the viewport
  float m_fZoom = 0.0f;                            ///< canvas-pixel to screen-pixel factor; 0 = fit (only used with a resolution)
  plVec2 m_vPan = plVec2::MakeZero();              ///< view pan in screen pixels (only used with a resolution)
  bool m_bCheckerboard = false;                    ///< draw a checkerboard behind the UI
};

using plUiCanvasComponentManager = plComponentManagerSimple<class plUiCanvasComponent, plComponentUpdateType::Always, plBlockStorageType::Compact>;

/// \brief Renders a UI Document as a screen-space overlay.
///
/// The widget tree is laid out against the viewport rect every frame at extraction time and
/// submitted as batched quads to the GUI render phase (plUiRenderer).
///
/// While the world is simulated, pointer input is routed to interactive elements (plUiButton):
/// hover/pressed visuals apply and clicks raise a plMsgUiEvent on the owner object.
class PL_GAMEUIPLUGIN_DLL plUiCanvasComponent : public plRenderComponent
{
  PL_DECLARE_COMPONENT_TYPE(plUiCanvasComponent, plRenderComponent, plUiCanvasComponentManager);

public:
  plUiCanvasComponent();
  ~plUiCanvasComponent();

  void Update();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  virtual plResult GetLocalBounds(plBoundingBoxSphere& ref_bounds, bool& ref_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg) override;

  PL_ADD_RESOURCEHANDLE_ACCESSORS_WITH_SETTER(UiDocument, m_hDocument, SetUiDocument);
  void SetUiDocument(const plUiDocumentResourceHandle& hDocument);                // [ property ]
  const plUiDocumentResourceHandle& GetUiDocument() const { return m_hDocument; } // [ property ]

  /// \brief Optional per-canvas UI Style; when valid it replaces the document's own style
  /// (re-skinning), otherwise the style compiled into the document is used.

  void SetScaleMode(plEnum<plUiCanvasScaleMode> mode) { m_ScaleMode = mode; } // [ property ]
  plEnum<plUiCanvasScaleMode> GetScaleMode() const { return m_ScaleMode; }    // [ property ]

  void SetReferenceHeight(plUInt32 uiHeight) { m_uiReferenceHeight = uiHeight; } // [ property ]
  plUInt32 GetReferenceHeight() const { return m_uiReferenceHeight; }            // [ property ]

  void SetSortOrder(plInt16 iOrder) { m_iSortOrder = iOrder; } // [ property ]
  plInt16 GetSortOrder() const { return m_iSortOrder; }        // [ property ]

  /// \brief Editor 'Interact' preview mode: when enabled, pointer input comes from InjectMouseState
  /// instead of plInputManager and works without world simulation.
  void SetInputInjectionEnabled(bool bEnabled);
  void InjectMouseState(const plVec2& vNormalizedPos, bool bButtonDown, float fWheelTicks = 0.0f);

  /// \brief Editor interact mode: queues a navigation action (keyboard stands in for the gamepad).
  void InjectNavAction(plUiNavAction::Enum action);

  /// \brief Editor interact mode: queues a typed character for the focused text input.
  void InjectCharacter(plUInt32 uiCodepoint);

  /// \brief Editor interact mode: queues an editing key for the focused text input.
  void InjectEditKey(plUiEditKey::Enum key);

  // ————— focus & directional navigation —————

  /// \brief True while an element has UI focus — the cooperative "UI wants the input" contract
  /// (mirrors plRmlUi::AnyContextWantsInput): gameplay should skip its own input handling.
  bool WantsInput() const { return m_FocusedElement.IsValid(); }

  /// \brief Raised as plMsgUiEvent (and animation-clip event) when the UiNav Cancel action fires.
  void SetCancelEvent(const char* szEvent) { m_sCancelEvent = szEvent; } // [ property ]
  const char* GetCancelEvent() const { return m_sCancelEvent; }          // [ property ]

  /// \brief While enabled (default), the canvas writes the interacted element NAMES into the
  /// primary blackboard whenever they change: "Ui_Hovered" (pointer), "Ui_Focused" (gamepad/
  /// keyboard focus) and "Ui_Active" (focused if any, else hovered; empty = none).
  ///
  /// This makes UI FLOW data-driven without game code: give an element a state like
  /// "Ui_Active == 'Start' -> Show" (see plUiElementState) to reveal a tooltip while the
  /// 'Start' button is hovered/focused, or drive description texts via bindings.
  void SetPublishInteractionState(bool b) { m_bPublishInteractionState = b; }    // [ property ]
  bool GetPublishInteractionState() const { return m_bPublishInteractionState; } // [ property ]

  /// \brief All focusable element instances with their drawn rects (canvas reference space).
  /// Used by the editor's navigation-graph overlay; viewport size in screen pixels.
  void CollectFocusables(const plVec2& vViewportSize, plDynamicArray<plUiFocusable>& out_focusables) const;

  /// \brief Resolves where focus moves from 'from' in the given direction: explicit NavUp/...
  /// name overrides win, else geometric best-candidate (direction cone + distance).
  /// Returns the index into 'focusables' or -1; out_bExplicit tells overrides apart (overlay coloring).
  static plInt32 FindNavTarget(const plUiFocusable& from, plUiNavAction::Enum direction, plArrayPtr<const plUiFocusable> focusables, bool* out_pExplicit = nullptr);

  void SetPreviewView(const plUiCanvasPreviewView& view) { m_PreviewView = view; }
  const plUiCanvasPreviewView& GetPreviewView() const { return m_PreviewView; }

  /// \brief Registers a blackboard for data bindings. Explicitly registered boards take
  /// priority over autobound ones; boards are searched in registration order.
  void RegisterBlackboard(const plSharedPtr<plBlackboard>& pBlackboard);
  void UnregisterBlackboard(const plBlackboard* pBlackboard);

  /// \brief Search the owner object and its parents for plBlackboardComponents and bind their boards.
  void SetAutobindBlackboards(bool bAutobind);                           // [ property ]
  bool GetAutobindBlackboards() const { return m_bAutobindBlackboards; } // [ property ]

  // ————— dynamic runtime UI (main thread; runtime-only, never serialized) —————

  /// \brief Spawns an instance of the given UI document under the NAMED element of the
  /// canvas document (a Panel/Stack anchor). Inside stack anchors instances flow after the
  /// authored children; elsewhere the instance root lays out against the anchor rect.
  ///
  /// pInstanceBoard (optional) provides per-instance data: the instance's bindings resolve
  /// against it first, falling back to the canvas' boards. Clicks inside the instance stamp
  /// plMsgUiEvent::m_uiUiInstanceId with the returned id. Returns 0 on failure.
  /// NOTE: scroll extents of stack anchors do not include spawned instances — prefer a
  /// plain Panel anchor or a plUiItemList for scrolling lists.
  plUInt32 CreateUiInstance(plStringView sAnchorElementName, const plUiDocumentResourceHandle& hDocument, const plSharedPtr<plBlackboard>& pInstanceBoard = nullptr);

  /// \brief Removes one spawned instance.
  void DestroyUiInstance(plUInt32 uiId);

  /// \brief Removes all spawned instances (of one anchor, or all when empty).
  void ClearUiInstances(plStringView sAnchorElementName = {});

  /// \brief The canvas-space to screen-space mapping for a given viewport, respecting the preview view settings.
  void ComputeCanvasTransform(const plVec2& vViewportSize, float& out_fScale, plVec2& out_vOffset, plVec2& out_vCanvasSize) const;

  /// \brief Draw-list statistics of the most recent extraction (editor status display).
  plUInt32 GetLastExtractedBatchCount() const { return m_uiLastExtractedBatches; }
  plUInt32 GetLastExtractedQuadCount() const { return m_uiLastExtractedQuads; }

  // ————— animation clips —————

  /// \brief Starts the named clip of the document from position 0.
  void PlayClip(plStringView sName) { m_BindingState.PlayClip(sName); }

  /// \brief Stops event-triggered clip playback (clips hold their pose).
  void StopClips() { m_BindingState.StopClips(); }

  /// \brief Editor timeline scrubbing: force-evaluates the named clip at the given time.
  /// Pass an empty name to return to normal playback.
  void SetClipPreview(plStringView sName, float fTime) { m_BindingState.SetClipPreview(sName, fTime); }

protected:
  virtual void OnActivated() override;

  void OnMsgExtractRenderData(plMsgExtractRenderData& msg) const; // [ msg handler ]

  void ResetInteractionState();
  void UpdateBindingBoards();

  /// \brief The style to render with: the override when set, else the document's compiled style.

  plUiDocumentResourceHandle m_hDocument;
  plEnum<plUiCanvasScaleMode> m_ScaleMode;
  plUInt32 m_uiReferenceHeight = 1080;
  plInt16 m_iSortOrder = 0;

  // pointer interaction state; the pointers reference the pinned document resource's tree
  // (or an instanced sub-document's tree) and are reset whenever any of those resources
  // (re)load (root pointer + change counter + sub-document refresh, see Update)
  void HandleNavAction(plUiNavAction::Enum action, plArrayPtr<const plUiFocusable> focusables);
  void TriggerClick(const plUiButton* pButton, const plUiElementInstance& instance, const plUiElement* pDocumentRoot, plArrayPtr<const plUiFocusable::RouteEntry> route, plInt32 iItemIndex, const plUiItemList* pItemList, plUInt32 uiUiInstanceId);
  bool ExecuteEventReactions(plStringView sEvent, plArrayPtr<const plUiFocusable::RouteEntry> route);
  /// writes the clicked row index to the list's SelectedBinding entry (when set)
  void WriteListSelection(const plUiItemList* pItemList, plInt32 iItemIndex);
  void AdjustSliderValue(const plUiSlider* pSlider, const plUiElementInstance& instance, float fDelta01);

  // ————— text input editing (all on the focused instance) —————

  plString GetTextInputDisplayText(const plUiTextInput* pInput, const plUiElementInstance& instance) const;
  plUiTextEditState& BeginTextEdit(const plUiTextInput* pInput, const plUiElementInstance& instance);
  void InsertTextInputCharacter(const plUiTextInput* pInput, plUInt32 uiCodepoint);
  void ApplyTextInputEditKey(const plUiTextInput* pInput, plUiEditKey::Enum key);
  void MoveTextInputCaret(const plUiTextInput* pInput, plInt32 iDelta);
  void WriteTextInputBinding(const plUiTextInput* pInput, const plString& sText);
  void CommitTextInput(const plUiTextInput* pInput); ///< Enter: write + SubmitEvent
  void RevertTextInput(const plUiTextInput* pInput); ///< Escape: drop the edit + unfocus
  void UpdateTextInputEditing(float fDeltaTime);     ///< polls characters/keys for the focused input

  /// hover for visuals/blends: pointer hover wins, else the focused element lights up
  plUiElementInstance GetEffectiveHover() const { return m_HoveredElement.IsValid() ? m_HoveredElement : m_FocusedElement; }

  const plUiElement* m_pCachedRoot = nullptr;
  plUInt32 m_uiCachedDocumentVersion = 0xFFFFFFFF;
  plUiElementInstance m_HoveredElement; ///< topmost interactive element instance under the cursor
  plUiElementInstance m_PressedElement;
  plUiElementInstance m_FocusedElement; ///< gamepad/keyboard navigation focus
  const plUiElement* m_pPressedDocumentRoot = nullptr;
  const plUiElement* m_pFocusedDocumentRoot = nullptr;
  plHybridArray<plUiFocusable::RouteEntry, 8> m_PressedRoute;
  plHybridArray<plUiFocusable::RouteEntry, 8> m_FocusedRoute;

  // list-row context of the pressed/focused element (plMsgUiEvent::m_iItemIndex + SelectedBinding)
  const plUiItemList* m_pPressedItemList = nullptr;
  plInt32 m_iPressedItemIndex = -1;
  const plUiItemList* m_pFocusedItemList = nullptr;
  plInt32 m_iFocusedItemIndex = -1;

  // dynamically spawned instances (runtime-only, never serialized)
  plDynamicArray<plUiDynamicInstance> m_DynamicInstances;
  plUInt32 m_uiNextUiInstanceId = 1;
  plUInt32 m_uiPressedUiInstanceId = 0; ///< instance the press happened in (plMsgUiEvent stamp)
  plUInt32 m_uiFocusedUiInstanceId = 0;
  plHybridArray<plUInt8, 6> m_QueuedNavActions;  ///< injected nav actions (editor interact mode)
  plHybridArray<plUInt32, 8> m_QueuedCharacters; ///< injected typed characters (editor interact mode)
  plHybridArray<plUInt8, 6> m_QueuedEditKeys;    ///< injected editing keys (editor interact mode)
  plString m_sCancelEvent;
  plRectFloat m_PressedRect = plRectFloat(0, 0, 0, 0); ///< drawn rect captured at press time (drags survive leaving the widget)

  // interaction-state publishing (see SetPublishInteractionState)
  bool m_bPublishInteractionState = true;
  plString m_sLastPublishedHover;
  plString m_sLastPublishedFocus;
  plString m_sLastPublishedActive;
  void PublishInteractionState();

  // per-instance widget runtime state, written on the main thread in Update and read
  // immutably by extraction (same phase separation as the binding state); reset with the
  // interaction state on resource reloads. Keyed by element INSTANCE so every placement
  // of a sub-document widget keeps its own state.
  plHashTable<plUiElementInstance, float> m_ScrollOffsets;              ///< plUiScrollView main-axis offsets in px
  plHashTable<plUiElementInstance, bool> m_LocalToggleState;            ///< unbound plUiToggle checked state
  plHashTable<plUiElementInstance, float> m_LocalSliderValues;          ///< unbound plUiSlider values 0..1
  plHashTable<plUiElementInstance, plUiTextEditState> m_TextEditStates; ///< live plUiTextInput edits

  // editor interact-mode input injection
  bool m_bInputInjectionEnabled = false;
  bool m_bInjectedButtonDown = false;
  bool m_bPrevInjectedButtonDown = false;
  plVec2 m_vInjectedMousePos = plVec2::MakeZero(); ///< normalized 0..1
  float m_fInjectedWheelTicks = 0.0f;              ///< accumulated until the next Update

  plUiCanvasPreviewView m_PreviewView;

  // last extraction's draw-list stats (extraction is const; display-only bookkeeping)
  mutable plUInt32 m_uiLastExtractedBatches = 0;
  mutable plUInt32 m_uiLastExtractedQuads = 0;

  // data binding
  plUiBindingState m_BindingState;
  plSharedPtr<plBlackboard> m_pLocalBlackboard;
  const plUiElement* m_pLocalStateRoot = nullptr;
  plUInt32 m_uiLocalStateDocumentVersion = 0xFFFFFFFF;
  plTime m_LastUpdateTime; ///< wall clock, so animations also play in editor previews
  plHybridArray<plSharedPtr<plBlackboard>, 2> m_RegisteredBlackboards;
  bool m_bAutobindBlackboards = true;
  bool m_bBindingBoardsDirty = true;

  plEventMessageSender<plMsgUiEvent> m_UiEventSender; // [ event ]
};
