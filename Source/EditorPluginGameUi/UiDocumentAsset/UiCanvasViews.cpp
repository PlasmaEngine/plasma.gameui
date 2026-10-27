#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiCanvasViews.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Elements/UiDrawList.h>

namespace
{
  constexpr float s_fPadding = 6.0f;
  constexpr float s_fGap = 6.0f;
  constexpr float s_fIconGap = 2.0f;
  constexpr float s_fDeviceWidth = 196.0f;
  constexpr float s_fZoomWidth = 112.0f;
  constexpr const char* s_szLanguagePrefix = "Canvas.language";

  const plHashedString s_sRulerDrawing = plMakeHashedString("Ruler.Drawing");
  const plHashedString s_sStatusDrawing = plMakeHashedString("Status.Drawing");

  struct ToggleInfo
  {
    const char* m_szIcon;
    const char* m_szLabelKey;
    const char* m_szTooltipKey;
  };

  /// Icon, label and tooltip of each toggle, in Control order from HighlightUntranslated on.
  const ToggleInfo s_Toggles[] = {
    {"Font", "GameUi.Toolbar.HighlightUntranslated", "GameUi.Toolbar.HighlightUntranslatedTooltip"},
    {"Pointer", "GameUi.Toolbar.SelectMode", "GameUi.Toolbar.SelectModeTooltip"},
    {"Anchor", "GameUi.Toolbar.AnchorsMode", "GameUi.Toolbar.AnchorsModeTooltip"},
    {"Hand", "GameUi.Toolbar.Interact", "GameUi.Toolbar.InteractTooltip"},
    {"Magnet", "GameUi.Toolbar.Snap", "GameUi.Toolbar.SnapTooltip"},
    {"SafeArea", "GameUi.Toolbar.SafeArea", "GameUi.Toolbar.SafeAreaTooltip"},
    {"NavGraph", "GameUi.Toolbar.NavGraph", "GameUi.Toolbar.NavGraphTooltip"},
    {"Grid", "GameUi.Toolbar.Checker", "GameUi.Toolbar.CheckerTooltip"},
  };

  constexpr plUInt32 s_uiFirstToggle = static_cast<plUInt32>(plUiCanvasToolBarView::Control::HighlightUntranslated);

  plString ControlName(plUiCanvasToolBarView::Control control)
  {
    plStringBuilder sName;
    sName.SetFormat("Canvas.control.{}", static_cast<plUInt32>(control));
    return sName;
  }

  plString ToggleBinding(plUiCanvasToolBarView::Control control, bool bOn)
  {
    plStringBuilder sName;
    sName.SetFormat("Canvas.{}.{}", bOn ? "on" : "off", static_cast<plUInt32>(control));
    return sName;
  }

  /// One piece of a status line: dim words or a number in the code face.
  struct Segment
  {
    plStringView m_sText;
    bool m_bMono = false;
  };

  float MeasureSegment(const Segment& segment)
  {
    return segment.m_bMono ? plEditorUi::MeasureCodeText(segment.m_sText, plEditorUi::FontSize - 2) : plEditorUi::MeasureText(segment.m_sText, plEditorUi::FontSize - 1);
  }

  /// Draws the segments so they end at fRight; returns where they start.
  float DrawSegmentsRightAligned(plUiDrawListWriter& ref_writer, plArrayPtr<const Segment> segments, float fRight, float fHeight, plColorGammaUB textColor, plColorGammaUB monoColor)
  {
    float fTotal = 0.0f;
    for (const Segment& segment : segments)
      fTotal += MeasureSegment(segment);

    float x = fRight - fTotal;
    const float fStart = x;
    for (const Segment& segment : segments)
    {
      const float w = MeasureSegment(segment);
      ref_writer.Text(plRectFloat(x, 0, w + 2.0f, fHeight), segment.m_sText, segment.m_bMono ? monoColor : textColor, segment.m_bMono ? plEditorUi::FontSize - 2 : plEditorUi::FontSize - 1, plUiTextHAlign::Left, plUiTextVAlign::Center, segment.m_bMono);
      x += w;
    }

    return fStart;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plUiCanvasToolBarView

plUiCanvasToolBarView::plUiCanvasToolBarView()
  : plEditorUiContent("GameUi.CanvasToolBar")
{
  m_Checked[static_cast<int>(Control::SelectMode)] = true;
  m_Checked[static_cast<int>(Control::Snap)] = true;
}

plUiCanvasToolBarView::Choices& plUiCanvasToolBarView::GetChoices(Control control)
{
  if (control == Control::Zoom)
    return m_Zoom;
  if (control == Control::Language)
    return m_Language;
  return m_Device;
}

void plUiCanvasToolBarView::SetDevice(plArrayPtr<const plString> choices, plInt32 iSelected)
{
  m_Device.m_Items = choices;
  m_Device.m_iSelected = iSelected;
  m_bRebuild = true;
}

void plUiCanvasToolBarView::SetZoom(plArrayPtr<const plString> choices, plInt32 iSelected, bool bEnabled)
{
  m_Zoom.m_Items = choices;
  m_Zoom.m_iSelected = iSelected;
  m_Zoom.m_bEnabled = bEnabled;
  m_bRebuild = true;
}

void plUiCanvasToolBarView::SetLanguage(plArrayPtr<const plString> choices, plInt32 iSelected)
{
  m_Language.m_Items = choices;
  m_Language.m_iSelected = iSelected;
  m_bRebuild = true;
}

void plUiCanvasToolBarView::SetChecked(Control control, bool bChecked)
{
  m_Checked[static_cast<int>(control)] = bChecked;
  WriteToggleState(control);
}

void plUiCanvasToolBarView::WriteToggleState(Control control)
{
  if (!IsReady())
    return;

  auto& board = GetSurface()->GetBlackboard();
  board.SetEntryValue(ToggleBinding(control, true), m_Checked[static_cast<int>(control)]);
  board.SetEntryValue(ToggleBinding(control, false), !m_Checked[static_cast<int>(control)]);
}

void plUiCanvasToolBarView::OnUiUpdate()
{
  if (m_bRebuild || GetSurface()->GetLogicalSize().x != m_fBuiltWidth)
    BuildDocument();
}

void plUiCanvasToolBarView::BuildDocument()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_fBuiltWidth = GetSurface()->GetLogicalSize().x;
  auto& board = GetSurface()->GetBlackboard();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  auto* pLine = Panel(pRoot);
  pLine->m_vAnchorMin.y = 1;
  pLine->m_vOffsets = plVec4(0, -1, 0, 1);
  pLine->m_Color = Line();

  const float y = (Height - ControlHeight) * 0.5f;
  float x = s_fPadding;

  // the labelled dropdowns: Device and Zoom lead with their word, the language with a globe
  auto addDropdown = [&](Control control, float fWidth, plStringView sLead, plStringView sLeadIcon, const char* szTooltipKey)
  {
    const Choices& choices = GetChoices(control);
    const plStringView sText = (choices.m_iSelected >= 0 && choices.m_iSelected < static_cast<plInt32>(choices.m_Items.GetCount())) ? choices.m_Items[choices.m_iSelected].GetView() : plStringView();

    const plRectFloat rect(x, y, fWidth, ControlHeight);
    m_DropdownRects[static_cast<int>(control)] = rect;
    plGameUiEditor::Dropdown(pRoot, rect, ControlName(control), sLead, sLeadIcon, sText, choices.m_bEnabled);
    SetToolTip(board, ControlName(control), plTranslate(szTooltipKey));
    x += fWidth + s_fGap;
  };

  addDropdown(Control::Device, s_fDeviceWidth, plTranslate("GameUi.Toolbar.DeviceLead"), {}, "GameUi.Toolbar.DevicePresetTooltip");
  addDropdown(Control::Zoom, s_fZoomWidth, plTranslate("GameUi.Toolbar.ZoomLead"), {}, "GameUi.Toolbar.ZoomTooltip");

  auto addSeparator = [&]()
  {
    x += 4.0f - s_fIconGap;
    auto* pSeparator = Panel(pRoot);
    plGameUiEditor::Place(pSeparator, plRectFloat(x, (Height - 16.0f) * 0.5f, 1.0f, 16.0f));
    pSeparator->m_Color = Line();
    x += 1.0f + 4.0f;
  };

  // the language strip: a dim lead word, then one segment per language and the pseudo-localization
  if (!m_Language.m_Items.IsEmpty())
  {
    const plStringView sLead = plTranslate("GameUi.Toolbar.LanguageLead");
    const float fLeadWidth = plMath::Ceil(MeasureText(sLead, FontSize - 1));
    TextAt(pRoot, sLead, plRectFloat(x, y, fLeadWidth + 2.0f, ControlHeight), Dim());
    x += fLeadWidth + s_fGap;

    const float fStripWidth = plGameUiEditor::SegmentedWidth(m_Language.m_Items);
    plGameUiEditor::Segmented(pRoot, plRectFloat(x, y, fStripWidth, ControlHeight), s_szLanguagePrefix, m_Language.m_Items, m_Language.m_iSelected);

    for (plUInt32 i = 0; i < m_Language.m_Items.GetCount(); ++i)
      SetToolTip(board, plGameUiEditor::SegmentName(s_szLanguagePrefix, i), plTranslate("GameUi.Toolbar.LanguageTooltip"));

    x += fStripWidth + s_fGap;
  }

  // a one-off action: every button fits its content
  addSeparator();
  {
    const plStringView sFitAll = plTranslate("GameUi.Toolbar.FitAll");
    const float fWidth = plGameUiEditor::TextButtonWidth(sFitAll, true);
    plGameUiEditor::TextButton(pRoot, plRectFloat(x, y, fWidth, ControlHeight), ControlName(Control::FitAll), sFitAll, plGameUiEditor::IconPath("FitContent"));
    SetToolTip(board, ControlName(Control::FitAll), plTranslate("GameUi.Toolbar.FitAllTooltip"));
    x += fWidth + s_fGap;
  }
  addSeparator();

  auto addToggle = [&](Control control)
  {
    const ToggleInfo& info = s_Toggles[static_cast<plUInt32>(control) - s_uiFirstToggle];
    plGameUiEditor::IconToggle(pRoot, plRectFloat(x, y, ControlHeight, ControlHeight), ControlName(control), plGameUiEditor::IconPath(info.m_szIcon), ToggleBinding(control, true), ToggleBinding(control, false));

    // style pass D6: the label leads the tooltip
    plStringBuilder sTooltip;
    sTooltip.SetFormat("{} \xE2\x80\x94 {}", plTranslate(info.m_szLabelKey), plTranslate(info.m_szTooltipKey));
    SetToolTip(board, ControlName(control), sTooltip);

    WriteToggleState(control);
    x += ControlHeight + s_fIconGap;
  };

  addToggle(Control::HighlightUntranslated);
  addSeparator();
  addToggle(Control::SelectMode);
  addToggle(Control::AnchorsMode);
  addSeparator();
  addToggle(Control::Interact);
  addToggle(Control::Snap);
  addToggle(Control::SafeArea);
  addToggle(Control::NavGraph);
  addToggle(Control::Checker);

  SetDocument(Finish(pRoot));
}

bool plUiCanvasToolBarView::HandleLanguageClick(plStringView sIdentifier)
{
  for (plUInt32 i = 0; i < m_Language.m_Items.GetCount(); ++i)
  {
    if (sIdentifier != plGameUiEditor::SegmentName(s_szLanguagePrefix, i).GetView())
      continue;

    // clicking the selected language again shows the authored texts
    const plInt32 iChoice = static_cast<plInt32>(i) == m_Language.m_iSelected ? -1 : static_cast<plInt32>(i);
    if (m_OnChanged.IsValid())
      m_OnChanged(Control::Language, iChoice);
    return true;
  }

  return false;
}

void plUiCanvasToolBarView::OnUiEvent(const plMsgUiEvent& event)
{
  if (HandleLanguageClick(event.m_sIdentifier.GetView()))
    return;

  for (plUInt32 i = 0; i < static_cast<plUInt32>(Control::Count); ++i)
  {
    const Control control = static_cast<Control>(i);
    if (event.m_sIdentifier.GetView() != ControlName(control).GetView())
      continue;

    if (control == Control::FitAll)
    {
      if (m_OnChanged.IsValid())
        m_OnChanged(control, 1);
      return;
    }

    if (i < s_uiFirstToggle)
    {
      OpenMenu(control);
      return;
    }

    // Select and Anchors are one choice; clicking the current one keeps it
    if (control == Control::SelectMode || control == Control::AnchorsMode)
    {
      if (m_OnChanged.IsValid())
        m_OnChanged(control, 1);
      return;
    }

    if (m_OnChanged.IsValid())
      m_OnChanged(control, m_Checked[i] ? 0 : 1);
    return;
  }
}

void plUiCanvasToolBarView::OpenMenu(Control control)
{
  const Choices& choices = GetChoices(control);
  if (!choices.m_bEnabled || choices.m_Items.IsEmpty())
    return;

  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < choices.m_Items.GetCount(); ++i)
  {
    auto& item = menu.AddAction(choices.m_Items[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == choices.m_iSelected;
  }

  const plRectFloat& rect = m_DropdownRects[static_cast<int>(control)];
  menu.m_fMinWidth = rect.width;

  plEditorUiMenuView::Show(*GetSurface(), plVec2(rect.x, rect.y + rect.height), menu, [this, control](plUInt32 uiChoice)
    {
      if (m_OnChanged.IsValid())
        m_OnChanged(control, static_cast<plInt32>(uiChoice)); });
}

//////////////////////////////////////////////////////////////////////////
// plUiRulerView

plUiRulerView::plUiRulerView(bool bHorizontal, float fCorner)
  : plEditorUiContent(bHorizontal ? "GameUi.RulerH" : "GameUi.RulerV")
  , m_bHorizontal(bHorizontal)
  , m_fCorner(fCorner)
{
}

void plUiRulerView::OnUiUpdate()
{
  using namespace plEditorUi;

  if (!m_bBuilt)
  {
    m_bBuilt = true;

    plUiPanel* pRoot = Panel();
    pRoot->m_Color = Surface();
    DrawList(pRoot, "Ruler", s_sRulerDrawing.GetView());
    SetDocument(Finish(pRoot));
  }

  float fScale = 0.0f;
  float fOrigin = 0.0f;
  if (m_GetTransform)
    m_GetTransform(fScale, fOrigin);

  // the transform is in widget pixels, the drawing in surface units
  fScale /= GetUiScale();
  fOrigin /= GetUiScale();

  if (GetSurface()->GetLogicalSize() != m_vDrawnSize || fScale != m_fDrawnScale || fOrigin != m_fDrawnOrigin)
  {
    m_vDrawnSize = GetSurface()->GetLogicalSize();
    m_fDrawnScale = fScale;
    m_fDrawnOrigin = fOrigin;
    Draw();
  }
}

void plUiRulerView::Draw()
{
  using namespace plEditorUi;

  const plVec2 vSize = m_vDrawnSize;
  const float fScale = m_fDrawnScale;
  const float fOrigin = m_fDrawnOrigin + m_fCorner;
  const float fLength = m_bHorizontal ? vSize.x : vSize.y;

  plUiDrawListWriter writer;

  // the edge towards the canvas, and the corner square over the other ruler
  if (m_bHorizontal)
  {
    writer.Rect(plRectFloat(0, vSize.y - 1.0f, vSize.x, 1.0f), Line());
    if (m_fCorner > 0.0f)
      writer.Rect(plRectFloat(m_fCorner - 1.0f, 0, 1.0f, vSize.y), Line());
  }
  else
  {
    writer.Rect(plRectFloat(vSize.x - 1.0f, 0, 1.0f, vSize.y), Line());
  }

  if (fScale > 0.0f)
  {
    // a canvas-space step that leaves at least 60 units between labels
    const float steps[] = {10, 25, 50, 100, 200, 500, 1000, 2000};
    float fStep = steps[PL_ARRAY_SIZE(steps) - 1];
    for (float fCandidate : steps)
    {
      if (fCandidate * fScale >= 60.0f)
      {
        fStep = fCandidate;
        break;
      }
    }

    if (m_bHorizontal)
      writer.PushClip(plRectFloat(m_fCorner, 0, plMath::Max(vSize.x - m_fCorner, 0.0f), vSize.y));

    const float fFirst = plMath::Floor((m_fCorner - fOrigin) / fScale / fStep) * fStep;
    const plColorGammaUB tickColor = Border();
    const plColorGammaUB labelColor = Dim();

    for (float fCanvas = fFirst;; fCanvas += fStep)
    {
      const float p = fCanvas * fScale + fOrigin;
      if (p > fLength)
        break;

      plStringBuilder sLabel;
      sLabel.SetFormat("{}", static_cast<plInt32>(fCanvas));

      if (m_bHorizontal)
      {
        writer.Line(plVec2(p + 0.5f, 10.0f), plVec2(p + 0.5f, vSize.y - 1.0f), tickColor);
        writer.Text(plRectFloat(p + 3.0f, 0.0f, 48.0f, 11.0f), sLabel, labelColor, MinFontSize, plUiTextHAlign::Left, plUiTextVAlign::Center, true);
      }
      else
      {
        writer.Line(plVec2(10.0f, p + 0.5f), plVec2(vSize.x - 1.0f, p + 0.5f), tickColor);

        // the label reads downwards, one character per line
        float fCharY = p + 2.0f;
        for (auto it = sLabel.GetIteratorFront(); it.IsValid(); ++it)
        {
          plStringBuilder sChar;
          sChar.Append(it.GetCharacter());
          writer.Text(plRectFloat(1.0f, fCharY, 10.0f, 10.0f), sChar, labelColor, MinFontSize, plUiTextHAlign::Center, plUiTextVAlign::Center, true);
          fCharY += 9.0f;
        }
      }

      for (plUInt32 i = 1; i < 4; ++i)
      {
        const float fMinor = plMath::Round(p + fStep * fScale * i / 4.0f) + 0.5f;
        if (m_bHorizontal)
          writer.Line(plVec2(fMinor, 15.0f), plVec2(fMinor, vSize.y - 1.0f), tickColor);
        else
          writer.Line(plVec2(15.0f, fMinor), plVec2(vSize.x - 1.0f, fMinor), tickColor);
      }
    }

    if (m_bHorizontal)
      writer.PopClip();
  }

  writer.Publish(GetSurface()->GetBlackboard(), s_sRulerDrawing);
}

//////////////////////////////////////////////////////////////////////////
// plUiCanvasStatusRow

plUiCanvasStatusRow::plUiCanvasStatusRow()
  : plEditorUiContent("GameUi.CanvasStatus")
{
}

float plUiCanvasStatusRow::GetHeight()
{
  return plEditorUi::StatusRowHeight;
}

void plUiCanvasStatusRow::SetEngineState(bool bConnected, plStringView sText)
{
  m_bConnected = bConnected;
  m_sEngineText = sText;
  m_bDirty = true;
}

void plUiCanvasStatusRow::SetSelection(plStringView sElement, plStringView sPosition, plStringView sSize)
{
  m_sElement = sElement;
  m_sPosition = sPosition;
  m_sSize = sSize;
  m_sMessage.Clear();
  m_bDirty = true;
}

void plUiCanvasStatusRow::SetMessage(plStringView sMessage)
{
  m_sMessage = sMessage;
  m_bDirty = true;
}

void plUiCanvasStatusRow::SetDrawStats(plStringView sBatches, plStringView sQuads)
{
  m_sBatches = sBatches;
  m_sQuads = sQuads;
  m_bDirty = true;
}

void plUiCanvasStatusRow::SetTextFitSummary(plUInt32 uiOverflowing, plUInt32 uiFitting)
{
  if (m_uiOverflowing == uiOverflowing && m_uiFitting == uiFitting)
    return;

  m_uiOverflowing = uiOverflowing;
  m_uiFitting = uiFitting;
  m_bDirty = true;
}

void plUiCanvasStatusRow::OnUiUpdate()
{
  using namespace plEditorUi;

  if (!m_bBuilt)
  {
    m_bBuilt = true;

    plUiPanel* pRoot = Panel();
    pRoot->m_Color = Background();
    DrawList(pRoot, "Status", s_sStatusDrawing.GetView());
    SetDocument(Finish(pRoot));
  }

  if (m_bDirty || GetSurface()->GetLogicalSize() != m_vDrawnSize)
  {
    m_bDirty = false;
    m_vDrawnSize = GetSurface()->GetLogicalSize();
    Draw();
  }
}

void plUiCanvasStatusRow::Draw()
{
  using namespace plEditorUi;

  const plVec2 vSize = m_vDrawnSize;
  plUiDrawListWriter writer;

  writer.Rect(plRectFloat(0, 0, vSize.x, 1.0f), Line());

  // left: the engine preview state with its dot
  const plColorGammaUB dotColor = m_bConnected ? plColorGammaUB(0x69, 0xDB, 0x7C) : Danger();
  const plColorGammaUB stateColor = plGameUiEditor::Mix(dotColor, Foreground(), 0.18f);
  writer.Circle(plVec2(11.0f, vSize.y * 0.5f), 3.0f, dotColor);
  const float fStateWidth = MeasureText(m_sEngineText, FontSize - 1);
  writer.Text(plRectFloat(19.0f, 0, fStateWidth + 2.0f, vSize.y), m_sEngineText, stateColor, FontSize - 1);

  // left, after the state: the Text fit summary, the overflow count in the warning colour
  float fLeft = 19.0f + fStateWidth;
  if (m_uiOverflowing > 0 || m_uiFitting > 0)
  {
    // the translations carry the {} of the counts
    plStringBuilder sFormat;
    plStringBuilder sOverflowing;
    if (m_uiOverflowing > 0)
    {
      sFormat = plTranslate("GameUi.Status.Overflowing");
      sOverflowing.SetFormat(sFormat.GetData(), m_uiOverflowing);
    }
    else
    {
      sOverflowing = plTranslate("GameUi.Status.AllFit");
    }

    plStringBuilder sFitting;
    sFormat = plTranslate("GameUi.Status.FitToContent");
    sFitting.SetFormat(sFormat.GetData(), m_uiFitting);

    fLeft += 16.0f;
    const float fOverflowWidth = MeasureText(sOverflowing, FontSize - 1);
    writer.Text(plRectFloat(fLeft, 0, fOverflowWidth + 2.0f, vSize.y), sOverflowing, m_uiOverflowing > 0 ? Warning() : Foreground(), FontSize - 1);
    fLeft += fOverflowWidth;

    plStringBuilder sRest(" \xC2\xB7 ", sFitting);
    const float fRestWidth = MeasureText(sRest, FontSize - 1);
    writer.Text(plRectFloat(fLeft, 0, fRestWidth + 2.0f, vSize.y), sRest, Dim(), FontSize - 1);
    fLeft += fRestWidth;
  }

  // right: draw statistics, then the selection left of them
  float fRight = vSize.x - 8.0f;
  if (!m_sBatches.IsEmpty())
  {
    const Segment segments[] = {{"draw: "}, {m_sBatches, true}, {" batches \xC2\xB7 "}, {m_sQuads, true}, {" quads"}};
    fRight = DrawSegmentsRightAligned(writer, segments, fRight, vSize.y, Dim(), Muted()) - 12.0f;
  }

  const float fLeftLimit = fLeft + 16.0f;
  if (!m_sMessage.IsEmpty())
  {
    const Segment segments[] = {{m_sMessage}};
    if (fRight - MeasureSegment(segments[0]) > fLeftLimit)
      DrawSegmentsRightAligned(writer, segments, fRight, vSize.y, Dim(), Muted());
  }
  else if (!m_sElement.IsEmpty())
  {
    plStringBuilder sElement(m_sElement, " \xE2\x80\x94 rect ");
    const Segment segments[] = {{sElement}, {m_sPosition, true}, {" \xC2\xB7 "}, {m_sSize, true}, {" px"}};

    float fTotal = 0.0f;
    for (const Segment& segment : segments)
      fTotal += MeasureSegment(segment);

    if (fRight - fTotal > fLeftLimit)
      DrawSegmentsRightAligned(writer, segments, fRight, vSize.y, Dim(), Muted());
  }

  writer.Publish(GetSurface()->GetBlackboard(), s_sStatusDrawing);
}
