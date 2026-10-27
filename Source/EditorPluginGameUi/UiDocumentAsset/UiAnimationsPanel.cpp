#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiAnimationsPanel.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Tree.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Algorithm/HashingUtils.h>
#include <Foundation/Math/ColorScheme.h>
#include <Foundation/Math/Math.h>
#include <Foundation/Strings/StringBuilder.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameUiPlugin/Elements/UiAnimationClip.h>
#include <GuiFoundation/EngineUi/EngineColorDialog.h>
#include <GuiFoundation/EngineUi/EngineTrackView.h>
#include <GuiFoundation/EngineUi/EngineUiControlStrip.h>

namespace
{
  constexpr float s_fRowHeight = 26.0f;
  constexpr float s_fHeadWidth = 180.0f;
  constexpr double s_fTicksPerSecond = 4800.0; // plCurveControlPointData ticks

  // the curve view is 150 to 220 high
  constexpr float s_fCurvesMinHeight = 150.0f;
  constexpr float s_fCurvesMaxHeight = 220.0f;

  const float s_SnapSteps[] = {1.0f / 30.0f, 0.01f, 0.0f};

  plColorGammaUB GetTargetColor(plInt32 iTarget)
  {
    // widget-kind palette from Documentation/GameUI
    switch (iTarget)
    {
      case plUiAnimTarget::OffsetX:
      case plUiAnimTarget::OffsetY:
        return plColorGammaUB(0x33, 0x9A, 0xF0); // blue: placement
      case plUiAnimTarget::Width:
      case plUiAnimTarget::Height:
        return plColorGammaUB(0x22, 0xB8, 0xCF); // cyan: size
      case plUiAnimTarget::Opacity:
        return plColorGammaUB(0x94, 0xD8, 0x2D); // lime: visibility
      case plUiAnimTarget::Scale:
        return plColorGammaUB(0xFC, 0xC4, 0x19); // yellow: scale
      case plUiAnimTarget::Rotation:
        return plColorGammaUB(0xE6, 0x49, 0x80); // pink: rotation
      case plUiAnimTarget::Fill:
        return plColorGammaUB(0xFF, 0x92, 0x2B); // orange: controls
      case plUiAnimTarget::Tint:
      default:
        return plColorGammaUB(0x84, 0x5E, 0xF7); // violet: color
    }
  }

  const char* GetTargetName(plInt32 iTarget)
  {
    switch (iTarget)
    {
      case plUiAnimTarget::OffsetX:
        return "Offset.X";
      case plUiAnimTarget::OffsetY:
        return "Offset.Y";
      case plUiAnimTarget::Opacity:
        return "Opacity";
      case plUiAnimTarget::Scale:
        return "Scale";
      case plUiAnimTarget::Width:
        return "Width";
      case plUiAnimTarget::Height:
        return "Height";
      case plUiAnimTarget::Rotation:
        return "Rotation";
      case plUiAnimTarget::Fill:
        return "Fill";
      case plUiAnimTarget::Tint:
      default:
        return "Tint";
    }
  }

  float ApplyEasingEditor(plInt32 iEasing, float fT)
  {
    // mirrors the runtime plUiAnimEasing evaluation, for the lane curve preview
    switch (iEasing)
    {
      case plUiAnimEasing::EaseIn:
        return fT * fT;
      case plUiAnimEasing::EaseOut:
        return 1.0f - (1.0f - fT) * (1.0f - fT);
      case plUiAnimEasing::EaseInOut:
        return fT < 0.5f ? 2.0f * fT * fT : 1.0f - 2.0f * (1.0f - fT) * (1.0f - fT);
      default:
        return fT;
    }
  }

  // vector-pair rows: X/Y component tracks of one element share a single lane

  plInt32 GetPairTarget(plInt32 iTarget)
  {
    switch (iTarget)
    {
      case plUiAnimTarget::OffsetX:
        return plUiAnimTarget::OffsetY;
      case plUiAnimTarget::OffsetY:
        return plUiAnimTarget::OffsetX;
      case plUiAnimTarget::Width:
        return plUiAnimTarget::Height;
      case plUiAnimTarget::Height:
        return plUiAnimTarget::Width;
      default:
        return -1;
    }
  }

  bool IsPairPrimary(plInt32 iTarget)
  {
    return iTarget == plUiAnimTarget::OffsetX || iTarget == plUiAnimTarget::Width;
  }

  const char* GetPairName(plInt32 iTarget)
  {
    return (iTarget == plUiAnimTarget::OffsetX || iTarget == plUiAnimTarget::OffsetY) ? "Offset" : "Size";
  }

  /// One row per track, or one for an X/Y pair of an element; [0] is always the X/Width component.
  void BuildTimelineRows(const plUiAnimationsPanel::ClipInfo* pClip, plDynamicArray<plHybridArray<plInt32, 2>>& out_rows)
  {
    out_rows.Clear();
    if (pClip == nullptr)
      return;

    plHybridArray<bool, 16> consumed;
    consumed.SetCount(pClip->m_Tracks.GetCount(), false);

    for (plUInt32 i = 0; i < pClip->m_Tracks.GetCount(); ++i)
    {
      if (consumed[i])
        continue;

      consumed[i] = true;
      plHybridArray<plInt32, 2>& row = out_rows.ExpandAndGetRef();
      row.PushBack(static_cast<plInt32>(i));

      const plInt32 iPairTarget = GetPairTarget(pClip->m_Tracks[i].m_iTarget);
      for (plUInt32 j = 0; iPairTarget >= 0 && j < pClip->m_Tracks.GetCount(); ++j)
      {
        if (!consumed[j] && pClip->m_Tracks[j].m_iTarget == iPairTarget && pClip->m_Tracks[j].m_sElement == pClip->m_Tracks[i].m_sElement)
        {
          consumed[j] = true;
          if (IsPairPrimary(pClip->m_Tracks[i].m_iTarget))
            row.PushBack(static_cast<plInt32>(j));
          else
            row.InsertAt(0, static_cast<plInt32>(j));
          break;
        }
      }
    }
  }

  plUInt64 IdOf(const plUuid& guid)
  {
    return plHashingUtils::xxHash64(&guid, sizeof(plUuid)) | 1;
  }

  /// "mm:ss.cc"
  plString FormatTime(float fSeconds)
  {
    const plUInt32 uiCentis = static_cast<plUInt32>(plMath::Round(plMath::Max(fSeconds, 0.0f) * 100.0f));
    plStringBuilder s;
    s.SetFormat("{}:{}.{}", plArgU(uiCentis / 6000, 2, true), plArgU((uiCentis / 100) % 60, 2, true), plArgU(uiCentis % 100, 2, true));
    return s;
  }

  // menu ids of the timeline's menus
  enum MenuId : plUInt32
  {
    EaseLinear = 1,
    EaseIn,
    EaseOut,
    EaseInOut,
    RemoveKeys,
    AddKeyHere,
    SelectAll,
  };
} // namespace

//////////////////////////////////////////////////////////////////////////
// plUiClipList

/// The clip list: a section header, the clips (name and length; double-click selects the clip in the property grid)
/// and Add Clip... and Remove Clip under them.
class plUiClipList : public plEditorUiContent
{
public:
  explicit plUiClipList(plUiAnimationsPanel* pPanel)
    : plEditorUiContent("GameUi.Clips")
    , m_pPanel(pPanel)
  {
    m_pAdapter = PL_DEFAULT_NEW(Adapter, this);

    if (!IsReady())
      return;

    m_pTree = PL_DEFAULT_NEW(plEditorUiTree, *GetSurface(), "GameUi.ClipTree", *m_pAdapter);
    m_pTree->m_Events.AddEventHandler(plMakeDelegate(&plUiClipList::OnTreeEvent, this));
  }

  ~plUiClipList()
  {
    if (m_pTree)
      m_pTree->m_Events.RemoveEventHandler(plMakeDelegate(&plUiClipList::OnTreeEvent, this));
  }

  void Refresh()
  {
    if (!m_pTree)
      return;

    m_pTree->Refresh();

    // the selected clip stays selected
    for (plUInt32 i = 0; i < GetClips().GetCount(); ++i)
    {
      if (GetClips()[i].m_Guid == m_pPanel->m_SelectedClip)
      {
        const plUInt64 uiItem = i + 1;
        m_bSyncing = true;
        m_pTree->SetSelection(plMakeArrayPtr(&uiItem, 1));
        m_bSyncing = false;
      }
    }
  }

  plEditorUiTree* GetTree() const { return m_pTree.Borrow(); }

protected:
  void OnUiUpdate() override
  {
    if (!m_pTree || GetSurface()->GetLogicalSize() == m_vBuiltSize)
      return;

    using namespace plEditorUi;
    m_vBuiltSize = GetSurface()->GetLogicalSize();

    plUiPanel* pRoot = Panel();
    pRoot->m_Color = Surface();

    SectionHeader(pRoot, plTranslate("GameUi.Animations.Clips"), plColorScheme::GetCategoryColor("UI", plColorScheme::CategoryColorUsage::OverlayIcon));

    plUiPanel* pList = Panel(pRoot);
    Fill(pList, plVec4(0, SectionHeaderHeight, 0, ControlHeight + 13));
    pList->m_Color = Surface();
    m_pTree->Build(pList);

    // Add Clip... and Remove Clip under the list
    plUiPanel* pButtons = Panel(pRoot);
    pButtons->m_vAnchorMin.y = 1;
    pButtons->m_vOffsets = plVec4(0, -(ControlHeight + 12), 0, ControlHeight + 12);
    pButtons->m_Color = Surface();

    auto* pLine = Panel(pButtons);
    pLine->m_vAnchorMax.y = 0;
    pLine->m_vOffsets = plVec4(0, 0, 0, 1);
    pLine->m_Color = Line();

    // text buttons as wide as their text; Add Clip... leads with its plus
    const plStringView sAdd = plTranslate("GameUi.Animations.AddClip");
    const plStringView sRemove = plTranslate("GameUi.Animations.RemoveClip");
    const float fAddWidth = plMath::Ceil(MeasureText(sAdd, FontSize - 1)) + 40.0f;
    const float fRemoveWidth = plMath::Ceil(MeasureText(sRemove, FontSize - 1)) + 20.0f;

    auto* pAdd = TextButton(pButtons, sAdd, "Clips.add");
    pAdd->m_vAnchorMin = pAdd->m_vAnchorMax = plVec2::MakeZero();
    pAdd->m_vOffsets = plVec4(8, 6, fAddWidth, ControlHeight);
    static_cast<plUiText*>(pAdd->m_Children[0])->m_vOffsets.x = 26;

    plUiImage* pPlus = ButtonIcon(pAdd, Icon::Add);
    pPlus->m_vAnchorMin = pPlus->m_vAnchorMax = plVec2(0, 0.5f);
    pPlus->m_vOffsets = plVec4(8, -7, 14, 14);
    pPlus->m_Color = Muted();

    auto* pRemove = TextButton(pButtons, sRemove, "Clips.remove");
    pRemove->m_vAnchorMin = pRemove->m_vAnchorMax = plVec2::MakeZero();
    pRemove->m_vOffsets = plVec4(16 + fAddWidth, 6, fRemoveWidth, ControlHeight);

    SetDocument(Finish(pRoot));
    Refresh();
  }

  void OnUiEvent(const plMsgUiEvent& event) override
  {
    if (event.m_sIdentifier.GetView() == "Clips.add")
    {
      m_pPanel->AddClipRequested();
    }
    else if (event.m_sIdentifier.GetView() == "Clips.remove")
    {
      if (const auto* pClip = m_pPanel->GetSelectedClip())
        m_pPanel->RemoveClipRequested(pClip->m_Guid);
    }
  }

private:
  const plDynamicArray<plUiAnimationsPanel::ClipInfo>& GetClips() const { return m_pPanel->m_Clips; }

  void ActivateClip(plUInt64 uiItem)
  {
    if (uiItem > 0 && uiItem <= GetClips().GetCount())
      m_pPanel->ClipActivated(GetClips()[static_cast<plUInt32>(uiItem - 1)].m_Guid);
  }

  class Adapter : public plEditorUiTreeAdapter
  {
  public:
    explicit Adapter(plUiClipList* pOwner)
      : m_pOwner(pOwner)
    {
    }

    void GetChildren(plUInt64 uiParent, plDynamicArray<plUInt64>& out_children) const override
    {
      out_children.Clear();
      if (uiParent != 0)
        return;

      for (plUInt32 i = 0; i < m_pOwner->GetClips().GetCount(); ++i)
        out_children.PushBack(i + 1);
    }

    void GetRow(plUInt64 uiItem, plEditorUiTreeRow& out_row) const override
    {
      const auto& clips = m_pOwner->GetClips();
      if (uiItem == 0 || uiItem > clips.GetCount())
        return;

      // "Open - 0.80s", the event that plays it as a dim tag, every trigger in the tooltip
      const plUiAnimationsPanel::ClipInfo& clip = clips[static_cast<plUInt32>(uiItem - 1)];
      plStringBuilder s;
      s.SetFormat("{} - {}s", clip.m_sName.IsEmpty() ? plStringView("<unnamed>") : clip.m_sName.GetView(), plArgF(clip.m_fDuration, 2));
      out_row.m_sText = s;
      out_row.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::AnimationClip);
      out_row.m_IconColor = plColorScheme::GetCategoryColor("UI", plColorScheme::CategoryColorUsage::OverlayIcon);
      out_row.m_sTag = clip.m_sPlayOnEvent;

      s.Clear();
      if (!clip.m_sPlayOnEvent.IsEmpty())
        s.AppendFormat("{} {}", plTranslate("GameUi.Animations.PlaysOn"), clip.m_sPlayOnEvent);
      if (!clip.m_sEntry.IsEmpty())
        s.AppendWithSeparator("\n", clip.m_sEntry);
      out_row.m_sTooltip = s;
    }

    bool HasDecorations() const override { return true; }

    void Activate(plUInt64 uiItem) override { m_pOwner->ActivateClip(uiItem); }

  private:
    plUiClipList* m_pOwner = nullptr;
  };

  void OnTreeEvent(const plEditorUiTreeEvent& e)
  {
    if (e.m_Type != plEditorUiTreeEvent::Type::SelectionChanged || m_bSyncing)
      return;

    plDynamicArray<plUInt64> selection;
    m_pTree->GetSelection(selection);
    m_pPanel->OnClipSelected(selection.IsEmpty() ? -1 : static_cast<plInt32>(selection[0]) - 1);
  }

  plUiAnimationsPanel* m_pPanel = nullptr;
  plUniquePtr<Adapter> m_pAdapter;
  plUniquePtr<plEditorUiTree> m_pTree;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bSyncing = false;
};

//////////////////////////////////////////////////////////////////////////
// plUiAnimationsPanel

plUiAnimationsPanel::plUiAnimationsPanel()
  : plEditorUiContent("GameUi.Animations")
{
  // left: the clips
  m_pClipList = static_cast<plUiClipList*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plUiClipList, this))));

  // right: header and transport, the track view, the curve view and the key editor
  CreateHeader();

  m_pTimeline = static_cast<plEditorUiTrackView*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiTrackView, "GameUi.Timeline"))));
  m_pTimeline->SetHeaderWidth(s_fHeadWidth);
  m_pTimeline->SetPersistKey("GameUi/Animations/TimeUnit");
  m_pTimeline->SetEmptyText(plTranslate("GameUi.Animations.NoClip"));
  m_pTimeline->SetSnapStep(s_SnapSteps[0]);
  m_pTimeline->m_Events.AddEventHandler(plMakeDelegate(&plUiAnimationsPanel::OnTimelineEvent, this));

  // the shared curve view for the selected row: drag keys in time and value, add and remove them
  m_pCurveView = static_cast<plEditorUiCurveView*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiCurveView, *static_cast<plEditorUiCurveEdits*>(this)))));

  CreateKeyEditor();

  m_Curves.m_uiFramesPerSecond = 30;
  RefreshKeyEditor();
}

plUiAnimationsPanel::~plUiAnimationsPanel()
{
  m_pTimeline->m_Events.RemoveEventHandler(plMakeDelegate(&plUiAnimationsPanel::OnTimelineEvent, this));
}

plEditorUiContent* plUiAnimationsPanel::GetClipList() const
{
  return m_pClipList;
}

void plUiAnimationsPanel::OnUiUpdate()
{
  if (!IsReady() || GetSurface()->GetLogicalSize() == m_vPlacedSize)
    return;

  m_vPlacedSize = GetSurface()->GetLogicalSize();

  plUiPanel* pRoot = plEditorUi::Panel();
  pRoot->m_Color = plEditorUi::Surface();
  SetDocument(plEditorUi::Finish(pRoot));

  PlaceParts();
}

void plUiAnimationsPanel::PlaceParts()
{
  const plVec2 vSize = m_vPlacedSize;
  const float fClipsWidth = plMath::Clamp(vSize.x * 0.2f, static_cast<float>(ClipListMinWidth), static_cast<float>(ClipListMaxWidth));
  const float fRightWidth = plMath::Max(vSize.x - fClipsWidth, 0.0f);

  const float fHeader = m_pHeader->GetHeight();
  const float fKeys = m_pBottom->GetHeight();
  const float fCurves = plMath::Clamp(vSize.y * 0.3f, s_fCurvesMinHeight, s_fCurvesMaxHeight);
  const float fTimeline = plMath::Max(vSize.y - fHeader - fCurves - fKeys, 0.0f);

  SetChildRect(m_pClipList, plRectFloat(0, 0, fClipsWidth, vSize.y));

  float fTop = 0.0f;
  SetChildRect(m_pHeader, plRectFloat(fClipsWidth, fTop, fRightWidth, fHeader));
  fTop += fHeader;
  SetChildRect(m_pTimeline, plRectFloat(fClipsWidth, fTop, fRightWidth, fTimeline));
  fTop += fTimeline;
  SetChildRect(m_pCurveView, plRectFloat(fClipsWidth, fTop, fRightWidth, fCurves));
  fTop += fCurves;
  SetChildRect(m_pBottom, plRectFloat(fClipsWidth, fTop, fRightWidth, fKeys));
}

void plUiAnimationsPanel::CreateHeader()
{
  using plEditorUi::Icon;

  // the clip's name and length, its triggers, then the transport, the time, snapping and Stop
  m_pHeader = static_cast<plEditorUiControlStrip*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiControlStrip))));

  m_uiName = m_pHeader->AddTextField("", plTranslate("GameUi.Animations.DlgName"), {}, 140.0f);
  m_uiDuration = m_pHeader->AddNumberField("", "s", "", {}, 86.0f);
  m_uiTrigger = m_pHeader->AddLabel("", -1.0f);
  m_pHeader->AddSeparator();

  // play is the trigger's preview, in the colour the mockup gives it
  m_uiToStart = m_pHeader->AddIconButton(Icon::ToStart, plTranslate("GameUi.Animations.JumpStartTooltip"));
  m_uiPrevKey = m_pHeader->AddIconButton(Icon::StepBack, plTranslate("GameUi.Animations.PrevKeyTooltip"));
  m_uiPlay = m_pHeader->AddIconButton(Icon::Play, plTranslate("GameUi.Animations.PlayTooltip"));
  m_pHeader->SetColor(m_uiPlay, plEditorUi::Axis(1));
  m_uiNextKey = m_pHeader->AddIconButton(Icon::StepForward, plTranslate("GameUi.Animations.NextKeyTooltip"));
  m_uiToEnd = m_pHeader->AddIconButton(Icon::ToEnd, plTranslate("GameUi.Animations.JumpEndTooltip"));

  // the time on a well in the accent, as wide as its longest text so it updates without a rebuild
  const float fTimeWidth = plMath::Ceil(plEditorUi::MeasureCodeText("00:00.00 f 0000 / 00:00.00", plEditorUi::FontSize - 1)) + 16.0f;
  m_uiTime = m_pHeader->AddCodeWell("00:00.00 f 0 / 00:00.00", plEditorUi::Accent(), fTimeWidth);

  plHybridArray<plString, 3> snaps;
  snaps.PushBack(plTranslate("GameUi.Animations.SnapFrame"));
  snaps.PushBack(plTranslate("GameUi.Animations.SnapTenMs"));
  snaps.PushBack(plTranslate("GameUi.Animations.SnapOff"));
  m_uiSnap = m_pHeader->AddDropdown(snaps, 0, plTranslate("GameUi.Animations.SnapTooltip"), 110.0f);
  m_pHeader->SetLead(m_uiSnap, plTranslate("GameUi.Animations.Snap"));
  m_uiStop = m_pHeader->AddIconTextButton(Icon::Stop, plTranslate("GameUi.Animations.Stop"));
  m_pHeader->AddSpacer();

  m_pHeader->m_OnChanged = [this](plUInt32 uiControl, plInt32 iValue)
  { OnHeader(uiControl, iValue); };
}

void plUiAnimationsPanel::CreateKeyEditor()
{
  using plEditorUi::Icon;

  // tracks, then the selected key: time, value, colour (Tint tracks), easing; the hint at the end
  m_pBottom = static_cast<plEditorUiControlStrip*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiControlStrip))));

  m_uiAddTrack = m_pBottom->AddIconTextButton(Icon::Add, plTranslate("GameUi.Animations.AddTrack"));
  m_uiRemoveTrack = m_pBottom->AddButton(plTranslate("GameUi.Animations.RemoveTrack"), {}, -1.0f);
  m_pBottom->AddSeparator();

  m_uiKeyTime = m_pBottom->AddNumberField("T", "s", "", plTranslate("GameUi.Animations.KeyTime"), 86.0f);
  m_uiKeyValue = m_pBottom->AddNumberField("V", "", "", plTranslate("GameUi.Animations.KeyValue"), 96.0f);
  m_uiKeyColor = m_pBottom->AddSwatch(plColorGammaUB(255, 255, 255), plTranslate("GameUi.Animations.KeyColorTooltip"));

  plHybridArray<plString, 4> easings;
  easings.PushBack(plTranslate("plUiAnimEasing::Linear"));
  easings.PushBack(plTranslate("plUiAnimEasing::EaseIn"));
  easings.PushBack(plTranslate("plUiAnimEasing::EaseOut"));
  easings.PushBack(plTranslate("plUiAnimEasing::EaseInOut"));
  m_uiKeyEasing = m_pBottom->AddDropdown(easings, 0, {}, 110.0f);
  m_uiRemoveKey = m_pBottom->AddButton(plTranslate("GameUi.Animations.RemoveKey"), {}, -1.0f);
  m_pBottom->AddHint(plTranslate("GameUi.Animations.Hint"));

  m_pBottom->m_OnChanged = [this](plUInt32 uiControl, plInt32 iValue)
  { OnKeyEditor(uiControl, iValue); };
}

//////////////////////////////////////////////////////////////////////////
// Clips

void plUiAnimationsPanel::SetClips(plArrayPtr<const ClipInfo> clips)
{
  m_Clips.Clear();
  m_Clips.PushBackRange(clips);

  // drop stale selection
  bool bClipFound = false, bTrackFound = false, bKeyFound = false;
  for (const ClipInfo& clip : m_Clips)
  {
    bClipFound |= clip.m_Guid == m_SelectedClip;
    for (const TrackInfo& track : clip.m_Tracks)
    {
      bTrackFound |= track.m_Guid == m_SelectedTrack;
      for (const KeyInfo& key : track.m_Keys)
        bKeyFound |= key.m_Guid == m_SelectedKey;
    }
  }

  if (!bClipFound)
    m_SelectedClip = m_Clips.IsEmpty() ? plUuid() : m_Clips[0].m_Guid;
  if (!bTrackFound)
    m_SelectedTrack = plUuid();
  if (!bKeyFound)
    m_SelectedKey = plUuid();

  m_pClipList->Refresh();
  RefreshHeader();
  RefreshTimeline();
  RefreshCurves();
  RefreshKeyEditor();
}

plUiAnimationsPanel::ClipInfo* plUiAnimationsPanel::GetSelectedClip()
{
  for (ClipInfo& clip : m_Clips)
  {
    if (clip.m_Guid == m_SelectedClip)
      return &clip;
  }

  return nullptr;
}

const plUiAnimationsPanel::ClipInfo* plUiAnimationsPanel::GetSelectedClip() const
{
  for (const ClipInfo& clip : m_Clips)
  {
    if (clip.m_Guid == m_SelectedClip)
      return &clip;
  }

  return nullptr;
}

void plUiAnimationsPanel::OnClipSelected(plInt32 iClip)
{
  const plUuid clip = iClip >= 0 && iClip < static_cast<plInt32>(m_Clips.GetCount()) ? m_Clips[iClip].m_Guid : plUuid();
  if (clip == m_SelectedClip)
    return;

  m_SelectedClip = clip;
  m_SelectedTrack = plUuid();
  m_SelectedKey = plUuid();
  m_fPlayhead = 0.0f;
  m_pTimeline->ClearSelection();

  RefreshHeader();
  RefreshTimeline();
  m_pTimeline->FrameAll();
  RefreshCurves();
  RefreshKeyEditor();

  // clear any scrub preview from the previously selected clip
  StopRequested();
}

void plUiAnimationsPanel::RefreshHeader()
{
  const ClipInfo* pClip = GetSelectedClip();

  for (plUInt32 uiControl : {m_uiName, m_uiDuration, m_uiToStart, m_uiPrevKey, m_uiPlay, m_uiNextKey, m_uiToEnd})
    m_pHeader->SetEnabled(uiControl, pClip != nullptr);

  if (pClip == nullptr)
  {
    m_pHeader->SetText(m_uiName, "");
    m_pHeader->SetText(m_uiDuration, "");
    m_pHeader->SetText(m_uiTrigger, plTranslate("GameUi.Animations.NoClip"));
    UpdateTimecode();
    return;
  }

  m_pHeader->SetText(m_uiName, pClip->m_sName);

  plStringBuilder s;
  s.SetFormat("{}", plArgF(pClip->m_fDuration, 2));
  m_pHeader->SetText(m_uiDuration, s);

  s.Clear();
  if (!pClip->m_sPlayOnEvent.IsEmpty())
    s.AppendFormat("{} {}", plTranslate("GameUi.Animations.PlaysOn"), pClip->m_sPlayOnEvent);

  if (!pClip->m_sEntry.IsEmpty())
    s.AppendFormat("{}{} {} ({})", s.IsEmpty() ? "" : " - ", plTranslate("GameUi.Animations.DrivenBy"), pClip->m_sEntry, plTranslate(pClip->m_bValueMode ? "GameUi.Animations.ModeValue" : "GameUi.Animations.ModeTrigger"));

  if (s.IsEmpty())
    s = plTranslate("GameUi.Animations.NoTriggerHint");

  m_pHeader->SetText(m_uiTrigger, s);
  UpdateTimecode();
}

void plUiAnimationsPanel::OnHeader(plUInt32 uiControl, plInt32 iValue)
{
  const ClipInfo* pClip = GetSelectedClip();

  if (uiControl == m_uiSnap)
  {
    m_pTimeline->SetSnapStep(s_SnapSteps[plMath::Clamp(iValue, 0, 2)]);
    return;
  }

  if (uiControl == m_uiStop)
  {
    StopRequested();
    return;
  }

  if (pClip == nullptr)
    return;

  if (uiControl == m_uiName)
  {
    const plString sName = m_pHeader->GetText(m_uiName);
    if (sName != pClip->m_sName)
      ClipRenamed(pClip->m_Guid, sName);
  }
  else if (uiControl == m_uiDuration)
  {
    double fDuration = 0.0;
    if (plConversionUtils::StringToFloat(m_pHeader->GetText(m_uiDuration), fDuration).Succeeded() && plMath::Abs(static_cast<float>(fDuration) - pClip->m_fDuration) > 0.001f)
      ClipDurationChanged(pClip->m_Guid, plMath::Clamp(static_cast<float>(fDuration), 0.01f, 3600.0f));
    else
      RefreshHeader();
  }
  else if (uiControl == m_uiToStart)
    JumpToTime(0.0f);
  else if (uiControl == m_uiPrevKey)
    StepToNeighborKey(false);
  else if (uiControl == m_uiPlay)
    PlayRequested(pClip->m_sName);
  else if (uiControl == m_uiNextKey)
    StepToNeighborKey(true);
  else if (uiControl == m_uiToEnd)
    JumpToTime(pClip->m_fDuration);
}

//////////////////////////////////////////////////////////////////////////
// Playhead

void plUiAnimationsPanel::SetPlayhead(float fTime)
{
  const ClipInfo* pClip = GetSelectedClip();
  const float fDuration = pClip != nullptr ? plMath::Max(pClip->m_fDuration, 0.0001f) : 1.0f;
  m_fPlayhead = plMath::Clamp(fTime, 0.0f, fDuration);

  m_pTimeline->SetPlayhead(m_fPlayhead);
  m_pCurveView->SetScrubberPosition(m_fPlayhead);
  UpdateTimecode();
}

float plUiAnimationsPanel::SnapTime(float fTime) const
{
  return static_cast<float>(m_pTimeline->Snap(fTime));
}

void plUiAnimationsPanel::UpdateTimecode()
{
  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr)
  {
    m_pHeader->SetText(m_uiTime, "00:00.00 f 0 / 00:00.00");
    return;
  }

  plStringBuilder s;
  s.SetFormat("{} f {} / {}", FormatTime(m_fPlayhead), static_cast<int>(plMath::Round(m_fPlayhead * 30.0f)), FormatTime(pClip->m_fDuration));
  m_pHeader->SetText(m_uiTime, s);
}

void plUiAnimationsPanel::JumpToTime(float fTime)
{
  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr)
    return;

  SetPlayhead(fTime);
  Scrubbed(pClip->m_sName, m_fPlayhead);
}

void plUiAnimationsPanel::StepToNeighborKey(bool bNext)
{
  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr)
    return;

  constexpr float fEpsilon = 0.0005f;
  float fBest = bNext ? pClip->m_fDuration : 0.0f;
  bool bFound = false;

  for (const TrackInfo& track : pClip->m_Tracks)
  {
    for (const KeyInfo& key : track.m_Keys)
    {
      if (bNext && key.m_fTime > m_fPlayhead + fEpsilon && (!bFound || key.m_fTime < fBest))
      {
        fBest = key.m_fTime;
        bFound = true;
      }
      else if (!bNext && key.m_fTime < m_fPlayhead - fEpsilon && (!bFound || key.m_fTime > fBest))
      {
        fBest = key.m_fTime;
        bFound = true;
      }
    }
  }

  // no neighbor key: fall to the clip bounds
  JumpToTime(bFound ? fBest : (bNext ? pClip->m_fDuration : 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Timeline

void plUiAnimationsPanel::RefreshTimeline()
{
  const ClipInfo* pClip = GetSelectedClip();
  BuildTimelineRows(pClip, m_Rows);
  m_Keys.Clear();

  plEngineTrackContent content;
  content.m_uiFramesPerSecond = 30;
  content.m_fDuration = pClip != nullptr ? plMath::Max(pClip->m_fDuration, 0.0001f) : 1.0;

  for (plUInt32 r = 0; pClip != nullptr && r < m_Rows.GetCount(); ++r)
  {
    const plHybridArray<plInt32, 2>& row = m_Rows[r];
    const TrackInfo& primary = pClip->m_Tracks[row[0]];
    const bool bPair = row.GetCount() > 1;

    // the head: the kind square (or the pair's axis colours) and "element . Target", in warning when the element is gone
    plEngineTrackLane& lane = content.m_Lanes.ExpandAndGetRef();
    lane.m_fHeight = s_fRowHeight;

    plStringBuilder sLabel;
    sLabel.SetFormat("{} . {}", primary.m_sElement.IsEmpty() ? "?" : primary.m_sElement.GetData(), bPair ? GetPairName(primary.m_iTarget) : GetTargetName(primary.m_iTarget));
    lane.m_sLabel = sLabel;
    if (bPair)
    {
      lane.m_Swatches.PushBack(plEditorUi::Axis(0));
      lane.m_Swatches.PushBack(plEditorUi::Axis(1));
    }
    else
    {
      lane.m_Swatches.PushBack(GetTargetColor(primary.m_iTarget));
    }

    if (!primary.m_bElementFound)
      lane.m_LabelColor = plEditorUi::Warning();

    for (plInt32 iTrack : row)
      lane.m_bHighlighted |= pClip->m_Tracks[iTrack].m_Guid == m_SelectedTrack;

    // the row's tracks share one value scale, so a pair's curves read against each other
    float fMin = 0.0f, fMax = 0.0f;
    bool bAny = false;
    for (plInt32 iTrack : row)
    {
      for (const KeyInfo& key : pClip->m_Tracks[iTrack].m_Keys)
      {
        fMin = bAny ? plMath::Min(fMin, key.m_fValue) : key.m_fValue;
        fMax = bAny ? plMath::Max(fMax, key.m_fValue) : key.m_fValue;
        bAny = true;
      }
    }

    const float fRange = fMax - fMin;
    auto normalize = [&](float fValue)
    { return fRange <= 0.0001f ? 0.5 : static_cast<double>((fValue - fMin) / fRange); };

    for (plUInt32 t = 0; t < row.GetCount(); ++t)
    {
      const TrackInfo& track = pClip->m_Tracks[row[t]];
      const plColorGammaUB color = bPair ? plEditorUi::Axis(t) : GetTargetColor(track.m_iTarget);
      const bool bTint = track.m_iTarget == plUiAnimTarget::Tint;

      if (bTint)
      {
        // a colour track: the gradient between its keys
        for (const KeyInfo& key : track.m_Keys)
          lane.m_Gradient.PushBack({key.m_fTime, plColorGammaUB(key.m_Color)});
      }
      else if (track.m_Keys.GetCount() > 1)
      {
        // a number track: its eased value curve, sampled so the easing shape shows
        plEngineTrackCurve& curve = lane.m_Curves.ExpandAndGetRef();
        curve.m_Color = color;
        curve.m_Points.PushBack(plVec2d(track.m_Keys[0].m_fTime, normalize(track.m_Keys[0].m_fValue)));
        for (plUInt32 k = 1; k < track.m_Keys.GetCount(); ++k)
        {
          const KeyInfo& prev = track.m_Keys[k - 1];
          const KeyInfo& next = track.m_Keys[k];
          for (int iSample = 1; iSample <= 8; ++iSample)
          {
            const float fT = iSample / 8.0f;
            curve.m_Points.PushBack(plVec2d(plMath::Lerp(prev.m_fTime, next.m_fTime, fT), normalize(plMath::Lerp(prev.m_fValue, next.m_fValue, ApplyEasingEditor(prev.m_iEasing, fT)))));
          }
        }
      }

      for (const KeyInfo& key : track.m_Keys)
      {
        plEngineTrackKey& item = content.m_Keys.ExpandAndGetRef();
        item.m_uiId = IdOf(key.m_Guid);
        item.m_uiLane = r;
        item.m_fTime = key.m_fTime;
        item.m_bSwatch = bTint;
        item.m_Color = bTint ? plColorGammaUB(key.m_Color) : color;

        KeyRef ref;
        ref.m_KeyGuid = key.m_Guid;
        ref.m_TrackGuid = track.m_Guid;
        m_Keys.Insert(item.m_uiId, ref);
      }
    }
  }

  m_pTimeline->SetContent(std::move(content));
  m_pTimeline->SetPlayhead(m_fPlayhead);

  // the key editor's key is selected in the view
  if (m_SelectedKey.IsValid() && m_pTimeline->GetSelectedKeys().IsEmpty())
  {
    const plUInt64 uiKey = IdOf(m_SelectedKey);
    m_pTimeline->SetSelectedKeys(plMakeArrayPtr(&uiKey, 1));
  }
}

void plUiAnimationsPanel::OnTimelineEvent(const plEngineTrackViewEvent& e)
{
  using Type = plEngineTrackViewEvent::Type;

  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr)
    return;

  const float fDuration = pClip->m_fDuration;

  switch (e.m_Type)
  {
    case Type::Scrub:
      SetPlayhead(static_cast<float>(e.m_fTime));
      Scrubbed(pClip->m_sName, m_fPlayhead);
      break;

    case Type::SelectionChanged:
      SyncKeySelection();
      break;

    case Type::MoveKeys:
    case Type::ScaleKeys:
    {
      // every selected key in one undo step; the view drew where they land
      plDynamicArray<KeyRef> keys;
      GetSelectedKeys(keys);
      if (keys.IsEmpty())
        break;

      plHybridArray<plUuid, 16> guids;
      plHybridArray<float, 16> times;
      for (const KeyRef& ref : keys)
      {
        const KeyInfo* pKey = FindKey(ref.m_KeyGuid);
        if (pKey == nullptr)
          continue;

        const double fTime = e.m_Type == Type::MoveKeys ? pKey->m_fTime + e.m_fDelta : e.m_fPivot + (pKey->m_fTime - e.m_fPivot) * e.m_fFactor;
        guids.PushBack(ref.m_KeyGuid);
        times.PushBack(plMath::Clamp(SnapTime(static_cast<float>(fTime)), 0.0f, fDuration));
      }

      EditsBegin(e.m_Type == Type::MoveKeys ? "Move UI Animation Keys" : "Retime UI Animation Keys");
      for (plUInt32 i = 0; i < guids.GetCount(); ++i)
        KeyMoved(guids[i], times[i]);
      EditsEnd(true);
      break;
    }

    case Type::AddKey:
    {
      // a key on every track of the row (both components of a pair)
      if (e.m_iLane < 0 || static_cast<plUInt32>(e.m_iLane) >= m_Rows.GetCount())
        break;

      plHybridArray<plUuid, 2> tracks;
      for (plInt32 iTrack : m_Rows[e.m_iLane])
        tracks.PushBack(pClip->m_Tracks[iTrack].m_Guid);

      EditsBegin("Add UI Animation Key");
      for (const plUuid& track : tracks)
        AddKeyRequested(track, plMath::Clamp(static_cast<float>(e.m_fTime), 0.0f, fDuration));
      EditsEnd(true);
      break;
    }

    case Type::DeleteSelection:
    {
      plDynamicArray<KeyRef> keys;
      GetSelectedKeys(keys);

      EditsBegin("Remove UI Animation Keys");
      for (const KeyRef& ref : keys)
        RemoveKeyRequested(ref.m_KeyGuid);
      EditsEnd(true);
      break;
    }

    case Type::KeyActivated:
    {
      // a Tint key opens the colour dialog
      KeyRef ref;
      const TrackInfo* pTrack = nullptr;
      if (m_Keys.TryGetValue(e.m_uiId, ref) && FindKey(ref.m_KeyGuid, &pTrack) != nullptr && pTrack->m_iTarget == plUiAnimTarget::Tint)
        PickKeyColor();
      break;
    }

    case Type::LaneClicked:
      SelectRow(e.m_iLane);
      break;

    case Type::ContextMenu:
      OnTimelineMenu(e);
      break;

    case Type::KeyPress:
    {
      const plEditorUiKeyEvent& key = *e.m_pKey;
      if (key.m_Key == plEditorUiKeyEvent::Key::Character && key.m_uiModifiers == plEditorUiPointerEvent::Control && (key.m_uiCharacter == 'a' || key.m_uiCharacter == 'A'))
      {
        plDynamicArray<plUInt64> all;
        for (auto it = m_Keys.GetIterator(); it.IsValid(); ++it)
          all.PushBack(it.Key());
        m_pTimeline->SetSelectedKeys(all);
        SyncKeySelection();
        key.m_bHandled = true;
      }
      break;
    }

    default:
      break;
  }
}

void plUiAnimationsPanel::OnTimelineMenu(const plEngineTrackViewEvent& e)
{
  using Target = plEngineTrackViewEvent::Target;

  plEditorUiMenu menu;
  if (e.m_Target == Target::Key)
  {
    auto& ease = menu.AddSubmenu("Easing");
    const char* szEasings[] = {"plUiAnimEasing::Linear", "plUiAnimEasing::EaseIn", "plUiAnimEasing::EaseOut", "plUiAnimEasing::EaseInOut"};
    for (plUInt32 i = 0; i < 4; ++i)
    {
      auto& item = ease.m_Children.ExpandAndGetRef();
      item.m_sLabel = plTranslate(szEasings[i]);
      item.m_uiId = MenuId::EaseLinear + i;
    }

    menu.AddSeparator();
    auto& remove = menu.AddAction(plTranslate("GameUi.Animations.RemoveKey"), MenuId::RemoveKeys, "Del");
    remove.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Clear);
    remove.m_bDanger = true;
  }
  else
  {
    if (e.m_iLane >= 0)
      menu.AddAction("Add Key Here", MenuId::AddKeyHere).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Add);
    menu.AddAction("Select All Keys", MenuId::SelectAll, "Ctrl+A");
  }

  const plInt32 iLane = e.m_iLane;
  const double fTime = e.m_fTime;
  m_pTimeline->ShowContextMenu(e.m_vPosition, std::move(menu), [this, iLane, fTime](plUInt32 uiId)
    {
    if (uiId >= MenuId::EaseLinear && uiId <= MenuId::EaseInOut)
    {
      SetSelectedEasing(static_cast<plInt32>(uiId - MenuId::EaseLinear));
    }
    else if (uiId == MenuId::RemoveKeys)
    {
      plEngineTrackViewEvent remove;
      remove.m_Type = plEngineTrackViewEvent::Type::DeleteSelection;
      OnTimelineEvent(remove);
    }
    else if (uiId == MenuId::AddKeyHere)
    {
      plEngineTrackViewEvent add;
      add.m_Type = plEngineTrackViewEvent::Type::AddKey;
      add.m_iLane = iLane;
      add.m_fTime = fTime;
      OnTimelineEvent(add);
    }
    else if (uiId == MenuId::SelectAll)
    {
      plDynamicArray<plUInt64> all;
      for (auto it = m_Keys.GetIterator(); it.IsValid(); ++it)
        all.PushBack(it.Key());
      m_pTimeline->SetSelectedKeys(all);
      SyncKeySelection();
    } });
}

void plUiAnimationsPanel::SelectRow(plInt32 iRow)
{
  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr || iRow < 0 || static_cast<plUInt32>(iRow) >= m_Rows.GetCount())
    return;

  // a pair selects its X component
  m_SelectedTrack = pClip->m_Tracks[m_Rows[iRow][0]].m_Guid;
  m_SelectedKey = plUuid();
  m_pTimeline->ClearSelection();

  RefreshTimeline();
  RefreshCurves();
  RefreshKeyEditor();
}

void plUiAnimationsPanel::SyncKeySelection()
{
  // the key clicked last drives the key editor and selects its track's row
  const auto& selection = m_pTimeline->GetSelectedKeys();
  KeyRef ref;
  if (!selection.IsEmpty() && m_Keys.TryGetValue(selection.PeekBack(), ref))
  {
    const bool bRowChanged = ref.m_TrackGuid != m_SelectedTrack;
    m_SelectedKey = ref.m_KeyGuid;
    m_SelectedTrack = ref.m_TrackGuid;

    if (bRowChanged)
    {
      RefreshTimeline();
      RefreshCurves();
    }
  }
  else
  {
    m_SelectedKey = plUuid();
  }

  RefreshKeyEditor();
}

bool plUiAnimationsPanel::IsKeySelected(const plUuid& keyGuid) const
{
  return m_pTimeline->GetSelectedKeys().Contains(IdOf(keyGuid));
}

void plUiAnimationsPanel::GetSelectedKeys(plDynamicArray<KeyRef>& out_keys) const
{
  out_keys.Clear();
  for (plUInt64 uiKey : m_pTimeline->GetSelectedKeys())
  {
    KeyRef ref;
    if (m_Keys.TryGetValue(uiKey, ref))
      out_keys.PushBack(ref);
  }
}

const plUiAnimationsPanel::KeyInfo* plUiAnimationsPanel::FindKey(const plUuid& keyGuid, const TrackInfo** out_pTrack) const
{
  for (const ClipInfo& clip : m_Clips)
  {
    for (const TrackInfo& track : clip.m_Tracks)
    {
      for (const KeyInfo& key : track.m_Keys)
      {
        if (key.m_Guid == keyGuid)
        {
          if (out_pTrack != nullptr)
            *out_pTrack = &track;
          return &key;
        }
      }
    }
  }

  return nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Curve view

void plUiAnimationsPanel::RefreshCurves()
{
  // a drag in the curve view previews on its own until it ends
  if (m_bCurveOperation)
    return;

  m_Curves.Clear();
  m_CurveTracks.Clear();
  m_CurveKeys.Clear();

  // the selected row's number tracks; Tint tracks have no curve
  const ClipInfo* pClip = GetSelectedClip();
  for (plUInt32 r = 0; pClip != nullptr && r < m_Rows.GetCount(); ++r)
  {
    bool bSelected = false;
    for (plInt32 iTrack : m_Rows[r])
      bSelected |= pClip->m_Tracks[iTrack].m_Guid == m_SelectedTrack;

    if (!bSelected)
      continue;

    for (plUInt32 t = 0; t < m_Rows[r].GetCount(); ++t)
    {
      const TrackInfo& track = pClip->m_Tracks[m_Rows[r][t]];
      if (track.m_iTarget == plUiAnimTarget::Tint)
        continue;

      plSingleCurveData* pCurve = PL_DEFAULT_NEW(plSingleCurveData);
      pCurve->m_CurveColor = m_Rows[r].GetCount() > 1 ? plEditorUi::Axis(t) : GetTargetColor(track.m_iTarget);

      // Easing maps to tangent modes: Linear, or flat Fixed Length tangents where the segment eases in or out.
      plDynamicArray<plUuid>& keys = m_CurveKeys.ExpandAndGetRef();
      for (plUInt32 k = 0; k < track.m_Keys.GetCount(); ++k)
      {
        const KeyInfo& key = track.m_Keys[k];
        const plInt32 iEasingBefore = k > 0 ? track.m_Keys[k - 1].m_iEasing : plUiAnimEasing::Linear;

        plCurveControlPointData& cp = pCurve->m_ControlPoints.ExpandAndGetRef();
        cp.m_iTick = static_cast<plInt64>(plMath::Round(key.m_fTime * s_fTicksPerSecond));
        cp.m_fValue = key.m_fValue;
        cp.m_bTangentsLinked = false;
        cp.m_RightTangentMode = (key.m_iEasing == plUiAnimEasing::EaseIn || key.m_iEasing == plUiAnimEasing::EaseInOut) ? plCurveTangentMode::FixedLength : plCurveTangentMode::Linear;
        cp.m_LeftTangentMode = (iEasingBefore == plUiAnimEasing::EaseOut || iEasingBefore == plUiAnimEasing::EaseInOut) ? plCurveTangentMode::FixedLength : plCurveTangentMode::Linear;
        cp.m_LeftTangent = plVec2(-0.1f, 0.0f);
        cp.m_RightTangent = plVec2(0.1f, 0.0f);
        keys.PushBack(key.m_Guid);
      }

      m_Curves.m_Curves.PushBack(pCurve);
      m_CurveTracks.PushBack(track.m_Guid);
    }

    break;
  }

  plHybridArray<plString, 2> names;
  for (const plUuid& trackGuid : m_CurveTracks)
  {
    const TrackInfo* pTrack = nullptr;
    for (const TrackInfo& track : pClip->m_Tracks)
    {
      if (track.m_Guid == trackGuid)
        pTrack = &track;
    }

    names.PushBack(pTrack != nullptr ? plString(GetTargetName(pTrack->m_iTarget)) : plString());
  }

  m_pCurveView->SetCurveExtents(0, pClip != nullptr ? pClip->m_fDuration : 1.0, true, true);
  m_pCurveView->SetCurves(m_Curves);
  m_pCurveView->SetCurveNames(names, "Selected row");
  m_pCurveView->SetScrubberPosition(m_fPlayhead);
}

void plUiAnimationsPanel::BeginCpChanges(plStringView sName)
{
  EditsBegin(sName);
}

void plUiAnimationsPanel::EndCpChanges()
{
  EditsEnd(true);
}

void plUiAnimationsPanel::BeginOperation(plStringView sName)
{
  // during a drag the curve view previews on its own; the keys it moves are the ones it was given
  m_bCurveOperation = true;
  EditsBegin(sName);
}

void plUiAnimationsPanel::EndOperation(bool bCommit)
{
  EditsEnd(bCommit);
  m_bCurveOperation = false;
  RefreshCurves();
}

void plUiAnimationsPanel::CpMoved(plUInt32 uiCurve, plUInt32 uiPoint, plInt64 iTick, double fValue)
{
  if (uiCurve >= m_CurveKeys.GetCount() || uiPoint >= m_CurveKeys[uiCurve].GetCount())
    return;

  const plUuid keyGuid = m_CurveKeys[uiCurve][uiPoint];
  const KeyInfo* pKey = FindKey(keyGuid);
  if (pKey == nullptr)
    return;

  const ClipInfo* pClip = GetSelectedClip();
  const float fTime = plMath::Clamp(static_cast<float>(iTick / s_fTicksPerSecond), 0.0f, pClip != nullptr ? pClip->m_fDuration : 3600.0f);
  KeyEdited(keyGuid, fTime, static_cast<float>(fValue), pKey->m_Color, pKey->m_iEasing);
}

void plUiAnimationsPanel::InsertCp(plUInt32 uiCurve, plInt64 iTick, double fValue)
{
  PL_IGNORE_UNUSED(fValue);
  if (uiCurve < m_CurveTracks.GetCount())
    AddKeyRequested(m_CurveTracks[uiCurve], static_cast<float>(iTick / s_fTicksPerSecond));
}

void plUiAnimationsPanel::CpDeleted(plUInt32 uiCurve, plUInt32 uiPoint)
{
  if (uiCurve < m_CurveKeys.GetCount() && uiPoint < m_CurveKeys[uiCurve].GetCount())
    RemoveKeyRequested(m_CurveKeys[uiCurve][uiPoint]);
}

//////////////////////////////////////////////////////////////////////////
// Key editor

void plUiAnimationsPanel::RefreshKeyEditor()
{
  const TrackInfo* pTrack = nullptr;
  const KeyInfo* pKey = m_SelectedKey.IsValid() ? FindKey(m_SelectedKey, &pTrack) : nullptr;
  const bool bTint = pTrack != nullptr && pTrack->m_iTarget == plUiAnimTarget::Tint;

  m_pBottom->SetEnabled(m_uiAddTrack, GetSelectedClip() != nullptr);
  m_pBottom->SetEnabled(m_uiRemoveTrack, m_SelectedTrack.IsValid());
  m_pBottom->SetEnabled(m_uiKeyTime, pKey != nullptr);
  m_pBottom->SetEnabled(m_uiKeyValue, pKey != nullptr && !bTint);
  m_pBottom->SetEnabled(m_uiKeyColor, pKey != nullptr && bTint);
  m_pBottom->SetEnabled(m_uiKeyEasing, pKey != nullptr);
  m_pBottom->SetEnabled(m_uiRemoveKey, pKey != nullptr);

  plStringBuilder s;
  if (pKey != nullptr)
  {
    s.SetFormat("{}", plArgF(pKey->m_fTime, 2));
    m_pBottom->SetText(m_uiKeyTime, s);
    s.SetFormat("{}", plArgF(pKey->m_fValue, 2));
    m_pBottom->SetText(m_uiKeyValue, bTint ? plStringView() : s.GetView());
    m_pBottom->SetSelected(m_uiKeyEasing, plMath::Clamp(pKey->m_iEasing, 0, 3));
  }
  else
  {
    m_pBottom->SetText(m_uiKeyTime, "");
    m_pBottom->SetText(m_uiKeyValue, "");
  }

  // the swatch shows a Tint key's colour, else a neutral well
  m_pBottom->SetColor(m_uiKeyColor, pKey != nullptr && bTint ? plColorGammaUB(pKey->m_Color.WithAlpha(1.0f)) : plEditorUi::Raised());
}

void plUiAnimationsPanel::OnKeyEditor(plUInt32 uiControl, plInt32 iValue)
{
  if (uiControl == m_uiAddTrack)
  {
    if (const ClipInfo* pClip = GetSelectedClip())
      AddTrackRequested(pClip->m_Guid);
  }
  else if (uiControl == m_uiRemoveTrack)
    RemoveSelectedTrack();
  else if (uiControl == m_uiKeyTime || uiControl == m_uiKeyValue)
    EmitKeyEdited();
  else if (uiControl == m_uiKeyColor)
    PickKeyColor();
  else if (uiControl == m_uiKeyEasing)
    SetSelectedEasing(iValue);
  else if (uiControl == m_uiRemoveKey)
  {
    plEngineTrackViewEvent remove;
    remove.m_Type = plEngineTrackViewEvent::Type::DeleteSelection;
    OnTimelineEvent(remove);
  }
}

void plUiAnimationsPanel::EmitKeyEdited()
{
  const KeyInfo* pKey = FindKey(m_SelectedKey);
  if (pKey == nullptr)
    return;

  double fTime = pKey->m_fTime, fValue = pKey->m_fValue;
  plConversionUtils::StringToFloat(m_pBottom->GetText(m_uiKeyTime), fTime).IgnoreResult();
  plConversionUtils::StringToFloat(m_pBottom->GetText(m_uiKeyValue), fValue).IgnoreResult();

  KeyEdited(m_SelectedKey, static_cast<float>(fTime), static_cast<float>(fValue), pKey->m_Color, pKey->m_iEasing);
}

void plUiAnimationsPanel::SetSelectedEasing(plInt32 iEasing)
{
  // every selected number key in one undo step
  plDynamicArray<KeyRef> keys;
  GetSelectedKeys(keys);

  EditsBegin("Set UI Animation Easing");
  for (const KeyRef& ref : keys)
  {
    if (const KeyInfo* pKey = FindKey(ref.m_KeyGuid))
    {
      const KeyInfo key = *pKey;
      KeyEdited(key.m_Guid, key.m_fTime, key.m_fValue, key.m_Color, iEasing);
    }
  }
  EditsEnd(true);
}

void plUiAnimationsPanel::PickKeyColor()
{
  const KeyInfo* pKey = FindKey(m_SelectedKey);
  if (pKey == nullptr)
    return;

  // the editor colour dialog with alpha: the key follows every change, OK keeps it as one undo step, Cancel undoes it
  const KeyInfo key = *pKey;
  plColor color = key.m_Color;

  EditsBegin("Edit UI Animation Key");

  const bool bPicked = plEngineColorDialog::PickColor(color, true, false, {}, [this, key](const plColor& preview)
    { KeyEdited(key.m_Guid, key.m_fTime, key.m_fValue, preview, key.m_iEasing); });

  if (bPicked)
    KeyEdited(key.m_Guid, key.m_fTime, key.m_fValue, color, key.m_iEasing);

  EditsEnd(bPicked);
}

void plUiAnimationsPanel::RemoveSelectedTrack()
{
  const ClipInfo* pClip = GetSelectedClip();
  if (!m_SelectedTrack.IsValid() || pClip == nullptr)
    return;

  // a pair row removes both of its component tracks
  plHybridArray<plUuid, 2> tracks;
  for (const auto& row : m_Rows)
  {
    bool bSelected = false;
    for (plInt32 iTrack : row)
      bSelected |= pClip->m_Tracks[iTrack].m_Guid == m_SelectedTrack;

    if (bSelected)
    {
      for (plInt32 iTrack : row)
        tracks.PushBack(pClip->m_Tracks[iTrack].m_Guid);
    }
  }

  EditsBegin("Remove UI Animation Track");
  for (const plUuid& track : tracks)
    RemoveTrackRequested(track);
  EditsEnd(true);
}

//////////////////////////////////////////////////////////////////////////
// What the delegates are told

void plUiAnimationsPanel::AddClipRequested()
{
  if (m_OnAddClipRequested.IsValid())
    m_OnAddClipRequested();
}

void plUiAnimationsPanel::RemoveClipRequested(const plUuid& clipGuid)
{
  if (m_OnRemoveClipRequested.IsValid())
    m_OnRemoveClipRequested(clipGuid);
}

void plUiAnimationsPanel::ClipActivated(const plUuid& clipGuid)
{
  if (m_OnClipActivated.IsValid())
    m_OnClipActivated(clipGuid);
}

void plUiAnimationsPanel::ClipRenamed(const plUuid& clipGuid, plStringView sNewName)
{
  if (m_OnClipRenamed.IsValid())
    m_OnClipRenamed(clipGuid, sNewName);
}

void plUiAnimationsPanel::ClipDurationChanged(const plUuid& clipGuid, float fNewDuration)
{
  if (m_OnClipDurationChanged.IsValid())
    m_OnClipDurationChanged(clipGuid, fNewDuration);
}

void plUiAnimationsPanel::AddTrackRequested(const plUuid& clipGuid)
{
  if (m_OnAddTrackRequested.IsValid())
    m_OnAddTrackRequested(clipGuid);
}

void plUiAnimationsPanel::RemoveTrackRequested(const plUuid& trackGuid)
{
  if (m_OnRemoveTrackRequested.IsValid())
    m_OnRemoveTrackRequested(trackGuid);
}

void plUiAnimationsPanel::AddKeyRequested(const plUuid& trackGuid, float fTime)
{
  if (m_OnAddKeyRequested.IsValid())
    m_OnAddKeyRequested(trackGuid, fTime);
}

void plUiAnimationsPanel::RemoveKeyRequested(const plUuid& keyGuid)
{
  if (m_OnRemoveKeyRequested.IsValid())
    m_OnRemoveKeyRequested(keyGuid);
}

void plUiAnimationsPanel::KeyMoved(const plUuid& keyGuid, float fNewTime)
{
  if (m_OnKeyMoved.IsValid())
    m_OnKeyMoved(keyGuid, fNewTime);
}

void plUiAnimationsPanel::KeyEdited(const plUuid& keyGuid, float fTime, float fValue, const plColor& color, plInt32 iEasing)
{
  if (!m_OnKeyEdited.IsValid())
    return;

  KeyInfo key;
  key.m_Guid = keyGuid;
  key.m_fTime = fTime;
  key.m_fValue = fValue;
  key.m_Color = color;
  key.m_iEasing = iEasing;
  m_OnKeyEdited(key);
}

void plUiAnimationsPanel::EditsBegin(plStringView sName)
{
  if (m_OnEditsBegin.IsValid())
    m_OnEditsBegin(sName);
}

void plUiAnimationsPanel::EditsEnd(bool bCommit)
{
  if (m_OnEditsEnd.IsValid())
    m_OnEditsEnd(bCommit);
}

void plUiAnimationsPanel::Scrubbed(plStringView sClipName, float fTime)
{
  if (m_OnScrubbed.IsValid())
    m_OnScrubbed(sClipName, fTime);
}

void plUiAnimationsPanel::PlayRequested(plStringView sClipName)
{
  if (m_OnPlayRequested.IsValid())
    m_OnPlayRequested(sClipName);
}

void plUiAnimationsPanel::StopRequested()
{
  if (m_OnStopRequested.IsValid())
    m_OnStopRequested();
}
