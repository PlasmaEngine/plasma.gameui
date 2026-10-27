#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiTextFitViews.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>

namespace
{
  constexpr float s_fHeaderHeight = 30.0f;
  constexpr float s_fRowHeight = 26.0f;
  constexpr float s_fLabelWidth = 112.0f;
  constexpr float s_fInset = 10.0f;
  constexpr float s_fValueWidth = 50.0f;
  constexpr float s_fEmptyHeight = 64.0f;
  constexpr float s_fIssueHeight = 38.0f;

  constexpr const char* s_szSizePrefix = "Fit.size";
  constexpr const char* s_szMinEntry = "Fit.min";
  constexpr const char* s_szMaxEntry = "Fit.max";
  constexpr const char* s_szMinText = "Fit.min.text";
  constexpr const char* s_szMaxText = "Fit.max.text";
  constexpr const char* s_szOverflow = "Fit.overflow";

  constexpr float s_fMinWidthLimit = 600.0f;
  constexpr float s_fMaxWidthLimit = 1200.0f;

  const char* s_szOverflowModes[] = {"plUiTextOverflow::Overflow", "plUiTextOverflow::Clip", "plUiTextOverflow::Ellipsis", "plUiTextOverflow::Shrink"};

  /// The green of the theme for "fits" values (the Text fit list uses the same colours as the canvas marks).
  plColorGammaUB FitsColor()
  {
    return plColorGammaUB(0x73, 0xC3, 0x91);
  }

  plColorGammaUB OverflowColor()
  {
    return plColorGammaUB(0xD7, 0x82, 0x78);
  }

  plString FormatPx(float fValue)
  {
    plStringBuilder sText;
    sText.SetFormat("{}", static_cast<plInt32>(plMath::Round(fValue)));
    return sText;
  }

  /// One label / value row: the muted label and the value column's rect.
  plRectFloat Row(plUiElement* pParent, float fY, float fWidth, plStringView sLabel)
  {
    using namespace plEditorUi;

    TextAt(pParent, sLabel, plRectFloat(s_fInset, fY, s_fLabelWidth - s_fInset, s_fRowHeight), Muted());
    return plRectFloat(s_fLabelWidth, fY + 1.0f, plMath::Max(fWidth - s_fLabelWidth - s_fInset, 40.0f), s_fRowHeight - 2.0f);
  }

  /// A slider over 0..fLimit bound to a surface entry, with its value in the code face at the right.
  void SliderRow(plUiElement* pParent, const plRectFloat& rect, plStringView sEntry, plStringView sTextEntry, float fLimit)
  {
    using namespace plEditorUi;

    const plRectFloat track(rect.x, rect.y + (rect.height - 14.0f) * 0.5f, plMath::Max(rect.width - s_fValueWidth - 6.0f, 20.0f), 14.0f);

    plUiSlider* pSlider = PL_DEFAULT_NEW(plUiSlider);
    pSlider->m_sName = sEntry;
    pSlider->m_sValueBinding = sEntry;
    pSlider->m_fMin = 0.0f;
    pSlider->m_fMax = fLimit;
    pSlider->m_Color = Field();
    pSlider->m_FillColor = plGameUiEditor::WithAlpha(Accent(), 170);
    pSlider->m_HandleColor = Foreground();
    pSlider->m_vCornerRadius = plVec4(7.0f);
    pSlider->m_fBorderWidth = 1.0f;
    pSlider->m_BorderColor = Line();
    plGameUiEditor::Place(pSlider, track);
    pParent->m_Children.PushBack(pSlider);

    auto* pValue = Text(pParent, plStringView(), sTextEntry);
    plGameUiEditor::Place(pValue, plRectFloat(rect.x + rect.width - s_fValueWidth, rect.y, s_fValueWidth, rect.height));
    pValue->m_hFont = GetCodeFont();
    pValue->m_uiFontSize = FontSize - 2;
    pValue->m_HAlign = plUiTextHAlign::Right;
    pValue->m_VAlign = plUiTextVAlign::Center;
    pValue->m_Color = Foreground();
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plUiSizeFitView

plUiSizeFitView::plUiSizeFitView()
  : plEditorUiContent("GameUi.SizeFit")
{
}

float plUiSizeFitView::GetHeight() const
{
  if (!m_State.m_bHasElement)
    return s_fEmptyHeight;

  plUInt32 uiRows = 3; // text, size, width (or two limits counted below)
  if (m_State.m_bFit)
    ++uiRows;
  if (m_State.m_iOverflow >= 0)
    ++uiRows;
  if (m_State.m_bHasNeeds)
    ++uiRows;

  return s_fHeaderHeight + uiRows * s_fRowHeight + 8.0f;
}

void plUiSizeFitView::SetState(const State& state)
{
  // while a limit is dragged the section keeps its slider (the document follows the drag)
  const bool bShapeChanged = state.m_bHasElement != m_State.m_bHasElement || state.m_bFit != m_State.m_bFit || state.m_pType != m_State.m_pType ||
                             state.m_iOverflow != m_State.m_iOverflow || state.m_bHasNeeds != m_State.m_bHasNeeds;
  const bool bTextChanged = state.m_sText != m_State.m_sText || state.m_sTypeName != m_State.m_sTypeName || state.m_fWidth != m_State.m_fWidth ||
                            state.m_fNeeded != m_State.m_fNeeded || state.m_fAvailable != m_State.m_fAvailable;

  m_State = state;

  if (bShapeChanged || bTextChanged)
    m_bRebuild = true;
  else if (!m_bDragging)
    WriteSliderValues();
}

void plUiSizeFitView::WriteSliderValues()
{
  if (!IsReady())
    return;

  auto& board = GetSurface()->GetBlackboard();
  board.SetEntryValue(s_szMinEntry, m_State.m_vMinSize.x);
  board.SetEntryValue(s_szMaxEntry, m_State.m_vMaxSize.x);
  board.SetEntryValue(s_szMinText, FormatPx(m_State.m_vMinSize.x));
  board.SetEntryValue(s_szMaxText, m_State.m_vMaxSize.x > 0.0f ? FormatPx(m_State.m_vMaxSize.x) : plString(plTranslate("GameUi.Fit.NoLimit")));

  m_fReportedMin = m_State.m_vMinSize.x;
  m_fReportedMax = m_State.m_vMaxSize.x;
}

void plUiSizeFitView::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();

  if (m_State.m_bHasElement && m_State.m_bFit)
    PollSliders();
}

void plUiSizeFitView::PollSliders()
{
  const auto& board = GetSurface()->GetBlackboard();
  const float fMin = plMath::Round(board.GetEntryValue(plTempHashedString(s_szMinEntry), 0.0f).ConvertTo<float>());
  const float fMax = plMath::Round(board.GetEntryValue(plTempHashedString(s_szMaxEntry), 0.0f).ConvertTo<float>());
  const bool bDragging = GetSurface()->IsDragging();

  const bool bChanged = fMin != m_fReportedMin || fMax != m_fReportedMax;
  const bool bDragEnded = m_bDragging && !bDragging;

  if (!bChanged && !bDragEnded)
    return;

  m_fReportedMin = fMin;
  m_fReportedMax = fMax;
  m_bDragging = bDragging;

  auto& writable = GetSurface()->GetBlackboard();
  writable.SetEntryValue(s_szMinText, FormatPx(fMin));
  writable.SetEntryValue(s_szMaxText, fMax > 0.0f ? FormatPx(fMax) : plString(plTranslate("GameUi.Fit.NoLimit")));

  if (m_OnWidthLimitsChanged.IsValid())
    m_OnWidthLimitsChanged(fMin, fMax, !bDragging);
}

void plUiSizeFitView::BuildDocument()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  if (!m_State.m_bHasElement)
  {
    plGameUiEditor::EmptyState(pRoot, plGameUiEditor::IconPath("FitContent"), plTranslate("GameUi.Fit.NoSelection"));
    SetDocument(Finish(pRoot));
    return;
  }

  BuildTypeHeader(pRoot, 0.0f);
  BuildRows(pRoot, s_fHeaderHeight + 4.0f);

  WriteSliderValues();
  SetDocument(Finish(pRoot));
}

void plUiSizeFitView::BuildTypeHeader(plUiElement* pRoot, float fY)
{
  using namespace plEditorUi;

  // the type with its icon on the raised surface, edged in the UI system colour
  auto* pHeader = Panel(pRoot);
  plGameUiEditor::Place(pHeader, plRectFloat(0.0f, fY, m_vBuiltSize.x, s_fHeaderHeight));
  pHeader->m_Color = Raised();

  auto* pEdge = Panel(pHeader);
  plGameUiEditor::Place(pEdge, plRectFloat(0.0f, 0.0f, 3.0f, s_fHeaderHeight));
  pEdge->m_Color = plGameUiEditor::UiColor();

  if (m_State.m_pType != nullptr)
  {
    auto* pIcon = SvgImage(pHeader, MonochromeIconPath(plGameUiEditor::TypeIconPath(m_State.m_pType)));
    plGameUiEditor::Place(pIcon, plRectFloat(12.0f, (s_fHeaderHeight - 15.0f) * 0.5f, 15.0f, 15.0f));
    pIcon->m_Color = plGameUiEditor::UiColor();
  }

  auto* pName = TextAt(pHeader, m_State.m_sTypeName, plRectFloat(34.0f, 0.0f, plMath::Max(m_vBuiltSize.x - 44.0f, 10.0f), s_fHeaderHeight), Foreground(), FontSize);
  pName->m_hFont = GetDefaultFont();
}

float plUiSizeFitView::BuildRows(plUiElement* pRoot, float fY)
{
  using namespace plEditorUi;

  auto& board = GetSurface()->GetBlackboard();
  const float fWidth = m_vBuiltSize.x;

  // the text the element shows
  {
    const plRectFloat value = Row(pRoot, fY, fWidth, plTranslate("GameUi.Fit.Text"));
    TextAt(pRoot, m_State.m_sText.IsEmpty() ? plStringView("-") : m_State.m_sText.GetView(), value, Muted());
    fY += s_fRowHeight;
  }

  // Size: Fixed | Fit content
  {
    const plRectFloat value = Row(pRoot, fY, fWidth, plTranslate("GameUi.Fit.Size"));
    plHybridArray<plString, 2> labels;
    labels.PushBack(plTranslate("GameUi.Fit.Fixed"));
    labels.PushBack(plTranslate("GameUi.Fit.FitContent"));
    const float fStripWidth = plMath::Min(plGameUiEditor::SegmentedWidth(labels), value.width);
    plGameUiEditor::Segmented(pRoot, plRectFloat(value.x, value.y + 1.0f, fStripWidth, value.height - 2.0f), s_szSizePrefix, labels, m_State.m_bFit ? 1 : 0);
    fY += s_fRowHeight;
  }

  // the width limits of Fit content, or the authored width
  if (m_State.m_bFit)
  {
    SliderRow(pRoot, Row(pRoot, fY, fWidth, plTranslate("GameUi.Fit.MinWidth")), s_szMinEntry, s_szMinText, s_fMinWidthLimit);
    fY += s_fRowHeight;
    SliderRow(pRoot, Row(pRoot, fY, fWidth, plTranslate("GameUi.Fit.MaxWidth")), s_szMaxEntry, s_szMaxText, s_fMaxWidthLimit);
    fY += s_fRowHeight;
  }
  else
  {
    const plRectFloat value = Row(pRoot, fY, fWidth, plTranslate("GameUi.Fit.Width"));
    plStringBuilder sWidth(FormatPx(m_State.m_fWidth), " px");
    auto* pWidth = TextAt(pRoot, sWidth, value, Foreground(), FontSize - 2, true);
    pWidth->m_HAlign = plUiTextHAlign::Right;
    fY += s_fRowHeight;
  }

  // the text's overflow mode
  if (m_State.m_iOverflow >= 0)
  {
    const plRectFloat value = Row(pRoot, fY, fWidth, plTranslate("GameUi.Fit.Overflow"));
    const plUInt32 uiMode = plMath::Min<plUInt32>(static_cast<plUInt32>(m_State.m_iOverflow), PL_ARRAY_SIZE(s_szOverflowModes) - 1);
    m_OverflowRect = plRectFloat(value.x, value.y, plMath::Min(value.width, 140.0f), value.height);
    plGameUiEditor::Dropdown(pRoot, m_OverflowRect, s_szOverflow, {}, {}, plTranslate(s_szOverflowModes[uiMode]));
    fY += s_fRowHeight;
  }

  // what the text needs against what it has, red when it does not fit
  if (m_State.m_bHasNeeds)
  {
    const plRectFloat value = Row(pRoot, fY, fWidth, plTranslate("GameUi.Fit.Needs"));
    const bool bOver = m_State.m_fNeeded > m_State.m_fAvailable + 0.5f;

    plStringBuilder sNeeds;
    sNeeds.SetFormat("{} / {} px", FormatPx(m_State.m_fNeeded), FormatPx(m_State.m_fAvailable));
    auto* pNeeds = TextAt(pRoot, sNeeds, value, bOver ? OverflowColor() : FitsColor(), FontSize - 2, true);
    pNeeds->m_HAlign = plUiTextHAlign::Right;
    fY += s_fRowHeight;
  }

  PL_IGNORE_UNUSED(board);
  return fY;
}

void plUiSizeFitView::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  for (plUInt32 i = 0; i < 2; ++i)
  {
    if (sId == plGameUiEditor::SegmentName(s_szSizePrefix, i).GetView())
    {
      if (m_OnSizeModeChanged.IsValid() && (i == 1) != m_State.m_bFit)
        m_OnSizeModeChanged(i == 1);
      return;
    }
  }

  if (sId == s_szOverflow)
    OpenOverflowMenu();
}

void plUiSizeFitView::OpenOverflowMenu()
{
  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_szOverflowModes); ++i)
  {
    auto& item = menu.AddAction(plTranslate(s_szOverflowModes[i]), i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == m_State.m_iOverflow;
  }

  menu.m_fMinWidth = m_OverflowRect.width;

  plEditorUiMenuView::Show(*GetSurface(), plVec2(m_OverflowRect.x, m_OverflowRect.y + m_OverflowRect.height), menu, [this](plUInt32 uiChoice)
    {
      if (m_OnOverflowChanged.IsValid())
        m_OnOverflowChanged(static_cast<plInt32>(uiChoice)); });
}

//////////////////////////////////////////////////////////////////////////
// plUiTextFitView

namespace
{
  plString IssueName(const char* szWhat, plUInt32 uiIndex)
  {
    plStringBuilder sName;
    sName.SetFormat("TextFit.{}.{}", szWhat, uiIndex);
    return sName;
  }

  plColorGammaUB SeverityColor(plUiTextFitView::Issue::Severity severity)
  {
    switch (severity)
    {
      case plUiTextFitView::Issue::Severity::Error:
        return OverflowColor();
      case plUiTextFitView::Issue::Severity::Warning:
        return plColorGammaUB(0xDC, 0xC3, 0x6E);
      default:
        return plEditorUi::Accent();
    }
  }
} // namespace

plUiTextFitView::plUiTextFitView()
  : plEditorUiContent("GameUi.TextFit")
{
}

void plUiTextFitView::SetIssues(plStringView sScope, plArrayPtr<const Issue> issues)
{
  bool bSame = sScope == m_sScope && issues.GetCount() == m_Issues.GetCount();
  for (plUInt32 i = 0; bSame && i < issues.GetCount(); ++i)
  {
    bSame = issues[i].m_sTitle == m_Issues[i].m_sTitle && issues[i].m_sDetail == m_Issues[i].m_sDetail && issues[i].m_bSelected == m_Issues[i].m_bSelected &&
            issues[i].m_Severity == m_Issues[i].m_Severity && issues[i].m_bAddCharacters == m_Issues[i].m_bAddCharacters;
  }

  if (bSame)
    return;

  m_sScope = sScope;
  m_Issues = issues;
  m_bRebuild = true;
}

void plUiTextFitView::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();
}

void plUiTextFitView::BuildDocument()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  // "TEXT FIT · 3 in Deutsch"
  plStringBuilder sSuffix;
  if (m_Issues.IsEmpty())
  {
    sSuffix.Set("\xC2\xB7 ", plTranslate("GameUi.TextFit.AllFit"));
  }
  else
  {
    plStringBuilder sFormat = plTranslate("GameUi.TextFit.InScope");
    sSuffix.SetFormat(sFormat.GetData(), m_Issues.GetCount(), m_sScope);
    sSuffix.Prepend("\xC2\xB7 ");
  }
  plGameUiEditor::SectionHeader(pRoot, plTranslate("GameUi.TextFit.Title"), sSuffix, plGameUiEditor::UiColor());

  if (m_Issues.IsEmpty())
  {
    plUiPanel* pEmpty = plGameUiEditor::EmptyState(pRoot, plGameUiEditor::IconPath("TextFit"), plTranslate("GameUi.TextFit.Empty"));
    Fill(pEmpty, plVec4(0, SectionHeaderHeight, 0, 0));
    SetDocument(Finish(pRoot));
    return;
  }

  plUiScrollView* pList = PL_DEFAULT_NEW(plUiScrollView);
  Fill(pList, plVec4(0, SectionHeaderHeight, 0, 0));
  pList->m_sName = "TextFit.Scroll";
  pList->m_fSpacing = 0.0f;
  pList->m_Color = Surface();
  pRoot->m_Children.PushBack(pList);

  for (plUInt32 i = 0; i < m_Issues.GetCount(); ++i)
    BuildRow(pList, i, m_Issues[i], m_vBuiltSize.x);

  SetDocument(Finish(pRoot));
}

void plUiTextFitView::BuildRow(plUiElement* pList, plUInt32 uiIndex, const Issue& issue, float fWidth)
{
  using namespace plEditorUi;

  auto* pRow = Panel(pList);
  pRow->m_vOffsets.w = s_fIssueHeight;
  pRow->m_Color = issue.m_bSelected ? Selection() : plColorGammaUB(0, 0, 0, 0);

  // the whole row selects the element
  auto* pHit = Button(pRow, plStringView(), IssueName("row", uiIndex));
  Fill(pHit);
  pHit->ClearChildren();
  pHit->m_hStyle = GetGhostStyle();

  auto* pDot = Panel(pRow);
  plGameUiEditor::Place(pDot, plRectFloat(s_fInset, (s_fIssueHeight - 9.0f) * 0.5f, 9.0f, 9.0f));
  pDot->m_Color = SeverityColor(issue.m_Severity);
  pDot->m_vCornerRadius = plVec4(4.5f);

  const float fButtonWidth = issue.m_bAddCharacters ? plGameUiEditor::TextButtonWidth(plTranslate("GameUi.TextFit.AddCharacters"), false) : 0.0f;
  const float fTextWidth = plMath::Max(fWidth - 2.0f * s_fInset - 17.0f - (fButtonWidth > 0.0f ? fButtonWidth + 8.0f : 0.0f) - ScrollbarGutter, 20.0f);

  auto* pTitle = TextAt(pRow, issue.m_sTitle, plRectFloat(s_fInset + 17.0f, 4.0f, fTextWidth, 16.0f), Foreground(), FontSize - 1);
  pTitle->m_Overflow = plUiTextOverflow::Ellipsis;

  auto* pDetail = TextAt(pRow, issue.m_sDetail, plRectFloat(s_fInset + 17.0f, 19.0f, fTextWidth, 15.0f), Muted(), FontSize - 2);
  pDetail->m_Overflow = plUiTextOverflow::Ellipsis;

  if (issue.m_bAddCharacters)
  {
    const float fX = fWidth - s_fInset - fButtonWidth - ScrollbarGutter;
    plGameUiEditor::TextButton(pRow, plRectFloat(fX, (s_fIssueHeight - 22.0f) * 0.5f, fButtonWidth, 22.0f), IssueName("add", uiIndex), plTranslate("GameUi.TextFit.AddCharacters"));
  }

  auto* pRule = Panel(pRow);
  pRule->m_vAnchorMin.y = 1;
  pRule->m_vOffsets = plVec4(0, -1, 0, 1);
  pRule->m_Color = plGameUiEditor::WithAlpha(Line(), 150);
}

void plUiTextFitView::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  for (plUInt32 i = 0; i < m_Issues.GetCount(); ++i)
  {
    if (sId == IssueName("add", i).GetView())
    {
      if (m_OnAddCharacters.IsValid())
        m_OnAddCharacters(i);
      return;
    }

    if (sId == IssueName("row", i).GetView())
    {
      if (m_OnIssueActivated.IsValid())
        m_OnIssueActivated(i);
      return;
    }
  }
}
