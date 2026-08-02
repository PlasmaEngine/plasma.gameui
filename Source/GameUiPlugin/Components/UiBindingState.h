#pragma once

#include <Core/Utils/Blackboard.h>
#include <Foundation/Algorithm/HashingUtils.h>
#include <GameUiPlugin/Elements/UiAnimationClip.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GameUiPlugin/GameUiPluginDLL.h>

class plUiStyleResource;

/// \brief Identifies one INSTANCE of a shared document element.
///
/// Sub-documents render the same immutable tree in several places, so the element pointer
/// alone cannot key per-instance runtime state (hover, scroll offsets, toggle state, ...).
/// The salt is derived from the chain of plUiSubDocument elements that instanced the tree
/// (0 for elements of the canvas' own document).
struct plUiElementInstance
{
  PL_DECLARE_POD_TYPE();

  const plUiElement* m_pElement = nullptr;
  plUInt32 m_uiSalt = 0;

  PL_ALWAYS_INLINE bool IsValid() const { return m_pElement != nullptr; }
  PL_ALWAYS_INLINE void Invalidate()
  {
    m_pElement = nullptr;
    m_uiSalt = 0;
  }

  PL_ALWAYS_INLINE bool operator==(const plUiElementInstance& other) const { return m_pElement == other.m_pElement && m_uiSalt == other.m_uiSalt; }
  PL_ALWAYS_INLINE bool operator!=(const plUiElementInstance& other) const { return !(*this == other); }

  /// \brief The salt of the tree instanced by the given sub-document element (chain-hashed).
  PL_ALWAYS_INLINE static plUInt32 CombineSalt(plUInt32 uiParentSalt, const plUiElement* pSubDocElement)
  {
    return plHashingUtils::xxHash32(&pSubDocElement, sizeof(pSubDocElement), uiParentSalt);
  }

  /// \brief The salt of one INDEXED instance (item lists, dynamic spawns) under a parent salt.
  PL_ALWAYS_INLINE static plUInt32 CombineSaltIndex(plUInt32 uiParentSalt, plUInt32 uiIndex)
  {
    return plHashingUtils::xxHash32(&uiIndex, sizeof(uiIndex), uiParentSalt);
  }
};

template <>
struct plHashHelper<plUiElementInstance>
{
  PL_ALWAYS_INLINE static plUInt32 Hash(const plUiElementInstance& value)
  {
    return plHashHelper<const plUiElement*>::Hash(value.m_pElement) ^ value.m_uiSalt;
  }

  PL_ALWAYS_INLINE static bool Equal(const plUiElementInstance& a, const plUiElementInstance& b) { return a == b; }
};

/// \brief Per-canvas-instance runtime state over the shared, immutable widget tree:
/// blackboard bindings, animation clip playback and style-state blends.
///
/// Bindings are discovered once per document (root pointer change) and resolved on the
/// MAIN thread in the component update (plBlackboard has no internal locking). Extraction
/// reads the resolved store immutably — the same phase separation the interaction state
/// uses.
///
/// Change detection uses the two-tier blackboard counter idiom (see RmlUi's
/// BlackboardDataBinding): the per-board entry-change counter gates the whole resolve,
/// per-entry counters gate individual values. All failure modes (missing entry, type
/// mismatch, removed entry) degrade to the authored value and self-heal when the board
/// structure changes.
class PL_GAMEUIPLUGIN_DLL plUiBindingState
{
public:
  struct ResolvedValues
  {
    enum FlagBits : plUInt8
    {
      HasVisible = PL_BIT(0),
      HasFill = PL_BIT(1),
      HasText = PL_BIT(2),
      HasAnimation = PL_BIT(3), ///< clip tracks touch this element
      HasStateBlends = PL_BIT(4),
      HasChecked = PL_BIT(5), ///< toggle checked state resolved from its ClickEntryBinding
    };

    plUInt8 m_uiFlags = 0;
    bool m_bVisible = true;
    bool m_bChecked = false;
    float m_fFill = 0.0f;
    plString m_sText;

    // combined output of all clip tracks on the element (offsets/sizes/rotation sum, factors multiply)
    plVec2 m_vAnimOffset = plVec2::MakeZero();
    plVec2 m_vAnimSize = plVec2::MakeZero(); ///< px added to the rect, centered
    float m_fAnimRotation = 0.0f;            ///< degrees, own visuals only
    float m_fAnimOpacity = 1.0f;
    float m_fAnimScale = 1.0f;
    float m_fAnimFill = 1.0f; ///< multiplier on a progress bar's fill
    plColor m_AnimTint = plColor::White;

    // interaction-state blends (0..1, advanced over the style transition duration);
    // the salts pin a blend to the INSTANCE that is (or was last) interacted with, so
    // hovering one sub-document instance doesn't light up its siblings
    float m_fHoverBlend = 0.0f;
    float m_fPressedBlend = 0.0f;
    float m_fDisabledBlend = 0.0f;
    plUInt32 m_uiHoverSalt = 0;
    plUInt32 m_uiPressedSalt = 0;

    /// state-machine visibility override (plUiElementState::m_Visibility of the active
    /// state): -1 = none, 0 = force hidden, 1 = force visible. Beats the authored Visible
    /// flag AND VisibleBinding; recomputed every frame in UpdateStates.
    plInt8 m_iStateVisibility = -1;
    plInt8 m_iFlowVisibility = -1;          ///< persistent per-instance Show/Hide reaction override
    bool m_bStateInteractionHidden = false; ///< Hide state is active, including its visual delay
    bool m_bDisabled = false;               ///< raw entry value: gates interaction immediately

    // the element's state machine: every state with a blend > 0, for style cross-fading
    // at draw time (the state pointers reference the pinned document tree)
    struct StateBlendInfo
    {
      const plUiElementState* m_pState = nullptr;
      float m_fBlend = 0.0f; ///< eased + pulsed
    };

    plUInt32 m_uiStateBlendCount = 0;
    StateBlendInfo m_StateBlends[4];
  };

  /// \brief Sets the document data; rebuilds discovery when it changes.
  ///
  /// uiDocumentVersion must change whenever the resource content is replaced (use the
  /// resource change counter) — pointer identity alone is NOT sufficient, a reloaded
  /// tree can reuse the previous root allocation address.
  void SetDocument(const plUiElement* pRoot, const plDynamicArray<plUiAnimationClip*>& clips, plUInt32 uiDocumentVersion);

  /// \brief Replaces the set of blackboards to resolve against (in priority order).
  void SetBlackboards(plArrayPtr<const plSharedPtr<plBlackboard>> boards);

  /// \brief Resolves changed bindings, advances state blends and clips. Main thread only.
  /// deltaTime should be wall-clock based so animations also play in editor previews.
  /// hovered/pressed is the component's current interaction state.
  void Update(plTime deltaTime, const plUiElementInstance& hovered, const plUiElementInstance& pressed);

  /// \brief Rebuilds discovery when any referenced sub-document resource has (re)loaded.
  ///
  /// Returns true when a rebuild happened — the CALLER must then drop all element-instance
  /// state it holds (the sub-trees' element pointers are stale). Call before Update.
  bool RefreshSubDocuments();

  /// \brief nullptr if the element has no resolved runtime values. Safe to call from extraction.
  const ResolvedValues* GetResolved(const plUiElement* pElement) const;

  /// \brief Instance-aware lookup: values resolved PER INSTANCE (item-list rows, dynamic
  /// spawns) win over the element-keyed store; falls back to GetResolved(element).
  const ResolvedValues* GetResolved(const plUiElementInstance& instance) const;

  /// \brief True while the element's DisabledBinding entry is true (buttons only).
  bool IsDisabled(const plUiElement* pElement) const;
  bool IsDisabled(const plUiElementInstance& instance) const;

  // ————— animation clips —————

  /// \brief Starts every clip whose PlayOnEvent matches (called for button click events).
  void NotifyEvent(plStringView sIdentifier);
  void NotifyEvent(plStringView sIdentifier, const plUiElement* pDocumentRoot, plUInt32 uiDocumentSalt);

  /// \brief Starts the clip with the given name from position 0.
  void PlayClip(plStringView sName);
  void PlayClip(plStringView sName, const plUiElement* pDocumentRoot, plUInt32 uiDocumentSalt);

  /// \brief Stops event-triggered playback (clips hold their current pose).
  void StopClips();

  /// \brief Editor preview override: the clip is force-evaluated at the given time and its
  /// normal playback logic is suspended. Pass an empty name to clear.
  void SetClipPreview(plStringView sName, float fTime);

  // ————— blackboard access —————

  /// \brief The first registered board that has the given entry, or nullptr.
  plBlackboard* FindBoardForEntry(const plTempHashedString& sEntry) const;

  /// \brief The first registered board, or nullptr. Used as write target for entries that don't exist yet.
  plBlackboard* GetPrimaryBoard() const;

  /// Persistent, instance-local visibility overrides used by parent event reactions.
  void SetFlowVisibility(const plUiElementInstance& instance, bool bVisible);
  void ToggleFlowVisibility(const plUiElementInstance& instance);

  /// \brief Current item count of a plUiItemList (0 while its entry is unresolved).
  plUInt32 GetListItemCount(const plUiItemList* pList) const;

  // ————— dynamically spawned instances (plUiCanvasComponent::CreateUiInstance) —————

  /// \brief Registers a spawned document instance: its tree's bindings resolve against
  /// pInstanceBoard FIRST (falling back to the canvas boards) into per-instance values.
  void RegisterDynamicInstance(plUInt32 uiId, plUInt32 uiSalt, const plUiDocumentResourceHandle& hDocument, const plSharedPtr<plBlackboard>& pInstanceBoard);

  /// \brief Drops the instance's bindings and per-instance resolved values.
  void UnregisterDynamicInstance(plUInt32 uiId);

  bool IsEmpty() const { return m_Bindings.IsEmpty(); }

  void Clear();

private:
  enum class Kind : plUInt8
  {
    Visible,
    Fill,
    Text,
    LocalizedText, ///< resolved via plTranslate, not a blackboard
    Checked,       ///< toggle checked state (bool)
  };

  struct Binding
  {
    const plUiElement* m_pElement = nullptr;
    plUInt32 m_uiSalt = 0;
    Kind m_Kind = Kind::Visible;
    plHashedString m_sEntry;
    float m_fMin = 0.0f;
    float m_fMax = 1.0f;
    plUInt32 m_uiBoardIndex = plInvalidIndex;
    plUInt32 m_uiLastEntryChangeCounter = 0xFFFFFFFF;
  };

  struct BoardState
  {
    plSharedPtr<plBlackboard> m_pBoard;
    plUInt32 m_uiStructureCounter = 0xFFFFFFFF;
    plUInt32 m_uiEntryCounter = 0xFFFFFFFF;
  };

  /// a plUiItemList's items entry: resolved like a Binding, drives the per-item instanced values
  struct ListBinding
  {
    const plUiItemList* m_pList = nullptr;
    plUInt32 m_uiChainSalt = 0; ///< salt of the tree CONTAINING the list element
    plHashedString m_sEntry;
    plUInt32 m_uiBoardIndex = plInvalidIndex;
    plUInt32 m_uiLastEntryChangeCounter = 0xFFFFFFFF;
    plUInt32 m_uiCount = 0;
  };

  /// a binding inside a list's item template: the entry names a KEY of each item's dictionary
  struct ItemBinding
  {
    plUInt32 m_uiList = 0; ///< index into m_Lists
    const plUiElement* m_pElement = nullptr;
    Kind m_Kind = Kind::Visible;
    plString m_sKey;
    float m_fMin = 0.0f;
    float m_fMax = 1.0f;
  };

  /// one dynamically spawned document instance; bindings resolve into the instanced store
  struct DynamicInstance
  {
    plUInt32 m_uiId = 0;
    plUInt32 m_uiSalt = 0;
    plUiDocumentResourceHandle m_hDocument;
    plUInt32 m_uiChangeCounter = 0xFFFFFFFF;
    plSharedPtr<plBlackboard> m_pBoard; ///< optional per-instance data board (looked up first)
    plUInt32 m_uiBoardEntryCounter = 0xFFFFFFFF;
    plUInt32 m_uiBoardStructureCounter = 0xFFFFFFFF;

    struct Entry
    {
      const plUiElement* m_pElement = nullptr;
      Kind m_Kind = Kind::Visible;
      plHashedString m_sEntry;
      float m_fMin = 0.0f;
      float m_fMax = 1.0f;
    };

    plDynamicArray<Entry> m_Bindings;
  };

  struct TrackInstance
  {
    const plUiAnimTrack* m_pTrack = nullptr;
    const plUiElement* m_pElement = nullptr; ///< resolved by name; nullptr if not found
    plHybridArray<plUInt32, 8> m_SortedKeys; ///< key indices sorted by time
  };

  struct ClipInstance
  {
    const plUiAnimationClip* m_pClip = nullptr;
    const plUiElement* m_pDocumentRoot = nullptr;
    plUInt32 m_uiDocumentSalt = 0;
    plTempHashedString m_sEntry; ///< if entry-bound
    plHybridArray<TrackInstance, 4> m_Tracks;

    float m_fTime = 0.0f;      ///< current playback time in seconds
    bool m_bPlaying = false;   ///< event-triggered playback running
    float m_fDirection = 1.0f; ///< ping-pong direction

    bool m_bStateDriven = false;           ///< some state references this clip via PlayClip
    bool m_bStateActive = false;           ///< a driving state is active this frame (set in UpdateStates)
    bool m_bStateBindingAvailable = false; ///< at least one driving state can resolve its blackboard entry
    bool m_bExplicitExit = false;          ///< referenced as ExitClip
    bool m_bExitPlaying = false;
    bool m_bHasExplicitExit = false; ///< enter clip is paired with a separate exit clip
  };

  struct StateInstance
  {
    const plUiElement* m_pElement = nullptr;
    const plUiElement* m_pDocumentRoot = nullptr;
    plUInt32 m_uiSalt = 0;
    const plUiElementState* m_pState = nullptr; ///< nullptr = disabled binding (buttons)
    plTempHashedString m_sEntry;
    plInt32 m_iPlayClipIndex = -1; ///< clip driven by this state
    plInt32 m_iExitClipIndex = -1;
    float m_fBlend = 0.0f;      ///< raw enter blend 0..1
    float m_fPulsePhase = 0.0f; ///< seconds into the pulse cycle
    float m_fHideElapsed = 0.0f;
    bool m_bWasActive = false;
    bool m_bHideDelayed = false;
  };

  void RebuildDiscovery();
  void DiscoverRecursive(const plUiElement* pElement, const plUiElement* pDocumentRoot, plUInt32 uiDepth, plUInt32 uiChainSalt, plInt32 iListContext, bool bSkipShared);
  void AddDocumentClips(const plUiElement* pDocumentRoot, const plDynamicArray<plUiAnimationClip*>& clips, plUInt32 uiDocumentSalt);
  /// routes a discovered element binding into m_Bindings, or m_ItemBindings inside a list template
  void AddDiscoveredBinding(plInt32 iListContext, const plUiElement* pElement, plUInt32 uiSalt, Kind kind, plStringView sEntry, float fMin = 0.0f, float fMax = 1.0f);
  void RemapEntries();
  void ResolveBinding(Binding& ref_binding, bool bForce);
  void ResolveList(plUInt32 uiList, bool bForce);
  void DiscoverDynamicInstance(DynamicInstance& ref_instance); ///< (re)collects its tree's bindings
  void ResolveDynamicInstance(DynamicInstance& ref_instance, bool bForce);
  static void ApplyValueTo(ResolvedValues& ref_resolved, Kind kind, float fMin, float fMax, const plVariant& value);
  void ApplyValue(const Binding& binding, const plVariant& value);
  void UpdateClips(plTime deltaTime);
  void UpdateStates(plTime deltaTime, const plUiElementInstance& hovered, const plUiElementInstance& pressed);
  const plUiElement* FindElementByName(const plUiElement* pElement, plStringView sName) const;
  bool ReadEntryBool(const plTempHashedString& sEntry) const;
  ResolvedValues& GetMutableResolved(const plUiElement* pElement, plUInt32 uiSalt);

  /// a sub-document tree that discovery descended into; its change counter detects reloads
  struct SubDocumentRef
  {
    plUiDocumentResourceHandle m_hDocument;
    plUInt32 m_uiChangeCounter = 0xFFFFFFFF;
  };

  const plUiElement* m_pRoot = nullptr;
  const plDynamicArray<plUiAnimationClip*>* m_pRootClips = nullptr;
  const void* m_pClipsId = nullptr;          ///< identity of the clip array the instances were built from
  plUInt32 m_uiDocumentVersion = 0xFFFFFFFF; ///< resource change counter the instances were built from
  plHybridArray<SubDocumentRef, 4> m_SubDocuments;
  plDynamicArray<ListBinding> m_Lists;
  plDynamicArray<ItemBinding> m_ItemBindings;
  plDynamicArray<DynamicInstance> m_DynamicInstances;
  plDynamicArray<Binding> m_Bindings;
  plDynamicArray<ClipInstance> m_Clips;
  plDynamicArray<StateInstance> m_States;
  plDynamicArray<plUiElementInstance> m_AnimatedElements; ///< element instances touched by any track
  plHashTable<const plUiElement*, ResolvedValues> m_Resolved;
  plHashTable<plUiElementInstance, ResolvedValues> m_ResolvedInstanced; ///< per-instance overrides (lists, spawns)
  plHybridArray<BoardState, 4> m_Boards;

  // interaction blend bookkeeping (buttons with a style class)
  plHashTable<plUiElementInstance, plVec2> m_InteractionBlends; ///< x = hover, y = pressed

  plInt32 m_iPreviewClip = -1;
  float m_fPreviewTime = 0.0f;
};
