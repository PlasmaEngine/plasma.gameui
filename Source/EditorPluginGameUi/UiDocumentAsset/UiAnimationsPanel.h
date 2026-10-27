#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HashTable.h>
#include <Foundation/Math/Color.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/ArrayPtr.h>
#include <Foundation/Types/Delegate.h>
#include <Foundation/Types/Uuid.h>
#include <GuiFoundation/EngineUi/EngineCurveEditor.h>
#include <GuiFoundation/Widgets/CurveEditData.h>

class plEditorUiControlStrip;
class plEditorUiTrackView;
class plUiClipList;
struct plEngineTrackViewEvent;

/// \brief Bottom panel of the UI Document editor: the "lite animation clips" timeline - named keyframe clips with
/// property tracks (element . target) over the document, triggered by events or blackboard entries.
///
/// Left: the clip list. Right: the clip's header and transport, the editor's shared track view (multi-select, box
/// select, zoom and pan, the shared ruler), the shared curve view for the selected row, and the key editor. All of it
/// is engine-drawn, as child contents the panel lays out. Clip properties (name, duration, triggers, loop mode) are
/// edited in the property grid - double-click a clip to select it there.
///
/// The panel never writes to the document: edits go out through the delegates, which the window applies through
/// accessor transactions. Edits of several keys at once, and curve drags, arrive between EditsBegin and EditsEnd, so
/// they are one undo step.
class plUiAnimationsPanel : public plEditorUiContent, private plEditorUiCurveEdits
{
public:
  struct KeyInfo
  {
    plUuid m_Guid;
    float m_fTime = 0.0f;
    float m_fValue = 0.0f;
    plColor m_Color = plColor::White;
    plInt32 m_iEasing = 0;
  };

  struct TrackInfo
  {
    plUuid m_Guid;
    plString m_sElement;
    plInt32 m_iTarget = 0; ///< plUiAnimTarget value
    bool m_bElementFound = true;
    plDynamicArray<KeyInfo> m_Keys; ///< sorted by time
  };

  struct ClipInfo
  {
    plUuid m_Guid;
    plString m_sName;
    float m_fDuration = 0.8f;
    plString m_sPlayOnEvent;
    plString m_sEntry;
    bool m_bValueMode = false;
    plDynamicArray<TrackInfo> m_Tracks;
  };

  plUiAnimationsPanel();
  ~plUiAnimationsPanel();

  /// Rebuilds the panel; keeps the clip/key selection when the guids still exist.
  void SetClips(plArrayPtr<const ClipInfo> clips);

  ClipInfo* GetSelectedClip();
  const ClipInfo* GetSelectedClip() const;
  float GetPlayhead() const { return m_fPlayhead; }
  void SetPlayhead(float fTime);

  /// Snaps a time to the grid selected in the Snap dropdown (frame / 10 ms / off).
  float SnapTime(float fTime) const;
  bool IsKeySelected(const plUuid& keyGuid) const;
  bool IsTrackSelected(const plUuid& trackGuid) const { return m_SelectedTrack == trackGuid; }

  /// Width of the clip list beside the timeline, in logical units.
  static constexpr int ClipListMinWidth = 180;
  static constexpr int ClipListMaxWidth = 260;

  /// The parts, for tests.
  plEditorUiContent* GetClipList() const;
  plEditorUiTrackView* GetTimeline() const { return m_pTimeline; }
  plEditorUiControlStrip* GetHeader() const { return m_pHeader; }
  plEditorUiControlStrip* GetKeyEditor() const { return m_pBottom; }
  plEditorUiCurveView* GetCurveView() const { return m_pCurveView; }

  /// \name What the panel asks of the document window, which sets the delegates.
  ///@{
  plDelegate<void()> m_OnAddClipRequested;
  plDelegate<void(const plUuid&)> m_OnRemoveClipRequested;
  plDelegate<void(const plUuid&)> m_OnClipActivated; ///< select the clip object in the property grid
  plDelegate<void(const plUuid&, plStringView)> m_OnClipRenamed;
  plDelegate<void(const plUuid&, float)> m_OnClipDurationChanged;

  plDelegate<void(const plUuid&)> m_OnAddTrackRequested; ///< clip
  plDelegate<void(const plUuid&)> m_OnRemoveTrackRequested;

  plDelegate<void(const plUuid&, float)> m_OnAddKeyRequested; ///< track, time
  plDelegate<void(const plUuid&)> m_OnRemoveKeyRequested;
  plDelegate<void(const plUuid&, float)> m_OnKeyMoved; ///< key, new time
  plDelegate<void(const KeyInfo&)> m_OnKeyEdited;      ///< the key's new values

  /// The edits until EditsEnd are one undo step; bCommit false undoes them.
  plDelegate<void(plStringView)> m_OnEditsBegin;
  plDelegate<void(bool)> m_OnEditsEnd;

  plDelegate<void(plStringView, float)> m_OnScrubbed; ///< engine preview: evaluate the clip at the time
  plDelegate<void(plStringView)> m_OnPlayRequested;
  plDelegate<void()> m_OnStopRequested; ///< also clears the scrub preview
  ///@}

protected:
  void OnUiUpdate() override;

private:
  friend class plUiClipList;

  // what the delegates are told; nothing for a delegate that is not set
  void AddClipRequested();
  void RemoveClipRequested(const plUuid& clipGuid);
  void ClipActivated(const plUuid& clipGuid);
  void ClipRenamed(const plUuid& clipGuid, plStringView sNewName);
  void ClipDurationChanged(const plUuid& clipGuid, float fNewDuration);
  void AddTrackRequested(const plUuid& clipGuid);
  void RemoveTrackRequested(const plUuid& trackGuid);
  void AddKeyRequested(const plUuid& trackGuid, float fTime);
  void RemoveKeyRequested(const plUuid& keyGuid);
  void KeyMoved(const plUuid& keyGuid, float fNewTime);
  void KeyEdited(const plUuid& keyGuid, float fTime, float fValue, const plColor& color, plInt32 iEasing);
  void EditsBegin(plStringView sName);
  void EditsEnd(bool bCommit);
  void Scrubbed(plStringView sClipName, float fTime);
  void PlayRequested(plStringView sClipName);
  void StopRequested();

  /// The parts' places: the clip list on the left; the clip's header, the track view, the curve view and the key
  /// editor stacked on the right.
  void PlaceParts();

  // the curve view's edits (plEditorUiCurveEdits)
  void CpMoved(plUInt32 uiCurve, plUInt32 uiPoint, plInt64 iTick, double fValue) override;
  void CpDeleted(plUInt32 uiCurve, plUInt32 uiPoint) override;
  void TangentMoved(plUInt32 uiCurve, plUInt32 uiPoint, float fX, float fY, bool bRight) override {}
  void InsertCp(plUInt32 uiCurve, plInt64 iTick, double fValue) override;
  void TangentLink(plUInt32 uiCurve, plUInt32 uiPoint, bool bLink) override {}
  void CpTangentMode(plUInt32 uiCurve, plUInt32 uiPoint, bool bRight, int iMode) override {}
  void BeginCpChanges(plStringView sName) override;
  void EndCpChanges() override;
  void BeginOperation(plStringView sName) override;
  void EndOperation(bool bCommit) override;

  struct KeyRef
  {
    plUuid m_KeyGuid;
    plUuid m_TrackGuid;
  };

  void CreateHeader();
  void CreateKeyEditor();
  void OnHeader(plUInt32 uiControl, plInt32 iValue);
  void OnKeyEditor(plUInt32 uiControl, plInt32 iValue);
  void OnTimelineEvent(const plEngineTrackViewEvent& e);
  void OnTimelineMenu(const plEngineTrackViewEvent& e);

  void OnClipSelected(plInt32 iClip);
  void SelectRow(plInt32 iRow);
  void SyncKeySelection();

  void RefreshHeader();
  void RefreshTimeline();
  void RefreshCurves();
  void RefreshKeyEditor();
  void UpdateTimecode();
  void JumpToTime(float fTime);
  void StepToNeighborKey(bool bNext);

  const KeyInfo* FindKey(const plUuid& keyGuid, const TrackInfo** out_pTrack = nullptr) const;
  void GetSelectedKeys(plDynamicArray<KeyRef>& out_keys) const;
  void EmitKeyEdited();
  void PickKeyColor();
  void RemoveSelectedTrack();
  void SetSelectedEasing(plInt32 iEasing);

  // the parts are child contents
  plUiClipList* m_pClipList = nullptr;
  plEditorUiControlStrip* m_pHeader = nullptr;
  plEditorUiTrackView* m_pTimeline = nullptr;
  plEditorUiCurveView* m_pCurveView = nullptr;
  plEditorUiControlStrip* m_pBottom = nullptr;
  plVec2 m_vPlacedSize = plVec2::MakeZero();

  // header controls
  plUInt32 m_uiName = 0;
  plUInt32 m_uiDuration = 0;
  plUInt32 m_uiTrigger = 0;
  plUInt32 m_uiToStart = 0;
  plUInt32 m_uiPrevKey = 0;
  plUInt32 m_uiPlay = 0;
  plUInt32 m_uiNextKey = 0;
  plUInt32 m_uiToEnd = 0;
  plUInt32 m_uiTime = 0;
  plUInt32 m_uiSnap = 0;
  plUInt32 m_uiStop = 0;

  // key editor controls
  plUInt32 m_uiAddTrack = 0;
  plUInt32 m_uiRemoveTrack = 0;
  plUInt32 m_uiKeyTime = 0;
  plUInt32 m_uiKeyValue = 0;
  plUInt32 m_uiKeyColor = 0;
  plUInt32 m_uiKeyEasing = 0;
  plUInt32 m_uiRemoveKey = 0;

  plDynamicArray<ClipInfo> m_Clips;
  plUuid m_SelectedClip;
  plUuid m_SelectedTrack;
  plUuid m_SelectedKey;
  float m_fPlayhead = 0.0f;

  plHashTable<plUInt64, KeyRef> m_Keys;       ///< timeline key id -> key
  plDynamicArray<plHybridArray<plInt32, 2>> m_Rows; ///< timeline lane -> track indices (a vector pair has two)

  plCurveGroupData m_Curves;                       ///< what the curve view shows: the selected row's number tracks
  plHybridArray<plUuid, 2> m_CurveTracks;          ///< curve -> track
  plHybridArray<plDynamicArray<plUuid>, 2> m_CurveKeys; ///< curve, point -> key
  bool m_bCurveOperation = false;                  ///< a curve drag is running: the curve view keeps its own preview
};
