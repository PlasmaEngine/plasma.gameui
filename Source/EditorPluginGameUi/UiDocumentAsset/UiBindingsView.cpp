#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiBindingsView.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorUi/Content.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GuiFoundation/EngineUi/EnginePrompt.h>
#include <GuiFoundation/EngineUi/EngineUiRenderer.h>

namespace
{
  constexpr float s_fSliderColumn = 100.0f;

  plString Name(const char* szFormat, plInt32 iIndex)
  {
    plStringBuilder sName;
    sName.SetFormat(szFormat, iIndex);
    return sName;
  }

  /// A number with up to four significant digits and no trailing zeros.
  plString FormatValue(float fValue)
  {
    const float fAbs = plMath::Abs(fValue);
    const plUInt32 uiDecimals = fAbs >= 1000.0f ? 0 : (fAbs >= 100.0f ? 1 : (fAbs >= 10.0f ? 2 : (fAbs >= 1.0f ? 3 : 4)));

    plStringBuilder sText;
    sText.SetFormat("{}", plArgF(fValue, uiDecimals));

    if (uiDecimals > 0)
    {
      while (sText.EndsWith("0"))
        sText.Shrink(0, 1);
      if (sText.EndsWith("."))
        sText.Shrink(0, 1);
    }

    if (sText == "-0")
      sText = "0";

    return sText;
  }

  bool ParseNumber(plStringView sText, float& out_fValue)
  {
    if (sText.IsEmpty())
      return false;

    // the whole text must be the number
    double fValue = 0.0;
    const char* szEnd = nullptr;
    if (plConversionUtils::StringToFloat(sText, fValue, &szEnd).Failed() || szEnd != sText.GetEndPointer())
      return false;

    out_fValue = static_cast<float>(fValue);
    return true;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plUiBindingsPanelBase

plString plUiBindingsPanelBase::AskScenarioName()
{
  // OK stays disabled until the name has more than spaces
  auto validator = [](plStringView sText)
  {
    plStringBuilder sTrimmed(sText);
    sTrimmed.Trim(" \t");
    return sTrimmed.IsEmpty() ? plEnginePrompt::QuietInvalid() : plString();
  };

  bool bOk = false;
  plStringBuilder sName = plEnginePrompt::GetText(plTranslate("GameUi.Bindings.ScenarioSaveTitle"), plTranslate("GameUi.Bindings.ScenarioSaveLabel"), {}, &bOk, validator);
  sName.Trim(" \t");

  return bOk ? plString(sName) : plString();
}

void plUiBindingsPanelBase::MockDataChanged()
{
  if (m_OnMockDataChanged.IsValid())
    m_OnMockDataChanged();
}

void plUiBindingsPanelBase::AddBindingRequested()
{
  if (m_OnAddBindingRequested.IsValid())
    m_OnAddBindingRequested();
}

void plUiBindingsPanelBase::RemoveBindingRequested(const plUuid& elementGuid, plStringView sBindingProperty)
{
  if (m_OnRemoveBindingRequested.IsValid())
    m_OnRemoveBindingRequested(elementGuid, sBindingProperty);
}

void plUiBindingsPanelBase::BindingEntryEdited(const plUuid& elementGuid, plStringView sBindingProperty, plStringView sNewEntry)
{
  if (m_OnBindingEntryEdited.IsValid())
    m_OnBindingEntryEdited(elementGuid, sBindingProperty, sNewEntry);
}

void plUiBindingsPanelBase::BindingRowActivated(const plUuid& elementGuid)
{
  if (m_OnBindingRowActivated.IsValid())
    m_OnBindingRowActivated(elementGuid);
}

void plUiBindingsPanelBase::ScenarioSaveRequested(plStringView sName)
{
  if (m_OnScenarioSaveRequested.IsValid())
    m_OnScenarioSaveRequested(sName);
}

void plUiBindingsPanelBase::ScenarioDeleteRequested(plStringView sName)
{
  if (m_OnScenarioDeleteRequested.IsValid())
    m_OnScenarioDeleteRequested(sName);
}

void plUiBindingsPanelBase::ScenarioSelected(plStringView sName)
{
  if (m_OnScenarioSelected.IsValid())
    m_OnScenarioSelected(sName);
}

//////////////////////////////////////////////////////////////////////////
// plUiBindingsSurface

/// The engine-drawn content of plUiBindingsView, which the document window shows as a panel.
class plUiBindingsSurface : public plEditorUiContent
{
public:
  enum class Edit : plUInt8
  {
    None,
    BindingEntry,
    MockName,
    MockValue,
  };

  explicit plUiBindingsSurface(plUiBindingsView* pView)
    : plEditorUiContent("GameUi.Bindings")
    , m_pView(pView)
  {
    if (!IsReady())
      return;

    m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plUiBindingsSurface::OnPointer, this));
    m_KeySubscription = GetSurface()->m_KeyEvents.AddEventHandler(plMakeDelegate(&plUiBindingsSurface::OnKey, this));
  }

  ~plUiBindingsSurface()
  {
    // the view keeps its state without a content to draw it
    m_pView->m_pSurface = nullptr;

    if (!IsReady())
      return;

    GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
    GetSurface()->m_KeyEvents.RemoveEventHandler(m_KeySubscription);
  }

  void Rebuild() { m_bRebuild = true; }

  /// The selected rows stay in range when rows go away.
  void ClampSelection()
  {
    if (m_iBinding >= static_cast<plInt32>(m_pView->m_Bindings.GetCount()))
      m_iBinding = -1;
    if (m_iMock >= static_cast<plInt32>(m_pView->m_MockRows.GetCount()))
      m_iMock = -1;
  }

  /// A mock value changed outside the panel: its text and slider follow without a rebuild.
  void ShowMockValue(plInt32 iRow)
  {
    if (!IsReady() || m_bRebuild || iRow < 0)
      return;

    const auto& row = m_pView->m_MockRows[iRow];
    auto& board = GetSurface()->GetBlackboard();
    board.SetEntryValue(Name("Mock.value.{}", iRow), row.m_sValue);

    float fValue = 0.0f;
    if (ParseNumber(row.m_sValue, fValue))
    {
      board.SetEntryValue(Name("Mock.sliderValue.{}", iRow), fValue);
      m_SliderValues.EnsureCount(iRow + 1);
      m_SliderValues[iRow] = fValue;
    }
  }

protected:
  void OnUiUpdate() override
  {
    if (!IsReady())
      return;

    if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
      BuildDocument();

    PollSliders();

    // an edit commits when its field loses the keyboard, as with Enter
    if (m_Edit != Edit::None)
    {
      const bool bFocused = GetSurface()->GetCanvas().GetFocusedElementName() == GetEditFieldName();
      if (m_bEditFocused && !bFocused)
        CommitEdit();
      else
        m_bEditFocused = bFocused;
    }
  }

  void OnUiEvent(const plMsgUiEvent& event) override
  {
    const plStringView sId = event.m_sIdentifier.GetView();

    if (sId == "Bind.commit" || sId == "Mock.commit")
    {
      CommitEdit();
      return;
    }

    if (sId == "Bind.add")
    {
      // the dialog opens after the UI update: its engine-drawn form needs the UI frames
      m_Timers.Start(plTime::MakeZero(), [this]()
        { m_pView->AddBindingRequested(); });
    }
    else if (sId == "Bind.remove")
    {
      if (m_iBinding >= 0)
      {
        const auto& binding = m_pView->m_Bindings[m_iBinding];
        if (binding.m_ElementGuid.IsValid() && !binding.m_sBindingProperty.IsEmpty())
          m_pView->RemoveBindingRequested(binding.m_ElementGuid, binding.m_sBindingProperty);
      }
    }
    else if (sId == "Mock.add")
    {
      m_pView->m_MockRows.ExpandAndGetRef();
      m_iMock = static_cast<plInt32>(m_pView->m_MockRows.GetCount()) - 1;
      m_iFocusTable = 1;
      BuildDocument();
      BeginEdit(Edit::MockName, m_iMock);
    }
    else if (sId == "Mock.remove")
    {
      if (m_iMock >= 0)
      {
        m_pView->m_MockRows.RemoveAtAndCopy(m_iMock);
        m_iMock = -1;
        m_bRebuild = true;
        m_pView->MockDataChanged();
      }
    }
    else if (sId == "Mock.reset" || sId == "Mock.chip.default")
    {
      m_pView->m_sCurrentScenario.Clear();
      m_pView->ResetValues();
      m_bRebuild = true;
    }
    else if (sId == "Mock.chip.save")
    {
      m_Timers.Start(plTime::MakeZero(), [this]()
        { SaveScenario(); });
    }
    else
    {
      for (plUInt32 i = 0; i < m_pView->m_Scenarios.GetCount(); ++i)
      {
        if (sId == Name("Mock.chip.{}", i).GetView())
        {
          m_pView->m_sCurrentScenario = m_pView->m_Scenarios[i];
          m_bRebuild = true;
          m_pView->ScenarioSelected(m_pView->m_Scenarios[i]);
          return;
        }
      }
    }
  }

private:
  struct Layout
  {
    float m_fLeftWidth = 0.0f;
    float m_fRightX = 0.0f;
    float m_fRightWidth = 0.0f;
    plHybridArray<float, 4> m_BindLefts, m_BindWidths;
    plHybridArray<float, 4> m_MockLefts, m_MockWidths;
  };

  void ComputeLayout(Layout& out_layout) const
  {
    const plVec2 vSize = GetSurface()->GetLogicalSize();
    out_layout.m_fLeftWidth = plMath::Floor(vSize.x * 0.5f);
    out_layout.m_fRightX = out_layout.m_fLeftWidth + 1.0f;
    out_layout.m_fRightWidth = plMath::Max(vSize.x - out_layout.m_fRightX, 1.0f);

    plGameUiEditor::LayoutColumns(GetBindColumns(), out_layout.m_fLeftWidth, out_layout.m_BindLefts, out_layout.m_BindWidths);
    plGameUiEditor::LayoutColumns(GetMockColumns(), out_layout.m_fRightWidth, out_layout.m_MockLefts, out_layout.m_MockWidths);
  }

  static plArrayPtr<const plGameUiEditor::Column> GetBindColumns()
  {
    static plGameUiEditor::Column s_Columns[3];
    s_Columns[0] = {plTranslate("GameUi.Bindings.ColElement"), 0.34f};
    s_Columns[1] = {plTranslate("GameUi.Bindings.ColProperty"), 0.30f};
    s_Columns[2] = {plTranslate("GameUi.Bindings.ColEntry"), 0.36f};
    return s_Columns;
  }

  static plArrayPtr<const plGameUiEditor::Column> GetMockColumns()
  {
    static plGameUiEditor::Column s_Columns[3];
    s_Columns[0] = {plTranslate("GameUi.Bindings.ColEntry"), 0.45f};
    s_Columns[1] = {plTranslate("GameUi.Bindings.ColValue"), 0.55f};
    s_Columns[2] = {plStringView(), s_fSliderColumn};
    return s_Columns;
  }

  plColorGammaUB GetHoverColor() const { return plGameUiEditor::Mix(plEditorUi::Surface(), plEditorUi::Raised(), 0.5f); }

  /// A table row's hit target, selection fill and hairline; the content goes on top.
  plUiPanel* AddRow(plUiElement* pParent, plStringView sName, plStringView sSelectedBinding, plStringView sTooltip)
  {
    using namespace plEditorUi;

    auto* pRow = Panel(pParent);
    pRow->m_vOffsets.w = RowHeight;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

    auto* pCatcher = Button(pRow, plStringView(), sName);
    pCatcher->m_sClickEvent.Clear();
    pCatcher->ClearChildren();
    Fill(pCatcher);
    pCatcher->m_Color = plColorGammaUB(0, 0, 0, 0);
    pCatcher->m_HoverColor = GetHoverColor();
    pCatcher->m_PressedColor = GetHoverColor();
    pCatcher->m_vCornerRadius = plVec4::MakeZero();
    SetToolTip(GetSurface()->GetBlackboard(), sName, sTooltip);

    auto* pSelected = Panel(pRow);
    pSelected->m_Color = Selection();
    pSelected->m_sVisibleBinding = sSelectedBinding;

    auto* pRule = Panel(pRow);
    pRule->m_vAnchorMin.y = 1;
    pRule->m_vOffsets = plVec4(0, -1, 0, 1);
    pRule->m_Color = plGameUiEditor::WithAlpha(Line(), 150);
    return pRow;
  }

  /// An in-place editor over a cell, shown while editing that cell.
  void AddCellEditor(plUiElement* pRow, const plRectFloat& rect, plStringView sName, plStringView sVisibleBinding, plStringView sTextBinding, plStringView sSubmit, bool bMono)
  {
    using namespace plEditorUi;

    auto* pInput = Input(pRow, sName, sTextBinding, plStringView(), sSubmit);
    plGameUiEditor::Place(pInput, plRectFloat(rect.x + 2.0f, 2.0f, plMath::Max(rect.width - 4.0f, 8.0f), RowHeight - 4.0f));
    pInput->m_BorderColor = Accent();
    pInput->m_vCornerRadius = plVec4(3);
    pInput->m_sVisibleBinding = sVisibleBinding;
    if (bMono)
    {
      pInput->m_hFont = GetCodeFont();
      pInput->m_uiFontSize = FontSize - 2;
    }
  }

  void BuildDocument()
  {
    using namespace plEditorUi;

    m_bRebuild = false;
    m_vBuiltSize = GetSurface()->GetLogicalSize();
    ClampSelection();

    auto& board = GetSurface()->GetBlackboard();
    Layout layout;
    ComputeLayout(layout);

    plUiPanel* pRoot = Panel();
    pRoot->m_Color = Surface();

    auto* pDivider = Panel(pRoot);
    plGameUiEditor::Place(pDivider, plRectFloat(layout.m_fLeftWidth, 0, 1.0f, m_vBuiltSize.y));
    pDivider->m_Color = Line();

    BuildBindings(pRoot, layout);
    BuildMock(pRoot, layout);

    // the selection and edit states live in the board, so clicks need no rebuild
    m_SliderValues.Clear();
    for (plUInt32 i = 0; i < m_pView->m_MockRows.GetCount(); ++i)
    {
      float fValue = 0.0f;
      m_SliderValues.PushBack(ParseNumber(m_pView->m_MockRows[i].m_sValue, fValue) ? fValue : 0.0f);
    }

    SetDocument(Finish(pRoot));
    WriteSelection();

    for (plUInt32 i = 0; i < m_pView->m_Bindings.GetCount(); ++i)
      board.SetEntryValue(Name("Bind.editing.{}", i), m_Edit == Edit::BindingEntry && m_iEditRow == static_cast<plInt32>(i));
    for (plUInt32 i = 0; i < m_pView->m_MockRows.GetCount(); ++i)
    {
      board.SetEntryValue(Name("Mock.editingName.{}", i), m_Edit == Edit::MockName && m_iEditRow == static_cast<plInt32>(i));
      board.SetEntryValue(Name("Mock.editingValue.{}", i), m_Edit == Edit::MockValue && m_iEditRow == static_cast<plInt32>(i));
    }
  }

  void BuildBindings(plUiPanel* pRoot, const Layout& layout)
  {
    using namespace plEditorUi;

    auto* pPane = Panel(pRoot);
    plGameUiEditor::Place(pPane, plRectFloat(0, 0, layout.m_fLeftWidth, m_vBuiltSize.y));
    pPane->m_Color = Surface();

    plGameUiEditor::SectionHeader(pPane, plTranslate("GameUi.Section.Bindings"), {}, plGameUiEditor::XrColor());
    plGameUiEditor::TableHeader(pPane, SectionHeaderHeight, GetBindColumns(), layout.m_BindLefts, layout.m_BindWidths);

    plUiScrollView* pScroll = PL_DEFAULT_NEW(plUiScrollView);
    Fill(pScroll, plVec4(0, SectionHeaderHeight + plGameUiEditor::TableHeaderHeight, 0, plGameUiEditor::ToolRowHeight));
    pScroll->m_sName = "Bind.Scroll";
    pScroll->m_fSpacing = 0.0f;
    pScroll->m_Color = Surface();
    pPane->m_Children.PushBack(pScroll);

    const bool bHasTemplate = !m_pView->m_TemplateEntries.IsEmpty();
    const plString sNotInTemplate = plTranslate("GameUi.Bindings.NotInTemplateTooltip");

    for (plUInt32 i = 0; i < m_pView->m_Bindings.GetCount(); ++i)
    {
      const auto& binding = m_pView->m_Bindings[i];
      const bool bKnown = !bHasTemplate || m_pView->IsTemplateEntry(binding.m_sEntry);

      plUiPanel* pRow = AddRow(pScroll, Name("Bind.row.{}", i), Name("Bind.sel.{}", i), bKnown ? plStringView() : sNotInTemplate.GetView());

      plGameUiEditor::CellText(pRow, plRectFloat(layout.m_BindLefts[0], 0, layout.m_BindWidths[0], RowHeight), binding.m_sElement, Foreground());
      plGameUiEditor::CellText(pRow, plRectFloat(layout.m_BindLefts[1], 0, layout.m_BindWidths[1], RowHeight), binding.m_sProperty, Dim());
      plGameUiEditor::CellText(pRow, plRectFloat(layout.m_BindLefts[2], 0, layout.m_BindWidths[2], RowHeight), binding.m_sEntry, bKnown ? plGameUiEditor::XrColor() : Warning(), true);

      if (!binding.m_sBindingProperty.IsEmpty())
        AddCellEditor(pRow, plRectFloat(layout.m_BindLefts[2], 0, layout.m_BindWidths[2], RowHeight), Name("Bind.edit.{}", i), Name("Bind.editing.{}", i), "Bind.editText", "Bind.commit", true);
    }

    if (m_pView->m_Bindings.IsEmpty())
    {
      plUiPanel* pEmpty = plGameUiEditor::EmptyState(pPane, plGameUiEditor::IconPath("Link"), plTranslate("GameUi.Bindings.Empty"), plTranslate("GameUi.Bindings.EmptyLine"));
      Fill(pEmpty, plVec4(0, SectionHeaderHeight + plGameUiEditor::TableHeaderHeight, 0, plGameUiEditor::ToolRowHeight));
    }

    plUiPanel* pTools = plGameUiEditor::ToolRow(pPane, 0.0f, true);
    pTools->m_vAnchorMin.y = pTools->m_vAnchorMax.y = 1.0f;
    pTools->m_vOffsets.y = -plGameUiEditor::ToolRowHeight;

    const plStringView sAdd = plTranslate("GameUi.Bindings.AddBinding");
    const plStringView sRemove = plTranslate("GameUi.Bindings.RemoveBinding");
    const float fAdd = plGameUiEditor::TextButtonWidth(sAdd, true);
    const float y = (plGameUiEditor::ToolRowHeight - ControlHeight) * 0.5f;
    plGameUiEditor::TextButton(pTools, plRectFloat(6.0f, y, fAdd, ControlHeight), "Bind.add", sAdd, IconPath(Icon::Add));
    plGameUiEditor::TextButton(pTools, plRectFloat(12.0f + fAdd, y, plGameUiEditor::TextButtonWidth(sRemove, true), ControlHeight), "Bind.remove", sRemove, plGameUiEditor::IconPath("Minus"), "Bind.noSelection");
  }

  void BuildMock(plUiPanel* pRoot, const Layout& layout)
  {
    using namespace plEditorUi;

    auto& board = GetSurface()->GetBlackboard();

    auto* pPane = Panel(pRoot);
    plGameUiEditor::Place(pPane, plRectFloat(layout.m_fRightX, 0, layout.m_fRightWidth, m_vBuiltSize.y));
    pPane->m_Color = Surface();

    plGameUiEditor::SectionHeader(pPane, plTranslate("GameUi.Section.MockData"), {}, Accent());

    // the scenario chips: Default, the saved scenarios, + Save...
    {
      plUiPanel* pChips = plGameUiEditor::ToolRow(pPane, SectionHeaderHeight);
      const float y = (plGameUiEditor::ToolRowHeight - RowHeight) * 0.5f;
      float x = 6.0f;

      auto addChip = [&](plStringView sName, plStringView sText, bool bOn, const char* szTooltipKey)
      {
        const float w = plGameUiEditor::ChipWidth(sText);
        plGameUiEditor::Chip(pChips, plRectFloat(x, y, w, RowHeight), sName, sText, bOn);
        SetToolTip(board, sName, plTranslate(szTooltipKey));
        x += w + 4.0f;
      };

      addChip("Mock.chip.default", plTranslate("GameUi.Bindings.ScenarioDefault"), m_pView->m_sCurrentScenario.IsEmpty(), "GameUi.Bindings.ScenarioDefaultTooltip");
      for (plUInt32 i = 0; i < m_pView->m_Scenarios.GetCount(); ++i)
        addChip(Name("Mock.chip.{}", i), m_pView->m_Scenarios[i], m_pView->m_Scenarios[i] == m_pView->m_sCurrentScenario, "GameUi.Bindings.ScenarioTooltip");
      addChip("Mock.chip.save", plTranslate("GameUi.Bindings.ScenarioSave"), false, "GameUi.Bindings.ScenarioSaveTooltip");
    }

    const float fTableY = SectionHeaderHeight + plGameUiEditor::ToolRowHeight;
    plGameUiEditor::TableHeader(pPane, fTableY, GetMockColumns(), layout.m_MockLefts, layout.m_MockWidths);

    plUiScrollView* pScroll = PL_DEFAULT_NEW(plUiScrollView);
    Fill(pScroll, plVec4(0, fTableY + plGameUiEditor::TableHeaderHeight, 0, plGameUiEditor::ToolRowHeight));
    pScroll->m_sName = "Mock.Scroll";
    pScroll->m_fSpacing = 0.0f;
    pScroll->m_Color = Surface();
    pPane->m_Children.PushBack(pScroll);

    const bool bHasTemplate = !m_pView->m_TemplateEntries.IsEmpty();
    const plString sNotInTemplate = plTranslate("GameUi.Bindings.NotInTemplateTooltip");
    const plString sMockTooltip = plTranslate("GameUi.Bindings.MockTooltip");

    for (plUInt32 i = 0; i < m_pView->m_MockRows.GetCount(); ++i)
    {
      const auto& row = m_pView->m_MockRows[i];
      const bool bInTemplate = !bHasTemplate || m_pView->IsTemplateEntry(row.m_sName);

      plUiPanel* pRow = AddRow(pScroll, Name("Mock.row.{}", i), Name("Mock.sel.{}", i), bInTemplate ? sMockTooltip.GetView() : sNotInTemplate.GetView());

      const plRectFloat nameRect(layout.m_MockLefts[0], 0, layout.m_MockWidths[0], RowHeight);
      const plRectFloat valueRect(layout.m_MockLefts[1], 0, layout.m_MockWidths[1], RowHeight);

      plGameUiEditor::CellText(pRow, nameRect, row.m_sName, bInTemplate ? plGameUiEditor::XrColor() : Dim(), true);

      board.SetEntryValue(Name("Mock.value.{}", i), row.m_sValue);
      plGameUiEditor::CellText(pRow, valueRect, row.m_sValue, bInTemplate ? Muted() : Dim(), true, Name("Mock.value.{}", i));

      AddCellEditor(pRow, nameRect, Name("Mock.editName.{}", i), Name("Mock.editingName.{}", i), "Mock.editText", "Mock.commit", true);
      AddCellEditor(pRow, valueRect, Name("Mock.editValue.{}", i), Name("Mock.editingValue.{}", i), "Mock.editText", "Mock.commit", true);

      // numbers get a slider over a range that grows to include the value
      float fValue = 0.0f;
      if (ParseNumber(row.m_sValue, fValue))
      {
        board.SetEntryValue(Name("Mock.sliderValue.{}", i), fValue);

        plUiSlider* pSlider = PL_DEFAULT_NEW(plUiSlider);
        pRow->m_Children.PushBack(pSlider);
        pSlider->m_sName = Name("Mock.slider.{}", i);
        plGameUiEditor::Place(pSlider, plRectFloat(layout.m_MockLefts[2] + 5.0f, RowHeight * 0.5f - 2.0f, s_fSliderColumn - 15.0f, 4.0f));
        pSlider->m_fMin = row.m_fMin;
        pSlider->m_fMax = row.m_fMax;
        pSlider->m_sValueBinding = Name("Mock.sliderValue.{}", i);
        pSlider->m_Color = Raised();
        pSlider->m_FillColor = Border();
        pSlider->m_HandleColor = Muted();
        pSlider->m_vCornerRadius = plVec4(2);
      }
    }

    if (m_pView->m_MockRows.IsEmpty())
    {
      plUiPanel* pEmpty = plGameUiEditor::EmptyState(pPane, plEditorUi::IconPath(Icon::List), plTranslate("GameUi.Bindings.MockEmpty"), plTranslate("GameUi.Bindings.MockEmptyLine"));
      Fill(pEmpty, plVec4(0, fTableY + plGameUiEditor::TableHeaderHeight, 0, plGameUiEditor::ToolRowHeight));
    }

    plUiPanel* pTools = plGameUiEditor::ToolRow(pPane, 0.0f, true);
    pTools->m_vAnchorMin.y = pTools->m_vAnchorMax.y = 1.0f;
    pTools->m_vOffsets.y = -plGameUiEditor::ToolRowHeight;

    const plStringView sAdd = plTranslate("GameUi.Bindings.AddEntry");
    const plStringView sRemove = plTranslate("GameUi.Bindings.RemoveEntry");
    const plStringView sReset = plTranslate("GameUi.Bindings.ResetValues");
    const float fAdd = plGameUiEditor::TextButtonWidth(sAdd, true);
    const float fReset = plGameUiEditor::TextButtonWidth(sReset, true);
    const float y = (plGameUiEditor::ToolRowHeight - ControlHeight) * 0.5f;

    plGameUiEditor::TextButton(pTools, plRectFloat(6.0f, y, fAdd, ControlHeight), "Mock.add", sAdd, IconPath(Icon::Add));
    plGameUiEditor::TextButton(pTools, plRectFloat(12.0f + fAdd, y, plGameUiEditor::TextButtonWidth(sRemove, true), ControlHeight), "Mock.remove", sRemove, plGameUiEditor::IconPath("Minus"), "Mock.noSelection");
    plGameUiEditor::TextButton(pTools, plRectFloat(layout.m_fRightWidth - 6.0f - fReset, y, fReset, ControlHeight), "Mock.reset", sReset, IconPath(Icon::Reset));
    SetToolTip(board, "Mock.reset", plTranslate("GameUi.Bindings.ResetValuesTooltip"));
  }

  void WriteSelection()
  {
    auto& board = GetSurface()->GetBlackboard();
    for (plUInt32 i = 0; i < m_pView->m_Bindings.GetCount(); ++i)
      board.SetEntryValue(Name("Bind.sel.{}", i), m_iBinding == static_cast<plInt32>(i));
    for (plUInt32 i = 0; i < m_pView->m_MockRows.GetCount(); ++i)
      board.SetEntryValue(Name("Mock.sel.{}", i), m_iMock == static_cast<plInt32>(i));

    // Remove Binding and Remove Entry need a row
    const bool bRemovableBinding = m_iBinding >= 0 && !m_pView->m_Bindings[m_iBinding].m_sBindingProperty.IsEmpty();
    board.SetEntryValue("Bind.noSelection", !bRemovableBinding);
    board.SetEntryValue("Mock.noSelection", m_iMock < 0);
  }

  plInt32 GetRowIndex(plStringView sElement, plStringView sPrefix) const
  {
    if (!sElement.StartsWith(sPrefix))
      return -1;

    plUInt32 uiIndex = 0;
    if (plConversionUtils::StringToUInt(plStringView(sElement.GetStartPointer() + sPrefix.GetElementCount(), sElement.GetEndPointer()), uiIndex).Failed())
      return -1;

    return static_cast<plInt32>(uiIndex);
  }

  void OnPointer(const plEditorUiPointerEvent& event)
  {
    using Type = plEditorUiPointerEvent::Type;
    using Button = plEditorUiPointerEvent::Button;

    if (event.m_Type != Type::Press && event.m_Type != Type::DoubleClick)
      return;

    Layout layout;
    ComputeLayout(layout);

    // a scenario chip's menu
    if (event.m_Type == Type::Press && event.m_Button == Button::Right)
    {
      for (plUInt32 i = 0; i < m_pView->m_Scenarios.GetCount(); ++i)
      {
        if (event.m_sElement == Name("Mock.chip.{}", i).GetView())
        {
          ShowScenarioMenu(i, event.m_vPosition);
          return;
        }
      }
    }

    if (event.m_Button != Button::Left)
      return;

    const plInt32 iBinding = GetRowIndex(event.m_sElement, "Bind.row.");
    const plInt32 iMock = GetRowIndex(event.m_sElement, "Mock.row.");

    if (iBinding >= 0 && iBinding < static_cast<plInt32>(m_pView->m_Bindings.GetCount()))
    {
      m_iFocusTable = 0;
      m_iBinding = iBinding;
      WriteSelection();

      if (event.m_Type == Type::DoubleClick)
      {
        // the entry cell edits in place; the element and property select the element
        const auto& binding = m_pView->m_Bindings[iBinding];
        if (event.m_vPosition.x >= layout.m_BindLefts[2] && !binding.m_sBindingProperty.IsEmpty())
          BeginEdit(Edit::BindingEntry, iBinding);
        else
          m_pView->BindingRowActivated(binding.m_ElementGuid);
      }
      return;
    }

    if (iMock >= 0 && iMock < static_cast<plInt32>(m_pView->m_MockRows.GetCount()))
    {
      m_iFocusTable = 1;
      m_iMock = iMock;
      WriteSelection();

      if (event.m_Type == Type::DoubleClick)
      {
        const float x = event.m_vPosition.x - layout.m_fRightX;
        if (x < layout.m_MockLefts[1])
          BeginEdit(Edit::MockName, iMock);
        else if (x < layout.m_MockLefts[2])
          BeginEdit(Edit::MockValue, iMock);
      }
    }
  }

  void OnKey(const plEditorUiKeyEvent& event)
  {
    if (event.m_bHandled)
      return;

    if (m_Edit != Edit::None)
    {
      if (event.m_Key == plEditorUiKeyEvent::Key::Escape)
      {
        EndEdit();
        event.m_bHandled = true;
      }
      return;
    }

    if (event.m_Key != plEditorUiKeyEvent::Key::F2)
      return;

    if (m_iFocusTable == 0 && m_iBinding >= 0 && !m_pView->m_Bindings[m_iBinding].m_sBindingProperty.IsEmpty())
    {
      BeginEdit(Edit::BindingEntry, m_iBinding);
      event.m_bHandled = true;
    }
    else if (m_iFocusTable == 1 && m_iMock >= 0)
    {
      BeginEdit(Edit::MockValue, m_iMock);
      event.m_bHandled = true;
    }
  }

  plString GetEditFieldName() const
  {
    switch (m_Edit)
    {
      case Edit::BindingEntry:
        return Name("Bind.edit.{}", m_iEditRow);
      case Edit::MockName:
        return Name("Mock.editName.{}", m_iEditRow);
      case Edit::MockValue:
        return Name("Mock.editValue.{}", m_iEditRow);
      default:
        return {};
    }
  }

  plString GetEditVisibleBinding() const
  {
    switch (m_Edit)
    {
      case Edit::BindingEntry:
        return Name("Bind.editing.{}", m_iEditRow);
      case Edit::MockName:
        return Name("Mock.editingName.{}", m_iEditRow);
      case Edit::MockValue:
        return Name("Mock.editingValue.{}", m_iEditRow);
      default:
        return {};
    }
  }

  const char* GetEditTextBinding() const { return m_Edit == Edit::BindingEntry ? "Bind.editText" : "Mock.editText"; }

  void BeginEdit(Edit edit, plInt32 iRow)
  {
    if (m_Edit != Edit::None)
      CommitEdit();

    plString sText;
    if (edit == Edit::BindingEntry)
      sText = m_pView->m_Bindings[iRow].m_sEntry;
    else if (edit == Edit::MockName)
      sText = m_pView->m_MockRows[iRow].m_sName;
    else
      sText = m_pView->m_MockRows[iRow].m_sValue;

    m_Edit = edit;
    m_iEditRow = iRow;
    m_bEditFocused = false;

    auto& board = GetSurface()->GetBlackboard();
    board.SetEntryValue(GetEditTextBinding(), sText);
    board.SetEntryValue(GetEditVisibleBinding(), true);

    GetSurface()->Update();
    GetSurface()->GetCanvas().FocusElement(GetEditFieldName(), GetSurface()->GetLogicalSize());
    GetSurface()->GetCanvas().SetTextBinding(GetEditTextBinding(), sText);
  }

  void CommitEdit()
  {
    if (m_Edit == Edit::None)
      return;

    const Edit edit = m_Edit;
    const plInt32 iRow = m_iEditRow;
    const plString sText = GetSurface()->GetBlackboard().GetEntryValue(plTempHashedString(GetEditTextBinding()), plString()).ConvertTo<plString>();
    EndEdit();

    if (edit == Edit::BindingEntry)
    {
      if (iRow < static_cast<plInt32>(m_pView->m_Bindings.GetCount()) && m_pView->m_Bindings[iRow].m_sEntry != sText)
      {
        const auto& binding = m_pView->m_Bindings[iRow];
        m_pView->BindingEntryEdited(binding.m_ElementGuid, binding.m_sBindingProperty, sText);
      }
      return;
    }

    if (iRow >= static_cast<plInt32>(m_pView->m_MockRows.GetCount()))
      return;

    auto& row = m_pView->m_MockRows[iRow];
    plString& sTarget = edit == Edit::MockName ? row.m_sName : row.m_sValue;
    if (sTarget == sText)
      return;

    sTarget = sText;

    float fValue = 0.0f;
    if (edit == Edit::MockValue && ParseNumber(sText, fValue))
    {
      row.m_fMin = plMath::Min(row.m_fMin, fValue);
      row.m_fMax = plMath::Max(row.m_fMax, fValue);
    }

    m_bRebuild = true;
    m_pView->MockDataChanged();
  }

  void EndEdit()
  {
    if (m_Edit == Edit::None)
      return;

    GetSurface()->GetBlackboard().SetEntryValue(GetEditVisibleBinding(), false);
    GetSurface()->GetCanvas().CancelInput();
    m_Edit = Edit::None;
    m_iEditRow = -1;
    m_bEditFocused = false;
  }

  void PollSliders()
  {
    // sliders write their entry while they are dragged; the value text follows
    const auto& board = GetSurface()->GetBlackboard();
    for (plUInt32 i = 0; i < m_pView->m_MockRows.GetCount() && i < m_SliderValues.GetCount(); ++i)
    {
      const plVariant value = board.GetEntryValue(plTempHashedString(Name("Mock.sliderValue.{}", i)));
      if (!value.CanConvertTo<float>())
        continue;

      const float fValue = value.ConvertTo<float>();
      if (plMath::IsEqual(fValue, m_SliderValues[i], 1e-6f))
        continue;

      m_SliderValues[i] = fValue;
      m_pView->m_MockRows[i].m_sValue = FormatValue(fValue);
      GetSurface()->GetBlackboard().SetEntryValue(Name("Mock.value.{}", i), m_pView->m_MockRows[i].m_sValue);
      m_pView->MockDataChanged();
    }
  }

  /// Asks for a name and saves the current values under it.
  void SaveScenario()
  {
    const plString sName = plUiBindingsPanelBase::AskScenarioName();
    if (sName.IsEmpty())
      return;

    m_pView->m_sCurrentScenario = sName;
    m_pView->ScenarioSaveRequested(sName);
  }

  void ShowScenarioMenu(plUInt32 uiScenario, const plVec2& vPosition)
  {
    plEditorUiMenu menu;
    auto& item = menu.AddAction(plTranslate("GameUi.Bindings.ScenarioDelete"), 1);
    item.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Remove);
    item.m_bDanger = true;

    plEditorUiMenuView::Show(*GetSurface(), vPosition, menu, [this, uiScenario](plUInt32)
      {
        if (uiScenario < m_pView->m_Scenarios.GetCount())
          m_pView->ScenarioDeleteRequested(m_pView->m_Scenarios[uiScenario]); });
  }

  plUiBindingsView* m_pView = nullptr;
  plInt32 m_iBinding = -1;
  plInt32 m_iMock = -1;
  plInt32 m_iFocusTable = 0;
  Edit m_Edit = Edit::None;
  plInt32 m_iEditRow = -1;
  bool m_bEditFocused = false;
  plDynamicArray<float> m_SliderValues;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
  plEventSubscriptionID m_PointerSubscription = 0;
  plEventSubscriptionID m_KeySubscription = 0;
  plEditorUiTimerGroup m_Timers; ///< work that waits for the UI update to end
};

//////////////////////////////////////////////////////////////////////////
// plUiBindingsView

plUiBindingsView::plUiBindingsView() = default;

plUiBindingsView::~plUiBindingsView()
{
  PL_ASSERT_DEV(m_pSurface == nullptr, "The bindings panel's content must go before the view");
}

plUniquePtr<plEditorUiContent> plUiBindingsView::CreateContent()
{
  // the content's surface needs the UI renderer
  plEngineUiRenderer::EnsureRenderer();

  plUniquePtr<plUiBindingsSurface> pSurface(PL_DEFAULT_NEW(plUiBindingsSurface, this));
  m_pSurface = pSurface.Borrow();
  return pSurface;
}

void plUiBindingsView::RebuildSurface()
{
  if (m_pSurface != nullptr)
    m_pSurface->Rebuild();
}

bool plUiBindingsView::IsTemplateEntry(plStringView sName, plStringView* out_pInitialValue) const
{
  for (const MockEntry& entry : m_TemplateEntries)
  {
    if (entry.m_sName == sName)
    {
      if (out_pInitialValue != nullptr)
        *out_pInitialValue = entry.m_sValue;
      return true;
    }
  }

  return false;
}

plInt32 plUiBindingsView::FindMockRow(plStringView sName) const
{
  for (plUInt32 i = 0; i < m_MockRows.GetCount(); ++i)
  {
    if (m_MockRows[i].m_sName == sName)
      return static_cast<plInt32>(i);
  }

  return -1;
}

void plUiBindingsView::SetTemplateEntries(plArrayPtr<const MockEntry> entries)
{
  m_TemplateEntries = entries;
  RebuildSurface();
}

void plUiBindingsView::SetBindings(plArrayPtr<const BindingInfo> bindings)
{
  m_Bindings = bindings;

  const bool bMockChanged = MirrorEntriesIntoMockTable();
  RebuildSurface();

  if (bMockChanged)
    MockDataChanged();
}

bool plUiBindingsView::MirrorEntriesIntoMockTable()
{
  bool bChanged = false;

  auto addRow = [&](const plString& sName, const plString& sValue)
  {
    MockRow& row = m_MockRows.ExpandAndGetRef();
    row.m_sName = sName;
    row.m_sValue = sValue;
    bChanged = true;
  };

  // template entries first (the design-time contract), seeded with their initial values
  for (const MockEntry& entry : m_TemplateEntries)
  {
    if (!entry.m_sName.IsEmpty() && FindMockRow(entry.m_sName) < 0)
      addRow(entry.m_sName, entry.m_sValue);
  }

  // bound entries that are not in the template (still useful, flagged by their colour)
  for (const BindingInfo& binding : m_Bindings)
  {
    if (!binding.m_sEntry.IsEmpty() && FindMockRow(binding.m_sEntry) < 0)
      addRow(binding.m_sEntry, binding.m_sSeedValue);
  }

  for (MockRow& row : m_MockRows)
  {
    float fValue = 0.0f;
    if (ParseNumber(row.m_sValue, fValue))
    {
      row.m_fMin = plMath::Min(row.m_fMin, fValue);
      row.m_fMax = plMath::Max(row.m_fMax, fValue);
    }
  }

  return bChanged;
}

void plUiBindingsView::GetMockEntries(plDynamicArray<MockEntry>& out_entries) const
{
  out_entries.Clear();

  for (const MockRow& row : m_MockRows)
  {
    if (row.m_sName.IsEmpty())
      continue;

    MockEntry& entry = out_entries.ExpandAndGetRef();
    entry.m_sName = row.m_sName;
    entry.m_sValue = row.m_sValue;
  }
}

void plUiBindingsView::SetMockEntries(plArrayPtr<const MockEntry> entries)
{
  bool bChanged = false;

  for (const MockEntry& entry : entries)
  {
    if (entry.m_sName.IsEmpty())
      continue;

    const plInt32 iRow = FindMockRow(entry.m_sName);
    if (iRow < 0)
    {
      MockRow& row = m_MockRows.ExpandAndGetRef();
      row.m_sName = entry.m_sName;
      row.m_sValue = entry.m_sValue;
      bChanged = true;
    }
    else if (m_MockRows[iRow].m_sValue != entry.m_sValue)
    {
      m_MockRows[iRow].m_sValue = entry.m_sValue;
      bChanged = true;
    }
  }

  MirrorEntriesIntoMockTable();
  RebuildSurface();

  if (bChanged)
    MockDataChanged();
}

void plUiBindingsView::SetScenarios(plArrayPtr<const plString> names)
{
  m_Scenarios = names;

  if (!m_sCurrentScenario.IsEmpty() && !m_Scenarios.Contains(m_sCurrentScenario))
    m_sCurrentScenario.Clear();

  RebuildSurface();
}

void plUiBindingsView::SetMockValueSilently(plStringView sName, plStringView sValue)
{
  plInt32 iRow = FindMockRow(sName);
  if (iRow < 0)
  {
    MockRow& row = m_MockRows.ExpandAndGetRef();
    row.m_sName = sName;
    row.m_sValue = sValue;
    MirrorEntriesIntoMockTable();
    RebuildSurface();
    return;
  }

  MockRow& row = m_MockRows[iRow];
  row.m_sValue = sValue;

  float fValue = 0.0f;
  if (ParseNumber(row.m_sValue, fValue) && (fValue < row.m_fMin || fValue > row.m_fMax))
  {
    row.m_fMin = plMath::Min(row.m_fMin, fValue);
    row.m_fMax = plMath::Max(row.m_fMax, fValue);
    RebuildSurface();
    return;
  }

  if (m_pSurface != nullptr)
    m_pSurface->ShowMockValue(iRow);
}

void plUiBindingsView::ResetValues()
{
  bool bChanged = false;

  for (MockRow& row : m_MockRows)
  {
    plStringView sInitial;
    if (IsTemplateEntry(row.m_sName, &sInitial) && row.m_sValue != sInitial)
    {
      row.m_sValue = sInitial;
      bChanged = true;
    }
  }

  RebuildSurface();

  if (bChanged)
    MockDataChanged();
}
