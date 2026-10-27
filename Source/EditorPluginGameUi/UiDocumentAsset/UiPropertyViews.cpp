#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiPropertyViews.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/ConversionUtils.h>

namespace
{
  constexpr float s_fPresetSize = 22.0f;
  constexpr float s_fPresetGap = 2.0f;
  constexpr float s_fGridSize = 4 * s_fPresetSize + 3 * s_fPresetGap;
  constexpr float s_fInset = 10.0f;
  constexpr float s_fEmptyHeight = 64.0f;
  constexpr float s_fHintHeight = 32.0f;

  const char* s_szFieldLetters[2][4] = {{"X", "Y", "W", "H"}, {"L", "T", "R", "B"}};

  plString FieldName(plUInt32 uiField)
  {
    plStringBuilder sName;
    sName.SetFormat("Rect.field.{}", uiField);
    return sName;
  }

  plString FieldText(plUInt32 uiField)
  {
    plStringBuilder sName;
    sName.SetFormat("Rect.text.{}", uiField);
    return sName;
  }

  plString PresetName(plInt32 iCol, plInt32 iRow)
  {
    plStringBuilder sName;
    sName.SetFormat("Rect.preset.{}.{}", iCol, iRow);
    return sName;
  }

  /// Whole numbers without decimals, others with one.
  plString FormatOffset(float fValue)
  {
    plStringBuilder sText;
    if (plMath::IsEqual(fValue, plMath::Round(fValue), 0.001f))
      sText.SetFormat("{}", static_cast<plInt32>(plMath::Round(fValue)));
    else
      sText.SetFormat("{}", plArgF(fValue, 1));
    return sText;
  }

  /// The preset column (or row) of an axis' anchors: 0..2 for a point at 0, 0.5 or 1, 3 for a full stretch, -1 else.
  plInt32 GetPresetIndex(float fMin, float fMax)
  {
    if (fMin == 0.0f && fMax == 1.0f)
      return 3;
    if (fMin != fMax)
      return -1;
    if (fMin == 0.0f)
      return 0;
    if (fMin == 0.5f)
      return 1;
    if (fMin == 1.0f)
      return 2;
    return -1;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plUiRectTransformView

plUiRectTransformView::plUiRectTransformView()
  : plEditorUiContent("GameUi.RectTransform")
{
}

float plUiRectTransformView::GetHeaderHeight() const
{
  return GetWindowHost() == nullptr ? plEditorUi::SectionHeaderHeight : 0.0f;
}

float plUiRectTransformView::GetHeight() const
{
  if (!m_State.m_bHasElement)
    return GetHeaderHeight() + s_fEmptyHeight;

  return GetHeaderHeight() + 8.0f + s_fGridSize + 8.0f + (m_State.m_bInContainer ? s_fHintHeight : 0.0f);
}

void plUiRectTransformView::SetState(const State& state)
{
  const bool bLayoutChanged = state.m_bHasElement != m_State.m_bHasElement || state.m_bInContainer != m_State.m_bInContainer;
  const bool bLooksChanged = bLayoutChanged || state.m_vAnchorMin != m_State.m_vAnchorMin || state.m_vAnchorMax != m_State.m_vAnchorMax || state.m_sSummary != m_State.m_sSummary;

  m_State = state;

  if (bLooksChanged)
    m_bRebuild = true;
  else
    WriteFieldTexts();
}

void plUiRectTransformView::WriteFieldTexts()
{
  if (!IsReady() || !m_State.m_bHasElement)
    return;

  // the field being typed in keeps its text
  const plStringView sFocused = GetSurface()->GetCanvas().GetFocusedElementName();
  for (plUInt32 i = 0; i < 4; ++i)
  {
    if (sFocused != FieldName(i).GetView())
      GetSurface()->GetBlackboard().SetEntryValue(FieldText(i), FormatOffset(m_State.m_vOffsets.GetData()[i]));
  }
}

void plUiRectTransformView::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();

  // a field commits when focus leaves it, as with Enter
  const plStringView sFocused = GetSurface()->GetCanvas().GetFocusedElementName();
  const bool bFocused = sFocused.StartsWith("Rect.field.");
  if (m_bFieldFocused && !bFocused)
    CommitFields();

  m_bFieldFocused = bFocused;
}

void plUiRectTransformView::BuildDocument()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();
  auto& board = GetSurface()->GetBlackboard();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  // the engine shell's panel tab names the section already
  if (GetHeaderHeight() > 0.0f)
    plGameUiEditor::SectionHeader(pRoot, plTranslate("GameUi.Section.RectTransform"), {}, plGameUiEditor::UiColor());

  if (!m_State.m_bHasElement)
  {
    plUiPanel* pEmpty = plGameUiEditor::EmptyState(pRoot, plGameUiEditor::IconPath("Pointer"), plTranslate("GameUi.RectTransform.NoSelection"));
    Fill(pEmpty, plVec4(0, GetHeaderHeight(), 0, 0));
    SetDocument(Finish(pRoot));
    return;
  }

  // children of stack and grid containers: the parent places them, so the section dims
  plUiPanel* pContent = Panel(pRoot);
  Fill(pContent, plVec4(0, GetHeaderHeight(), 0, 0));
  pContent->m_Color = plColorGammaUB(0, 0, 0, 0);
  pContent->m_fOpacity = m_State.m_bInContainer ? 0.55f : 1.0f;

  const float y0 = 8.0f;
  const plInt32 iCurrentCol = GetPresetIndex(m_State.m_vAnchorMin.x, m_State.m_vAnchorMax.x);
  const plInt32 iCurrentRow = GetPresetIndex(m_State.m_vAnchorMin.y, m_State.m_vAnchorMax.y);

  // the 4x4 presets: columns left / center / right / stretch, rows top / middle / bottom / stretch
  for (plInt32 iRow = 0; iRow < 4; ++iRow)
  {
    for (plInt32 iCol = 0; iCol < 4; ++iCol)
    {
      const bool bOn = iCol == iCurrentCol && iRow == iCurrentRow;
      const plString sName = PresetName(iCol, iRow);

      auto* pPreset = Button(pContent, plStringView(), sName);
      plGameUiEditor::Place(pPreset, plRectFloat(s_fInset + iCol * (s_fPresetSize + s_fPresetGap), y0 + iRow * (s_fPresetSize + s_fPresetGap), s_fPresetSize, s_fPresetSize));
      pPreset->m_Color = bOn ? Selection() : Field();
      pPreset->m_HoverColor = bOn ? Selection() : Raised();
      pPreset->m_PressedColor = Selection();
      pPreset->m_fBorderWidth = 1.0f;
      pPreset->m_BorderColor = bOn ? plGameUiEditor::Mix(Selection(), Accent(), 0.6f) : Line();
      pPreset->m_vCornerRadius = plVec4(3);
      pPreset->ClearChildren();

      // the marker shows where the element anchors in its parent
      const float fMarks[3] = {3.0f, 8.0f, 13.0f};
      auto* pMarker = Panel(pPreset);
      plGameUiEditor::Place(pMarker, plRectFloat(iCol == 3 ? 3.0f : fMarks[iCol] + 1.0f, iRow == 3 ? 3.0f : fMarks[iRow] + 1.0f, iCol == 3 ? 16.0f : 4.0f, iRow == 3 ? 16.0f : 4.0f));
      pMarker->m_Color = bOn ? Accent() : Muted();
      pMarker->m_vCornerRadius = plVec4(1);

      SetToolTip(board, sName, plTranslate("GameUi.AnchorPreset.Tooltip"));
    }
  }

  // the offsets, two per row: X Y / W H (L T / R B on stretched axes)
  const float x0 = s_fInset + s_fGridSize + 10.0f;
  const float fFieldWidth = plMath::Max((m_vBuiltSize.x - x0 - s_fInset - 4.0f) * 0.5f, 40.0f);
  const bool bStretchX = m_State.m_vAnchorMin.x != m_State.m_vAnchorMax.x;
  const bool bStretchY = m_State.m_vAnchorMin.y != m_State.m_vAnchorMax.y;

  for (plUInt32 i = 0; i < 4; ++i)
  {
    const bool bStretch = (i == 0 || i == 2) ? bStretchX : bStretchY;
    const plRectFloat rect(x0 + (i % 2) * (fFieldWidth + 4.0f), y0 + (i / 2) * (ControlHeight + 4.0f), fFieldWidth, ControlHeight);

    board.SetEntryValue(FieldText(i), FormatOffset(m_State.m_vOffsets.GetData()[i]));
    NumberField(pContent, rect, FieldName(i), FieldText(i), "Rect.submit", s_szFieldLetters[bStretch ? 1 : 0][i]);
  }

  auto* pSummary = Text(pContent, m_State.m_sSummary);
  plGameUiEditor::Place(pSummary, plRectFloat(x0, y0 + 2 * (ControlHeight + 4.0f), plMath::Max(m_vBuiltSize.x - x0 - s_fInset, 1.0f), 18.0f));
  pSummary->m_hFont = GetCodeFont();
  pSummary->m_uiFontSize = FontSize - 2;
  pSummary->m_Color = Dim();

  if (m_State.m_bInContainer)
  {
    auto* pHint = Text(pRoot, plTranslate("GameUi.Status.AnchorsInContainer"));
    plGameUiEditor::Place(pHint, plRectFloat(s_fInset, GetHeaderHeight() + y0 + s_fGridSize + 4.0f, plMath::Max(m_vBuiltSize.x - 2 * s_fInset, 1.0f), s_fHintHeight));
    pHint->m_uiFontSize = FontSize - 2;
    pHint->m_Color = Dim();
    pHint->m_bWrap = true;
    pHint->m_VAlign = plUiTextVAlign::Top;
  }

  SetDocument(Finish(pRoot));
}

void plUiRectTransformView::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  if (sId == "Rect.submit")
  {
    CommitFields();
    return;
  }

  for (plInt32 iRow = 0; iRow < 4; ++iRow)
  {
    for (plInt32 iCol = 0; iCol < 4; ++iCol)
    {
      if (sId == PresetName(iCol, iRow).GetView())
      {
        if (m_OnAnchorPreset.IsValid())
          m_OnAnchorPreset(static_cast<plInt8>(iCol), static_cast<plInt8>(iRow));
        return;
      }
    }
  }
}

void plUiRectTransformView::CommitFields()
{
  if (!m_State.m_bHasElement)
    return;

  const auto& board = GetSurface()->GetBlackboard();

  plVec4 vOffsets = m_State.m_vOffsets;
  for (plUInt32 i = 0; i < 4; ++i)
  {
    const plString sText = board.GetEntryValue(plTempHashedString(FieldText(i)), plString()).ConvertTo<plString>();

    double fValue = 0.0;
    if (plConversionUtils::StringToFloat(sText, fValue).Failed())
    {
      // an unreadable entry falls back to the current value
      WriteFieldTexts();
      return;
    }

    vOffsets.GetData()[i] = static_cast<float>(fValue);
  }

  if (vOffsets != m_State.m_vOffsets && m_OnOffsetsEdited.IsValid())
    m_OnOffsetsEdited(vOffsets);
}

//////////////////////////////////////////////////////////////////////////
// plUiEffectiveStyleView

plUiEffectiveStyleView::plUiEffectiveStyleView()
  : plEditorUiContent("GameUi.EffectiveStyle")
{
}

void plUiEffectiveStyleView::SetEmpty()
{
  if (m_bEmpty)
    return;

  m_bEmpty = true;
  m_Rows.Clear();
  m_bRebuild = true;
}

void plUiEffectiveStyleView::SetRows(plStringView sElementName, plArrayPtr<const Row> rows)
{
  m_bEmpty = false;
  m_sElementName = sElementName;
  m_Rows = rows;
  m_bRebuild = true;
}

void plUiEffectiveStyleView::OnUiUpdate()
{
  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();
}

void plUiEffectiveStyleView::BuildDocument()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  if (m_bEmpty)
  {
    plGameUiEditor::EmptyState(pRoot, plGameUiEditor::IconPath("Style"), plTranslate("GameUi.EffectiveStyle.NoSelection"));
    SetDocument(Finish(pRoot));
    return;
  }

  plStringBuilder sSuffix;
  sSuffix.SetFormat("\xC2\xB7 {}", m_sElementName);
  plGameUiEditor::SectionHeader(pRoot, plTranslate("GameUi.EffectiveStyle.Title"), sSuffix, plGameUiEditor::UiColor());

  const plGameUiEditor::Column columns[] = {
    {plTranslate("GameUi.EffectiveStyle.ColProperty"), 0.34f},
    {plTranslate("GameUi.EffectiveStyle.ColValue"), 0.36f},
    {plTranslate("GameUi.EffectiveStyle.ColSource"), 0.30f},
  };

  plHybridArray<float, 4> lefts, widths;
  plGameUiEditor::LayoutColumns(columns, m_vBuiltSize.x, lefts, widths);
  plGameUiEditor::TableHeader(pRoot, SectionHeaderHeight, columns, lefts, widths);

  plUiScrollView* pScroll = PL_DEFAULT_NEW(plUiScrollView);
  Fill(pScroll, plVec4(0, SectionHeaderHeight + plGameUiEditor::TableHeaderHeight, 0, 0));
  pScroll->m_sName = "EffectiveStyle.Scroll";
  pScroll->m_fSpacing = 0.0f;
  pScroll->m_Color = Surface();
  pRoot->m_Children.PushBack(pScroll);

  for (const Row& row : m_Rows)
  {
    auto* pRow = Panel(pScroll);
    pRow->m_vOffsets.w = RowHeight;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

    plGameUiEditor::CellText(pRow, plRectFloat(lefts[0], 0, widths[0], RowHeight), row.m_sProperty, Foreground());

    float fValueX = lefts[1];
    if (row.m_Swatch.a > 0)
    {
      auto* pSwatch = Panel(pRow);
      plGameUiEditor::Place(pSwatch, plRectFloat(lefts[1] + 8.0f, (RowHeight - 12.0f) * 0.5f, 12.0f, 12.0f));
      pSwatch->m_Color = plColorGammaUB(row.m_Swatch.r, row.m_Swatch.g, row.m_Swatch.b, 255);
      pSwatch->m_fBorderWidth = 1.0f;
      pSwatch->m_BorderColor = Border();
      pSwatch->m_vCornerRadius = plVec4(3);
      fValueX += 18.0f;
    }

    plGameUiEditor::CellText(pRow, plRectFloat(fValueX, 0, widths[1] - (fValueX - lefts[1]), RowHeight), row.m_sValue, Muted(), true);

    const plColorGammaUB sourceColor = row.m_Source == Row::Source::Style ? plGameUiEditor::UiColor() : (row.m_Source == Row::Source::State ? plGameUiEditor::ScriptingColor() : Dim());
    plGameUiEditor::CellText(pRow, plRectFloat(lefts[2], 0, widths[2], RowHeight), row.m_sSource, sourceColor)->m_uiFontSize = FontSize - 1;

    auto* pRule = Panel(pRow);
    pRule->m_vAnchorMin.y = 1;
    pRule->m_vOffsets = plVec4(0, -1, 0, 1);
    pRule->m_Color = plGameUiEditor::WithAlpha(Line(), 150);
  }

  SetDocument(Finish(pRoot));
}
