#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Color.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/ArrayPtr.h>
#include <Foundation/Types/Uuid.h>

// declares Q_DECLARE_METATYPE(plUuid), which must precede the moc-generated code
// for signals that carry plUuid parameters
#include <GuiFoundation/GuiFoundationDLL.h>

#include <QWidget>

#include <functional>

class QListWidget;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class plQtUiAnimationsPanel;

/// \brief The keyframe timeline of the Animation panel: a time ruler with a draggable
/// playhead and one lane per track with selectable/draggable keyframe diamonds.
///
/// Pure view over the panel's clip copy — all persistent mutations go through the
/// std::function callbacks (wired to panel signals -> document transactions). Dragging a
/// key edits only the local copy for live feedback and commits once on release.
class plQtUiTimelineWidget : public QWidget
{
public:
  explicit plQtUiTimelineWidget(plQtUiAnimationsPanel* pPanel);

  std::function<void(plUuid trackGuid, plUuid keyGuid)> m_OnSelectKey; ///< invalid guids = cleared
  std::function<void(plUuid keyGuid, float fNewTime)> m_OnKeyMoved;
  std::function<void(plUuid trackGuid, float fTime)> m_OnAddKey;
  std::function<void(plUuid keyGuid)> m_OnDeleteKey;
  std::function<void(float fTime)> m_OnScrub;

  virtual QSize minimumSizeHint() const override;

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;
  virtual void mousePressEvent(QMouseEvent* pEvent) override;
  virtual void mouseMoveEvent(QMouseEvent* pEvent) override;
  virtual void mouseReleaseEvent(QMouseEvent* pEvent) override;
  virtual void mouseDoubleClickEvent(QMouseEvent* pEvent) override;
  virtual void keyPressEvent(QKeyEvent* pEvent) override;

private:
  float TimeToX(float fTime) const;
  float XToTime(float fX) const;
  bool HitTestKey(const QPoint& pos, plUInt32& out_uiTrack, plUInt32& out_uiKey) const;
  int TrackAtY(int iY) const;

  plQtUiAnimationsPanel* m_pPanel = nullptr;
  bool m_bDraggingKey = false;
  bool m_bScrubbing = false;
  bool m_bKeyWasDragged = false;
};

/// \brief The Curve Editor pane under the timeline (sequencer-mockup bottom pane): a large
/// graph of the SELECTED row — float rows show eased value curves with keys draggable in
/// time AND value (vector rows draw both component lines on one shared scale), Tint rows
/// show the gradient strip with time-draggable keys. Pure view over the panel's clip copy;
/// mutations go through the std::function callbacks.
class plQtUiCurveEditorWidget : public QWidget
{
public:
  explicit plQtUiCurveEditorWidget(plQtUiAnimationsPanel* pPanel);

  std::function<void(plUuid trackGuid, plUuid keyGuid)> m_OnSelectKey;
  std::function<void(plUuid keyGuid)> m_OnKeyChanged; ///< commit a time/value drag (read from the local copy)
  std::function<void(plUuid trackGuid, float fTime)> m_OnAddKey;
  std::function<void(plUuid keyGuid)> m_OnDeleteKey;

protected:
  virtual void paintEvent(QPaintEvent* pEvent) override;
  virtual void mousePressEvent(QMouseEvent* pEvent) override;
  virtual void mouseMoveEvent(QMouseEvent* pEvent) override;
  virtual void mouseReleaseEvent(QMouseEvent* pEvent) override;
  virtual void mouseDoubleClickEvent(QMouseEvent* pEvent) override;
  virtual void keyPressEvent(QKeyEvent* pEvent) override;

private:
  /// the row shown: the one containing the selected track, else the clip's first row
  bool GetEditedTracks(plInt32& out_iTrack0, plInt32& out_iTrack1) const;
  void ComputeValueRange(float& out_fMin, float& out_fMax) const;
  float TimeToX(float fTime) const;
  float XToTime(float fX) const;
  float ValueToY(float fValue) const;
  float YToValue(float fY) const;
  bool HitTestKey(const QPoint& pos, plInt32& out_iTrack, plUInt32& out_uiKey) const;

  plQtUiAnimationsPanel* m_pPanel = nullptr;
  bool m_bDraggingKey = false;
  bool m_bKeyWasDragged = false;
  float m_fRangeMin = 0.0f; ///< value range of the last paint (drag mapping)
  float m_fRangeMax = 1.0f;
};

/// \brief Bottom dock of the UI Document editor: the "lite animation clips" timeline from
/// the mockup — named keyframe clips with property tracks (element . target) over the
/// document, triggered by events or blackboard entries.
///
/// Left: the clip list. Right: the timeline for the selected clip with per-key editing,
/// plus scrub/play preview controls that drive the engine preview. Clip properties
/// (name, duration, triggers, loop mode) are edited in the property grid — double-click
/// a clip (or use the Edit button) to select it there. The panel never writes to the
/// document itself — all mutations are emitted as signals and applied by the document
/// window through accessor transactions.
class plQtUiAnimationsPanel : public QWidget
{
  Q_OBJECT

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

  explicit plQtUiAnimationsPanel(QWidget* pParent);

  /// Rebuilds the panel; keeps the clip/key selection when the guids still exist.
  void SetClips(plArrayPtr<const ClipInfo> clips);

  // ————— timeline widget access —————
  ClipInfo* GetSelectedClip();
  const ClipInfo* GetSelectedClip() const;
  float GetPlayhead() const { return m_fPlayhead; }
  void SetPlayhead(float fTime);

  /// Snaps a time to the grid selected in the Snap combo (frame / 10 ms / off).
  float SnapTime(float fTime) const;
  bool IsKeySelected(const plUuid& keyGuid) const { return m_SelectedKey == keyGuid; }
  bool IsTrackSelected(const plUuid& trackGuid) const { return m_SelectedTrack == trackGuid; }

Q_SIGNALS:
  void AddClipRequested();
  void RemoveClipRequested(plUuid clipGuid);
  void ClipActivated(plUuid clipGuid); ///< select the clip object in the property grid
  void ClipRenamed(plUuid clipGuid, QString sNewName);
  void ClipDurationChanged(plUuid clipGuid, float fNewDuration);

  void AddTrackRequested(plUuid clipGuid);
  void RemoveTrackRequested(plUuid trackGuid);

  void AddKeyRequested(plUuid trackGuid, float fTime);
  void RemoveKeyRequested(plUuid keyGuid);
  void KeyMoved(plUuid keyGuid, float fNewTime);
  void KeyEdited(plUuid keyGuid, float fTime, float fValue, plColor color, int iEasing);

  void Scrubbed(QString sClipName, float fTime); ///< engine preview: evaluate the clip at fTime
  void PlayRequested(QString sClipName);
  void StopRequested(); ///< also clears the scrub preview

private:
  friend class plQtUiTimelineWidget;
  friend class plQtUiCurveEditorWidget;

  void OnClipSelectionChanged();
  void OnSelectKey(plUuid trackGuid, plUuid keyGuid);
  void RefreshClipList();
  void RefreshKeyEditor();
  void RefreshHeader();
  void UpdateTimecode();               ///< "00:03.40 / 00:08.00 · f 102" readout
  void JumpToTime(float fTime);        ///< playhead + engine scrub
  void StepToNeighborKey(bool bNext);  ///< playhead to the nearest key before/after
  void CommitKeyFromLocal(const plUuid& keyGuid); ///< emits KeyEdited from the local copy (curve drags)
  const KeyInfo* FindSelectedKey() const;
  void EmitKeyEdited();

  QListWidget* m_pClipList = nullptr;
  plQtUiTimelineWidget* m_pTimeline = nullptr;
  plQtUiCurveEditorWidget* m_pCurveEditor = nullptr;
  QLineEdit* m_pClipNameEdit = nullptr;
  QDoubleSpinBox* m_pClipDurationSpin = nullptr;
  QLabel* m_pHeaderLabel = nullptr;
  QLabel* m_pTimecodeLabel = nullptr;
  QComboBox* m_pSnapCombo = nullptr;
  QPushButton* m_pRemoveTrackButton = nullptr;

  // key editor row
  QWidget* m_pKeyEditor = nullptr;
  QDoubleSpinBox* m_pKeyTimeSpin = nullptr;
  QDoubleSpinBox* m_pKeyValueSpin = nullptr;
  QPushButton* m_pKeyColorButton = nullptr;
  QComboBox* m_pKeyEasingCombo = nullptr;

  plDynamicArray<ClipInfo> m_Clips;
  plUuid m_SelectedClip;
  plUuid m_SelectedTrack;
  plUuid m_SelectedKey;
  float m_fPlayhead = 0.0f;
  plColor m_KeyColor = plColor::White; ///< color of the selected key (edited via the button)
  bool m_bIgnoreChanges = false;
};
