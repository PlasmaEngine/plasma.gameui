#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/EngineAssetPicker.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiStatesView.h>
#include <EditorUi/Content.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Runtime.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GuiFoundation/EngineUi/EngineUiRenderer.h>

namespace
{
  enum Column : plUInt32
  {
    State,
    Entry,
    Condition,
    Value,
    Style,
    Clip,
    Visibility,
    Preview,
    Count
  };

  constexpr float s_fChainLineHeight = 30.0f;

  const char* GetConditionDisplay(plInt32 iCondition)
  {
    switch (iCondition)
    {
      case plUiStateCondition::IsTrue:
        return "is true";
      case plUiStateCondition::IsFalse:
        return "is false";
      case plUiStateCondition::Equal:
        return "=";
      case plUiStateCondition::NotEqual:
        return "!=";
      case plUiStateCondition::Less:
        return "<";
      case plUiStateCondition::LessEqual:
        return "<=";
      case plUiStateCondition::Greater:
        return ">";
      case plUiStateCondition::GreaterEqual:
        return ">=";
      default:
        return "?";
    }
  }

  const char* GetVisibilityKey(plInt32 iVisibility)
  {
    switch (iVisibility)
    {
      case 1:
        return "GameUi.States.VisShow";
      case 2:
        return "GameUi.States.VisHide";
      default:
        return "GameUi.States.VisNoChange";
    }
  }

  bool ConditionUsesValue(plInt32 iCondition)
  {
    return iCondition != plUiStateCondition::IsTrue && iCondition != plUiStateCondition::IsFalse;
  }

  /// A number with up to six decimals and no trailing zeros.
  plString NumberText(double fValue)
  {
    plStringBuilder sText;
    sText.SetFormat("{}", plArgF(fValue, 6));
    if (sText.FindSubString(".") != nullptr)
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

  /// The mock blackboard value that satisfies (or clears) a condition.
  plString ComputeConditionValue(plInt32 iCondition, double fValue, bool bSatisfy)
  {
    switch (iCondition)
    {
      case plUiStateCondition::IsTrue:
        return bSatisfy ? "true" : "false";
      case plUiStateCondition::IsFalse:
        return bSatisfy ? "false" : "true";
      case plUiStateCondition::Equal:
        return NumberText(bSatisfy ? fValue : fValue + 1.0);
      case plUiStateCondition::NotEqual:
        return NumberText(bSatisfy ? fValue + 1.0 : fValue);
      case plUiStateCondition::Less:
        return NumberText(bSatisfy ? fValue - 1.0 : fValue);
      case plUiStateCondition::LessEqual:
        return NumberText(bSatisfy ? fValue : fValue + 1.0);
      case plUiStateCondition::Greater:
        return NumberText(bSatisfy ? fValue + 1.0 : fValue);
      case plUiStateCondition::GreaterEqual:
      default:
        return NumberText(bSatisfy ? fValue : fValue - 1.0);
    }
  }

  plString MakeConditionText(const plUiStatesPanelBase::StateRow& state)
  {
    if (state.m_sEntry.IsEmpty())
      return plTranslate("GameUi.States.NoEntry");

    plStringBuilder sText;
    if (ConditionUsesValue(state.m_iCondition))
    {
      plStringBuilder sOperand;
      if (state.m_sValueText.IsEmpty())
        sOperand = NumberText(state.m_fValue);
      else
        sOperand.SetFormat("'{}'", state.m_sValueText);

      sText.SetFormat("{} {} {}", state.m_sEntry, GetConditionDisplay(state.m_iCondition), sOperand);
    }
    else
    {
      sText.SetFormat("{} {}", state.m_sEntry, GetConditionDisplay(state.m_iCondition));
    }

    return sText;
  }

  plString Name(const char* szFormat, plInt32 a)
  {
    plStringBuilder sName;
    sName.SetFormat(szFormat, a);
    return sName;
  }

  plString Name(const char* szFormat, plInt32 a, plInt32 b)
  {
    plStringBuilder sName;
    sName.SetFormat(szFormat, a, b);
    return sName;
  }

  /// Parses "<a>.<b>" after a prefix; false when the name does not have this form.
  bool ParseIndices(plStringView sName, plStringView sPrefix, plInt32& out_iA, plInt32& out_iB)
  {
    if (!sName.StartsWith(sPrefix))
      return false;

    const plStringView sRest(sName.GetStartPointer() + sPrefix.GetElementCount(), sName.GetEndPointer());
    plHybridArray<plStringView, 2> parts;
    sRest.Split(false, parts, ".");

    plUInt32 a = 0, b = 0;
    if (parts.IsEmpty() || plConversionUtils::StringToUInt(parts[0], a).Failed())
      return false;
    if (parts.GetCount() > 1 && plConversionUtils::StringToUInt(parts[1], b).Failed())
      return false;

    out_iA = static_cast<plInt32>(a);
    out_iB = parts.GetCount() > 1 ? static_cast<plInt32>(b) : -1;
    return true;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plUiStatesPanelBase

void plUiStatesPanelBase::AddStateRequested(const plUuid& elementGuid)
{
  if (m_OnAddStateRequested.IsValid())
    m_OnAddStateRequested(elementGuid);
}

void plUiStatesPanelBase::RemoveStateRequested(const plUuid& stateGuid)
{
  if (m_OnRemoveStateRequested.IsValid())
    m_OnRemoveStateRequested(stateGuid);
}

void plUiStatesPanelBase::StateEdited(const StateRow& state)
{
  if (m_OnStateEdited.IsValid())
    m_OnStateEdited(state);
}

void plUiStatesPanelBase::StateStyleChanged(const plUuid& stateGuid, plStringView sStyleRef)
{
  if (m_OnStateStyleChanged.IsValid())
    m_OnStateStyleChanged(stateGuid, sStyleRef);
}

void plUiStatesPanelBase::StateActivated(const plUuid& stateGuid)
{
  if (m_OnStateActivated.IsValid())
    m_OnStateActivated(stateGuid);
}

void plUiStatesPanelBase::DisabledBindingChanged(const plUuid& elementGuid, plStringView sEntry)
{
  if (m_OnDisabledBindingChanged.IsValid())
    m_OnDisabledBindingChanged(elementGuid, sEntry);
}

void plUiStatesPanelBase::TestValueChanged(plStringView sEntry, plStringView sValue)
{
  if (m_OnTestValueChanged.IsValid())
    m_OnTestValueChanged(sEntry, sValue);
}

//////////////////////////////////////////////////////////////////////////
// plUiStatesSurface

/// The engine-drawn content of plUiStatesView, which the document window shows as a panel.
class plUiStatesSurface : public plEditorUiContent
{
public:
  explicit plUiStatesSurface(plUiStatesView* pView)
    : plEditorUiContent("GameUi.States")
    , m_pView(pView)
  {
    if (!IsReady())
      return;

    m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plUiStatesSurface::OnPointer, this));
    m_KeySubscription = GetSurface()->m_KeyEvents.AddEventHandler(plMakeDelegate(&plUiStatesSurface::OnKey, this));
  }

  ~plUiStatesSurface()
  {
    // the view keeps its state without a content to draw it
    m_pView->m_pSurface = nullptr;

    if (!IsReady())
      return;

    GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
    GetSurface()->m_KeyEvents.RemoveEventHandler(m_KeySubscription);
  }

  /// Another element (or other rows): the previews clear and an open edit ends.
  void Reset(bool bNewElement)
  {
    if (m_Edit.m_iRow >= 0)
      EndEdit();

    m_Previews.Clear();
    m_bDisabledPreview = false;
    if (bNewElement)
      m_iSelected = -1;

    m_bRebuild = true;
  }

protected:
  void OnUiUpdate() override
  {
    if (!IsReady())
      return;

    if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
      BuildDocument();

    // an edit commits when its field loses the keyboard, as with Enter
    if (m_Edit.m_iRow >= 0)
    {
      const bool bFocused = GetSurface()->GetCanvas().GetFocusedElementName() == Name("States.edit.{}.{}", m_Edit.m_iRow, m_Edit.m_iColumn).GetView();
      if (m_Edit.m_bFocused && !bFocused)
        CommitEdit();
      else
        m_Edit.m_bFocused = bFocused;
    }
  }

  void OnUiEvent(const plMsgUiEvent& event) override
  {
    const plStringView sId = event.m_sIdentifier.GetView();

    if (sId == "States.commit")
    {
      CommitEdit();
    }
    else if (sId == "States.add")
    {
      if (m_pView->m_bHasElement)
        m_pView->AddStateRequested(m_pView->m_Info.m_ElementGuid);
    }
    else if (sId == "States.remove")
    {
      if (m_iSelected >= 0 && m_iSelected < static_cast<plInt32>(m_pView->m_Info.m_States.GetCount()))
        m_pView->RemoveStateRequested(m_pView->m_Info.m_States[m_iSelected].m_Guid);
    }
  }

private:
  struct EditState
  {
    plInt32 m_iRow = -1;
    plInt32 m_iColumn = -1;
    bool m_bFocused = false;
  };

  plUInt32 GetStateCount() const { return m_pView->m_Info.m_States.GetCount(); }
  bool HasDisabledRow() const { return m_pView->m_Info.m_bIsButton; }

  static plArrayPtr<const plGameUiEditor::Column> GetColumns()
  {
    static plGameUiEditor::Column s_Columns[Column::Count];
    s_Columns[State] = {plTranslate("GameUi.States.ColState"), 0.15f};
    s_Columns[Entry] = {plTranslate("GameUi.States.ColEntry"), 0.17f};
    s_Columns[Condition] = {plTranslate("GameUi.States.ColCondition"), 106.0f};
    s_Columns[Value] = {plTranslate("GameUi.States.ColValue"), 0.10f, true};
    s_Columns[Style] = {plTranslate("GameUi.States.ColStyle"), 0.20f};
    s_Columns[Clip] = {plTranslate("GameUi.States.ColClip"), 0.13f};
    s_Columns[Visibility] = {plTranslate("GameUi.States.ColVisibility"), 0.13f};
    s_Columns[Preview] = {plTranslate("GameUi.States.Preview"), 70.0f};
    return s_Columns;
  }

  plColorGammaUB GetHoverColor() const { return plGameUiEditor::Mix(plEditorUi::Surface(), plEditorUi::Raised(), 0.5f); }

  /// A cell button that opens something on press (menus, the asset browser); no click event.
  plUiButton* AddCellButton(plUiElement* pRow, const plRectFloat& rect, plStringView sName, plStringView sTooltip)
  {
    using namespace plEditorUi;

    auto* pButton = Button(pRow, plStringView(), sName);
    pButton->m_sClickEvent.Clear();
    pButton->ClearChildren();
    plGameUiEditor::Place(pButton, rect);
    pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
    pButton->m_HoverColor = plGameUiEditor::WithAlpha(Raised(), 160);
    pButton->m_PressedColor = pButton->m_HoverColor;
    pButton->m_vCornerRadius = plVec4(3);
    SetToolTip(GetSurface()->GetBlackboard(), sName, sTooltip);
    return pButton;
  }

  void AddCellEditor(plUiElement* pRow, const plRectFloat& rect, plInt32 iRow, plInt32 iColumn, bool bMono)
  {
    using namespace plEditorUi;

    auto* pInput = Input(pRow, Name("States.edit.{}.{}", iRow, iColumn), "States.editText", plStringView(), "States.commit");
    plGameUiEditor::Place(pInput, plRectFloat(rect.x + 2.0f, 2.0f, plMath::Max(rect.width - 4.0f, 8.0f), RowHeight - 4.0f));
    pInput->m_BorderColor = Accent();
    pInput->m_vCornerRadius = plVec4(3);
    pInput->m_sVisibleBinding = Name("States.editing.{}.{}", iRow, iColumn);
    if (bMono)
    {
      pInput->m_hFont = GetCodeFont();
      pInput->m_uiFontSize = FontSize - 2;
    }

    GetSurface()->GetBlackboard().SetEntryValue(pInput->m_sVisibleBinding.GetView(), m_Edit.m_iRow == iRow && m_Edit.m_iColumn == iColumn);
  }

  /// The chain of chips: Normal, Hover and Pressed for buttons, the states with their conditions, Disabled.
  float BuildChain(plUiPanel* pRoot, float y)
  {
    using namespace plEditorUi;

    struct Item
    {
      plString m_sText;
      bool m_bChip = true;
      bool m_bOn = false;
      bool m_bEdge = false;
      bool m_bDimmed = false;
    };

    const auto& info = m_pView->m_Info;
    plHybridArray<Item, 16> items;

    auto addArrow = [&](plStringView sCondition)
    {
      Item& item = items.ExpandAndGetRef();
      item.m_bChip = false;
      plStringBuilder sText;
      sText.SetFormat("\xE2\x86\x92 {} \xE2\x86\x92", sCondition);
      item.m_sText = sText;
    };

    items.ExpandAndGetRef() = {plTranslate("GameUi.States.Normal"), true, true};

    if (info.m_bIsButton)
    {
      addArrow(plTranslate("GameUi.States.HoverCondition"));
      items.ExpandAndGetRef() = {plTranslate("GameUi.States.Hover")};
      addArrow(plTranslate("GameUi.States.PressedCondition"));
      items.ExpandAndGetRef() = {plTranslate("GameUi.States.Pressed")};
    }

    // the state machine: the first matching condition wins
    for (const auto& state : info.m_States)
    {
      addArrow(MakeConditionText(state));

      plStringBuilder sChip(state.m_sName.IsEmpty() ? "?" : state.m_sName.GetData());
      if (!state.m_sPlayClip.IsEmpty())
        sChip.AppendFormat("  > {}", state.m_sPlayClip);

      Item& item = items.ExpandAndGetRef();
      item.m_sText = sChip;
      item.m_bEdge = true;
    }

    if (info.m_bIsButton && !info.m_sDisabledBinding.IsEmpty())
    {
      addArrow(info.m_sDisabledBinding);
      Item& item = items.ExpandAndGetRef();
      item.m_sText = plTranslate("GameUi.States.Disabled");
      item.m_bDimmed = true;
    }

    // the items flow left to right and wrap
    const float fWidth = m_vBuiltSize.x;
    float x = 6.0f;
    float fLineY = y;
    plUInt32 uiLines = 1;

    plUiPanel* pChain = Panel(pRoot);
    pChain->m_Color = Surface();

    for (const Item& item : items)
    {
      const float w = item.m_bChip ? plGameUiEditor::ChipWidth(item.m_sText) + (item.m_bEdge ? 4.0f : 0.0f) : plMath::Ceil(MeasureText(item.m_sText, FontSize - 1)) + 2.0f;
      if (x > 6.0f && x + w > fWidth - 6.0f)
      {
        x = 6.0f;
        fLineY += s_fChainLineHeight;
        ++uiLines;
      }

      if (item.m_bChip)
      {
        auto* pChip = Panel(pChain);
        plGameUiEditor::Place(pChip, plRectFloat(x, fLineY + (s_fChainLineHeight - RowHeight) * 0.5f, w, RowHeight));
        pChip->m_Color = item.m_bOn ? Selection() : Field();
        pChip->m_fBorderWidth = 1;
        pChip->m_BorderColor = item.m_bOn ? plGameUiEditor::Mix(Selection(), Accent(), 0.55f) : Line();
        pChip->m_vCornerRadius = plVec4(RowHeight * 0.5f);
        pChip->m_fOpacity = item.m_bDimmed ? 0.5f : 1.0f;

        if (item.m_bEdge)
        {
          auto* pEdge = Panel(pChip);
          pEdge->m_vAnchorMax.x = 0;
          pEdge->m_vOffsets = plVec4(1, 3, 3, -6);
          pEdge->m_Color = plGameUiEditor::ScriptingColor();
        }

        auto* pText = Text(pChip, item.m_sText);
        Fill(pText, plVec4(item.m_bEdge ? 12.0f : 9.0f, 0, 8, 0));
        pText->m_uiFontSize = FontSize - 1;
        pText->m_Color = item.m_bOn ? Foreground() : Muted();
        pText->m_Overflow = plUiTextOverflow::Overflow;
      }
      else
      {
        auto* pText = Text(pChain, item.m_sText);
        plGameUiEditor::Place(pText, plRectFloat(x, fLineY, w, s_fChainLineHeight));
        pText->m_uiFontSize = FontSize - 1;
        pText->m_Color = Dim();
        pText->m_Overflow = plUiTextOverflow::Overflow;
      }

      x += w + 6.0f;
    }

    const float fHeight = uiLines * s_fChainLineHeight;
    pChain->m_vAnchorMax.y = 0;
    pChain->m_vOffsets = plVec4(0, y, 0, fHeight);

    // the chips were placed in root space: move them into the chain panel
    for (plUiElement* pChild : pChain->m_Children)
      pChild->m_vOffsets.y -= y;

    auto* pLine = Panel(pChain);
    pLine->m_vAnchorMin.y = 1;
    pLine->m_vOffsets = plVec4(0, -1, 0, 1);
    pLine->m_Color = Line();

    return fHeight;
  }

  void BuildDocument()
  {
    using namespace plEditorUi;

    m_bRebuild = false;
    m_vBuiltSize = GetSurface()->GetLogicalSize();
    auto& board = GetSurface()->GetBlackboard();
    const auto& info = m_pView->m_Info;

    if (m_iSelected >= static_cast<plInt32>(GetStateCount()))
      m_iSelected = -1;

    plUiPanel* pRoot = Panel();
    pRoot->m_Color = Surface();

    if (!m_pView->m_bHasElement)
    {
      plGameUiEditor::SectionHeader(pRoot, plTranslate("GameUi.Panel.States"), {}, plGameUiEditor::ScriptingColor());
      plUiPanel* pEmpty = plGameUiEditor::EmptyState(pRoot, plGameUiEditor::IconPath("Pointer"), plTranslate("GameUi.States.NoSelection"));
      Fill(pEmpty, plVec4(0, SectionHeaderHeight, 0, 0));
      SetDocument(Finish(pRoot));
      return;
    }

    // "States - <element>", then the element's style
    {
      plStringBuilder sTitle;
      sTitle.SetFormat("{} - {}", plTranslate("GameUi.Panel.States"), info.m_sElementLabel);

      plStringBuilder sSuffix;
      if (!info.m_sStyleName.IsEmpty())
        sSuffix.SetFormat("({}: {})", plTranslate("GameUi.States.StyleLabel"), info.m_sStyleName);

      plGameUiEditor::SectionHeader(pRoot, sTitle, sSuffix, plGameUiEditor::ScriptingColor());
    }

    const float fChainHeight = BuildChain(pRoot, SectionHeaderHeight);
    const float fTableY = SectionHeaderHeight + fChainHeight;

    plHybridArray<float, 8> lefts, widths;
    plGameUiEditor::LayoutColumns(GetColumns(), m_vBuiltSize.x, lefts, widths);
    plGameUiEditor::TableHeader(pRoot, fTableY, GetColumns(), lefts, widths);

    plUiScrollView* pScroll = PL_DEFAULT_NEW(plUiScrollView);
    Fill(pScroll, plVec4(0, fTableY + plGameUiEditor::TableHeaderHeight, 0, plGameUiEditor::ToolRowHeight));
    pScroll->m_sName = "States.Scroll";
    pScroll->m_fSpacing = 0.0f;
    pScroll->m_Color = Surface();
    pRoot->m_Children.PushBack(pScroll);

    auto cellRect = [&](plUInt32 uiColumn)
    { return plRectFloat(lefts[uiColumn], 0, widths[uiColumn], RowHeight); };

    m_Previews.EnsureCount(GetStateCount());

    for (plUInt32 r = 0; r < GetStateCount(); ++r)
    {
      const auto& state = info.m_States[r];
      const plInt32 iRow = static_cast<plInt32>(r);
      plUiPanel* pRow = AddRow(pScroll, iRow);

      // State: the name; double-click selects the state in the property grid
      plGameUiEditor::CellText(pRow, cellRect(State), state.m_sName, Foreground());
      AddCellEditor(pRow, cellRect(State), iRow, State, false);

      // When Entry: the watched entry; the chevron offers the template entries
      {
        const plRectFloat rect = cellRect(Entry);
        plGameUiEditor::CellText(pRow, plRectFloat(rect.x, 0, rect.width - 18.0f, RowHeight), state.m_sEntry, plGameUiEditor::XrColor(), true);
        AddEntryPicker(pRow, rect, iRow);
        AddCellEditor(pRow, rect, iRow, Entry, true);
      }

      // Condition: a dropdown field
      {
        const plRectFloat rect = cellRect(Condition);
        const plString sName = Name("States.cell.{}.{}", iRow, Condition);
        auto* pField = plGameUiEditor::Dropdown(pRow, plRectFloat(rect.x + 6.0f, 1.0f, rect.width - 12.0f, RowHeight - 2.0f), sName, {}, {}, GetConditionDisplay(state.m_iCondition));
        pField->m_sClickEvent.Clear();
      }

      // Value: a number or a text operand; "-" when the condition takes none
      {
        const bool bUsesValue = ConditionUsesValue(state.m_iCondition);
        const plString sValue = !bUsesValue ? plString("-") : (state.m_sValueText.IsEmpty() ? NumberText(state.m_fValue) : state.m_sValueText);
        auto* pText = plGameUiEditor::CellText(pRow, cellRect(Value), sValue, bUsesValue ? Muted() : Dim(), true);
        pText->m_HAlign = plUiTextHAlign::Right;
        if (bUsesValue)
          AddCellEditor(pRow, cellRect(Value), iRow, Value, true);
      }

      // Style: the style's name with a clear button, or "pick style..."
      {
        const plRectFloat rect = cellRect(Style);
        const bool bStyled = !state.m_sStyleName.IsEmpty();
        AddCellButton(pRow, plRectFloat(rect.x + 2.0f, 1.0f, rect.width - (bStyled ? 26.0f : 4.0f), RowHeight - 2.0f), Name("States.cell.{}.{}", iRow, Style), plTranslate("GameUi.States.StyleTooltip"));
        plGameUiEditor::CellText(pRow, plRectFloat(rect.x, 0, rect.width - (bStyled ? 24.0f : 0.0f), RowHeight), bStyled ? state.m_sStyleName.GetView() : plTranslate("GameUi.States.PickStyle"), bStyled ? plGameUiEditor::UiColor() : Dim());

        if (bStyled)
        {
          auto* pClear = AddCellButton(pRow, plRectFloat(rect.x + rect.width - 24.0f, 2.0f, 20.0f, RowHeight - 4.0f), Name("States.clear.{}", iRow), plTranslate("GameUi.States.ClearStyleTooltip"));
          auto* pIcon = ButtonIcon(pClear, MonochromeIconPath(plGameUiEditor::IconPath("Close")));
          pIcon->m_vOffsets = plVec4(-6, -6, 12, 12);
          pIcon->m_Color = Muted();
        }
      }

      // Clip and Visibility: choices that open a menu
      {
        const plRectFloat rect = cellRect(Clip);
        AddCellButton(pRow, plRectFloat(rect.x + 2.0f, 1.0f, rect.width - 4.0f, RowHeight - 2.0f), Name("States.cell.{}.{}", iRow, Clip), plTranslate("GameUi.States.ClipTooltip"));
        plGameUiEditor::CellText(pRow, rect, state.m_sPlayClip.IsEmpty() ? plStringView("-") : state.m_sPlayClip.GetView(), state.m_sPlayClip.IsEmpty() ? Dim() : Muted());
      }

      {
        const plRectFloat rect = cellRect(Visibility);
        AddCellButton(pRow, plRectFloat(rect.x + 2.0f, 1.0f, rect.width - 4.0f, RowHeight - 2.0f), Name("States.cell.{}.{}", iRow, Visibility), plTranslate("GameUi.States.VisibilityTooltip"));
        plGameUiEditor::CellText(pRow, rect, plTranslate(GetVisibilityKey(state.m_iVisibility)), Muted());
      }

      // Preview: writes a value that satisfies or clears the condition into the mock blackboard
      {
        const plString sName = Name("States.cell.{}.{}", iRow, Preview);
        board.SetEntryValue(Name("States.preview.{}", iRow), m_Previews[r]);
        board.SetEntryValue(Name("States.noPreview.{}", iRow), state.m_sEntry.IsEmpty());
        plGameUiEditor::CheckBox(pRow, cellRect(Preview), sName, Name("States.preview.{}", iRow), Name("States.noPreview.{}", iRow))->m_sClickEvent.Clear();
        SetToolTip(board, sName, plTranslate("GameUi.States.TestTooltip"));
      }
    }

    if (HasDisabledRow())
      BuildDisabledRow(pScroll, lefts, widths);

    if (GetStateCount() == 0 && !HasDisabledRow())
    {
      plUiPanel* pEmpty = plGameUiEditor::EmptyState(pRoot, plEditorUi::IconPath(Icon::List), plTranslate("GameUi.States.Empty"), plTranslate("GameUi.States.EmptyLine"));
      Fill(pEmpty, plVec4(0, fTableY + plGameUiEditor::TableHeaderHeight, 0, plGameUiEditor::ToolRowHeight));
    }

    BuildTools(pRoot);

    SetDocument(Finish(pRoot));
    WriteSelection();
  }

  plUiPanel* AddRow(plUiElement* pParent, plInt32 iRow)
  {
    using namespace plEditorUi;

    auto* pRow = Panel(pParent);
    pRow->m_vOffsets.w = RowHeight;
    pRow->m_Color = plColorGammaUB(0, 0, 0, 0);

    auto* pCatcher = Button(pRow, plStringView(), Name("States.row.{}", iRow));
    pCatcher->m_sClickEvent.Clear();
    pCatcher->ClearChildren();
    Fill(pCatcher);
    pCatcher->m_Color = plColorGammaUB(0, 0, 0, 0);
    pCatcher->m_HoverColor = GetHoverColor();
    pCatcher->m_PressedColor = GetHoverColor();
    pCatcher->m_vCornerRadius = plVec4::MakeZero();
    SetToolTip(GetSurface()->GetBlackboard(), pCatcher->m_sName, plTranslate("GameUi.States.NameTooltip"));

    auto* pSelected = Panel(pRow);
    pSelected->m_Color = Selection();
    pSelected->m_sVisibleBinding = Name("States.sel.{}", iRow);

    auto* pRule = Panel(pRow);
    pRule->m_vAnchorMin.y = 1;
    pRule->m_vOffsets = plVec4(0, -1, 0, 1);
    pRule->m_Color = plGameUiEditor::WithAlpha(Line(), 150);
    return pRow;
  }

  void AddEntryPicker(plUiElement* pRow, const plRectFloat& rect, plInt32 iRow)
  {
    using namespace plEditorUi;

    auto* pPicker = AddCellButton(pRow, plRectFloat(rect.x + rect.width - 20.0f, 3.0f, 16.0f, RowHeight - 6.0f), Name("States.pick.{}", iRow), plTranslate("GameUi.States.EntryTooltip"));
    auto* pChevron = ButtonIcon(pPicker, IconPath(Icon::Expand));
    pChevron->m_vOffsets = plVec4(-5, -5, 10, 10);
    pChevron->m_Color = Dim();
  }

  /// Buttons only: "Disabled while <entry> is true", the hint and the preview.
  void BuildDisabledRow(plUiElement* pParent, plArrayPtr<const float> lefts, plArrayPtr<const float> widths)
  {
    using namespace plEditorUi;

    const auto& info = m_pView->m_Info;
    auto& board = GetSurface()->GetBlackboard();
    const plInt32 iRow = static_cast<plInt32>(GetStateCount());

    plUiPanel* pRow = AddRow(pParent, iRow);
    SetToolTip(board, Name("States.row.{}", iRow), plTranslate("GameUi.States.DisabledHint"));

    plGameUiEditor::CellText(pRow, plRectFloat(lefts[State], 0, widths[State], RowHeight), plTranslate("GameUi.States.Disabled"), Foreground());

    const plRectFloat entryRect(lefts[Entry], 0, widths[Entry], RowHeight);
    plStringBuilder sEntry;
    sEntry.SetFormat("{} {}", plTranslate("GameUi.States.While"), info.m_sDisabledBinding);
    plGameUiEditor::CellText(pRow, plRectFloat(entryRect.x, 0, entryRect.width - 18.0f, RowHeight), sEntry, plGameUiEditor::XrColor(), true);
    AddEntryPicker(pRow, entryRect, iRow);
    AddCellEditor(pRow, entryRect, iRow, Entry, true);

    plGameUiEditor::CellText(pRow, plRectFloat(lefts[Condition], 0, widths[Condition], RowHeight), plTranslate("GameUi.States.IsTrue"), Muted());

    const float fHintX = lefts[Value];
    plGameUiEditor::CellText(pRow, plRectFloat(fHintX, 0, lefts[Preview] - fHintX, RowHeight), plTranslate("GameUi.States.DisabledHint"), Dim())->m_uiFontSize = FontSize - 2;

    const plString sName = Name("States.cell.{}.{}", iRow, Preview);
    board.SetEntryValue(Name("States.preview.{}", iRow), m_bDisabledPreview);
    board.SetEntryValue(Name("States.noPreview.{}", iRow), info.m_sDisabledBinding.IsEmpty());
    plGameUiEditor::CheckBox(pRow, plRectFloat(lefts[Preview], 0, widths[Preview], RowHeight), sName, Name("States.preview.{}", iRow), Name("States.noPreview.{}", iRow))->m_sClickEvent.Clear();
    SetToolTip(board, sName, plTranslate("GameUi.States.TestTooltip"));
  }

  void BuildTools(plUiPanel* pRoot)
  {
    using namespace plEditorUi;

    auto& board = GetSurface()->GetBlackboard();

    plUiPanel* pTools = plGameUiEditor::ToolRow(pRoot, 0.0f, true);
    pTools->m_vAnchorMin.y = pTools->m_vAnchorMax.y = 1.0f;
    pTools->m_vOffsets.y = -plGameUiEditor::ToolRowHeight;

    const plStringView sAdd = plTranslate("GameUi.States.AddState");
    const plStringView sRemove = plTranslate("GameUi.States.RemoveState");
    const float fAdd = plGameUiEditor::TextButtonWidth(sAdd, true);
    const float fRemove = plGameUiEditor::TextButtonWidth(sRemove, true);
    const float y = (plGameUiEditor::ToolRowHeight - ControlHeight) * 0.5f;

    plGameUiEditor::TextButton(pTools, plRectFloat(6.0f, y, fAdd, ControlHeight), "States.add", sAdd, IconPath(Icon::Add));
    plGameUiEditor::TextButton(pTools, plRectFloat(12.0f + fAdd, y, fRemove, ControlHeight), "States.remove", sRemove, plGameUiEditor::IconPath("Minus"), "States.noSelection");

    // the order hint; its tooltip has the full description of the state machine
    const float x = 20.0f + fAdd + fRemove;
    auto* pHint = Button(pTools, plTranslate("GameUi.States.OrderHint"), "States.hint");
    plGameUiEditor::Place(pHint, plRectFloat(x, y, plMath::Max(m_vBuiltSize.x - x - 6.0f, 1.0f), ControlHeight));
    pHint->m_sClickEvent.Clear();
    pHint->m_Color = pHint->m_HoverColor = pHint->m_PressedColor = plColorGammaUB(0, 0, 0, 0);
    pHint->m_hStyle = GetGhostStyle();
    auto* pText = static_cast<plUiText*>(pHint->m_Children[0]);
    pText->m_HAlign = plUiTextHAlign::Left;
    pText->m_uiFontSize = FontSize - 2;
    pText->m_Color = Dim();
    pText->m_vOffsets.x = 2.0f;
    SetToolTip(board, "States.hint", plTranslate("GameUi.States.MachineHint"));
  }

  void WriteSelection()
  {
    auto& board = GetSurface()->GetBlackboard();
    const plUInt32 uiRows = GetStateCount() + (HasDisabledRow() ? 1 : 0);
    for (plUInt32 i = 0; i < uiRows; ++i)
      board.SetEntryValue(Name("States.sel.{}", i), m_iSelected == static_cast<plInt32>(i));

    board.SetEntryValue("States.noSelection", m_iSelected < 0 || m_iSelected >= static_cast<plInt32>(GetStateCount()));
  }

  void OnPointer(const plEditorUiPointerEvent& event)
  {
    using Type = plEditorUiPointerEvent::Type;

    if ((event.m_Type != Type::Press && event.m_Type != Type::DoubleClick) || event.m_Button != plEditorUiPointerEvent::Button::Left || !m_pView->m_bHasElement)
      return;

    const plStringView sElement = event.m_sElement;
    plInt32 iRow = -1, iColumn = -1;

    if (!ParseIndices(sElement, "States.row.", iRow, iColumn) && !ParseIndices(sElement, "States.cell.", iRow, iColumn) && !ParseIndices(sElement, "States.pick.", iRow, iColumn) && !ParseIndices(sElement, "States.clear.", iRow, iColumn))
      return;

    const bool bDisabledRow = HasDisabledRow() && iRow == static_cast<plInt32>(GetStateCount());
    if (iRow < 0 || (iRow >= static_cast<plInt32>(GetStateCount()) && !bDisabledRow))
      return;

    // any press selects the row (the Disabled row is not a state)
    m_iSelected = iRow;
    WriteSelection();

    if (sElement.StartsWith("States.pick."))
    {
      if (event.m_Type == Type::Press)
        ShowEntryMenu(iRow, event.m_ElementRect);
      return;
    }

    if (sElement.StartsWith("States.clear."))
    {
      if (event.m_Type == Type::Press)
        m_pView->StateStyleChanged(m_pView->m_Info.m_States[iRow].m_Guid, {});
      return;
    }

    if (sElement.StartsWith("States.cell."))
    {
      if (event.m_Type == Type::Press)
        OnCellPressed(iRow, iColumn, event.m_ElementRect);
      return;
    }

    // a double-click on the row: the name and style select the state, entries and values edit in place
    if (event.m_Type != Type::DoubleClick)
      return;

    const plInt32 iClickedColumn = GetColumnAt(event.m_vPosition.x);
    if (bDisabledRow)
    {
      if (iClickedColumn == Entry)
        BeginEdit(iRow, Entry);
      return;
    }

    const auto& state = m_pView->m_Info.m_States[iRow];
    if (iClickedColumn == Entry || (iClickedColumn == Value && ConditionUsesValue(state.m_iCondition)))
      BeginEdit(iRow, iClickedColumn);
    else
      m_pView->StateActivated(state.m_Guid);
  }

  plInt32 GetColumnAt(float x) const
  {
    plHybridArray<float, 8> lefts, widths;
    plGameUiEditor::LayoutColumns(GetColumns(), m_vBuiltSize.x, lefts, widths);

    for (plUInt32 c = 0; c < Column::Count; ++c)
    {
      if (x >= lefts[c] && x < lefts[c] + widths[c])
        return static_cast<plInt32>(c);
    }

    return -1;
  }

  void OnCellPressed(plInt32 iRow, plInt32 iColumn, const plRectFloat& rect)
  {
    const bool bDisabledRow = iRow == static_cast<plInt32>(GetStateCount());

    if (iColumn == Preview)
    {
      TogglePreview(iRow);
      return;
    }

    if (bDisabledRow)
      return;

    auto& state = m_pView->m_Info.m_States[iRow];
    const plVec2 vBelow(rect.x, rect.y + rect.height);

    if (iColumn == Condition)
    {
      plEditorUiMenu menu;
      for (plInt32 i = plUiStateCondition::IsTrue; i <= plUiStateCondition::GreaterEqual; ++i)
      {
        auto& item = menu.AddAction(GetConditionDisplay(i), static_cast<plUInt32>(i));
        item.m_bCheckable = true;
        item.m_bChecked = i == state.m_iCondition;
      }

      ShowRowMenu(menu, vBelow, iRow, [](plUiStatesPanelBase::StateRow& ref_row, plUInt32 uiId)
        { ref_row.m_iCondition = static_cast<plInt32>(uiId); });
    }
    else if (iColumn == Clip)
    {
      plEditorUiMenu menu;
      auto& none = menu.AddAction("-", 0);
      none.m_bCheckable = true;
      none.m_bChecked = state.m_sPlayClip.IsEmpty();
      for (plUInt32 i = 0; i < m_pView->m_ClipNames.GetCount(); ++i)
      {
        auto& item = menu.AddAction(m_pView->m_ClipNames[i], i + 1);
        item.m_bCheckable = true;
        item.m_bChecked = m_pView->m_ClipNames[i] == state.m_sPlayClip;
      }

      m_MenuRow = iRow;
      plEditorUiMenuView::Show(*GetSurface(), vBelow, menu, [this](plUInt32 uiId)
        {
          if (m_MenuRow >= static_cast<plInt32>(GetStateCount()))
            return;
          auto row = m_pView->m_Info.m_States[m_MenuRow];
          row.m_sPlayClip = uiId == 0 || uiId > m_pView->m_ClipNames.GetCount() ? plString() : m_pView->m_ClipNames[uiId - 1];
          m_pView->EmitStateEdited(row); });
    }
    else if (iColumn == Visibility)
    {
      plEditorUiMenu menu;
      for (plInt32 i = 0; i < 3; ++i)
      {
        auto& item = menu.AddAction(plTranslate(GetVisibilityKey(i)), static_cast<plUInt32>(i));
        item.m_bCheckable = true;
        item.m_bChecked = i == state.m_iVisibility;
      }

      ShowRowMenu(menu, vBelow, iRow, [](plUiStatesPanelBase::StateRow& ref_row, plUInt32 uiId)
        { ref_row.m_iVisibility = static_cast<plInt32>(uiId); });
    }
    else if (iColumn == Style)
    {
      // the asset browser opens after the UI update: its content is engine-drawn too
      const plUuid stateGuid = state.m_Guid;
      m_Timers.Start(plTime::MakeZero(), [this, stateGuid]()
        { PickStyle(stateGuid); });
    }
  }

  using RowChange = void (*)(plUiStatesPanelBase::StateRow&, plUInt32);

  /// Shows a menu for a row; the pick changes a copy of the row, which is emitted as the edit.
  void ShowRowMenu(const plEditorUiMenu& menu, const plVec2& vPosition, plInt32 iRow, RowChange change)
  {
    m_MenuRow = iRow;
    m_MenuChange = change;
    plEditorUiMenuView::Show(*GetSurface(), vPosition, menu, [this](plUInt32 uiId)
      {
        if (m_MenuRow >= static_cast<plInt32>(GetStateCount()) || m_MenuChange == nullptr)
          return;
        auto row = m_pView->m_Info.m_States[m_MenuRow];
        m_MenuChange(row, uiId);
        m_pView->EmitStateEdited(row); });
  }

  void ShowEntryMenu(plInt32 iRow, const plRectFloat& rect)
  {
    plEditorUiMenu menu;
    menu.AddAction("-", 0);
    for (plUInt32 i = 0; i < m_pView->m_TemplateEntries.GetCount(); ++i)
      menu.AddAction(m_pView->m_TemplateEntries[i], i + 1);

    m_MenuRow = iRow;
    plEditorUiMenuView::Show(*GetSurface(), plVec2(rect.x, rect.y + rect.height), menu, [this](plUInt32 uiId)
      {
        const plString sEntry = (uiId == 0 || uiId > m_pView->m_TemplateEntries.GetCount()) ? plString() : m_pView->m_TemplateEntries[uiId - 1];
        SetEntry(m_MenuRow, sEntry); });
  }

  /// The entry of a state, or the disabled entry for the Disabled row.
  void SetEntry(plInt32 iRow, plStringView sEntry)
  {
    if (iRow == static_cast<plInt32>(GetStateCount()) && HasDisabledRow())
    {
      m_pView->DisabledBindingChanged(m_pView->m_Info.m_ElementGuid, sEntry);
      return;
    }

    if (iRow < 0 || iRow >= static_cast<plInt32>(GetStateCount()))
      return;

    auto row = m_pView->m_Info.m_States[iRow];
    row.m_sEntry = sEntry;
    m_pView->EmitStateEdited(row);
  }

  void PickStyle(const plUuid& stateGuid)
  {
    // the state may have gone while the event was queued
    const plUiStatesPanelBase::StateRow* pRow = nullptr;
    for (const auto& row : m_pView->m_Info.m_States)
    {
      if (row.m_Guid == stateGuid)
        pRow = &row;
    }

    if (pRow == nullptr)
      return;

    plUuid currentGuid;
    if (plConversionUtils::IsStringUuid(pRow->m_sStyleRef))
      currentGuid = plConversionUtils::ConvertStringToUuid(pRow->m_sStyleRef);

    plEngineAssetPicker::Result picked;
    if (!plEngineAssetPicker::PickAsset(nullptr, currentGuid, "CompatibleAsset_Ui_Style", picked) || !picked.m_Guid.IsValid())
      return;

    plStringBuilder sGuid;
    plConversionUtils::ToString(picked.m_Guid, sGuid);
    m_pView->StateStyleChanged(stateGuid, sGuid);
  }

  void TogglePreview(plInt32 iRow)
  {
    const auto& info = m_pView->m_Info;
    auto& board = GetSurface()->GetBlackboard();

    if (iRow == static_cast<plInt32>(GetStateCount()))
    {
      if (info.m_sDisabledBinding.IsEmpty())
        return;

      m_bDisabledPreview = !m_bDisabledPreview;
      board.SetEntryValue(Name("States.preview.{}", iRow), m_bDisabledPreview);
      m_pView->TestValueChanged(info.m_sDisabledBinding, m_bDisabledPreview ? "true" : "false");
      return;
    }

    const auto& row = info.m_States[iRow];
    if (row.m_sEntry.IsEmpty())
      return;

    m_Previews.EnsureCount(GetStateCount());
    const bool bChecked = !m_Previews[iRow];
    m_Previews[iRow] = bChecked;
    board.SetEntryValue(Name("States.preview.{}", iRow), bChecked);

    // string operands satisfy with the text itself and clear with a mismatching suffix
    plString sValue;
    if (!row.m_sValueText.IsEmpty() && (row.m_iCondition == plUiStateCondition::Equal || row.m_iCondition == plUiStateCondition::NotEqual))
    {
      const bool bWriteMatch = (row.m_iCondition == plUiStateCondition::Equal) == bChecked;
      sValue = bWriteMatch ? row.m_sValueText : plString(plStringBuilder(row.m_sValueText, "_x"));
    }
    else
    {
      sValue = ComputeConditionValue(row.m_iCondition, row.m_fValue, bChecked);
    }

    m_pView->TestValueChanged(row.m_sEntry, sValue);
  }

  void OnKey(const plEditorUiKeyEvent& event)
  {
    if (event.m_bHandled)
      return;

    if (m_Edit.m_iRow >= 0)
    {
      if (event.m_Key == plEditorUiKeyEvent::Key::Escape)
      {
        EndEdit();
        event.m_bHandled = true;
      }
      return;
    }

    // F2 renames the selected state
    if (event.m_Key == plEditorUiKeyEvent::Key::F2 && m_iSelected >= 0 && m_iSelected < static_cast<plInt32>(GetStateCount()))
    {
      BeginEdit(m_iSelected, State);
      event.m_bHandled = true;
    }
  }

  void BeginEdit(plInt32 iRow, plInt32 iColumn)
  {
    if (m_Edit.m_iRow >= 0)
      CommitEdit();

    const auto& info = m_pView->m_Info;
    plString sText;
    if (iRow == static_cast<plInt32>(GetStateCount()))
    {
      sText = info.m_sDisabledBinding;
    }
    else
    {
      const auto& state = info.m_States[iRow];
      if (iColumn == State)
        sText = state.m_sName;
      else if (iColumn == Entry)
        sText = state.m_sEntry;
      else
        sText = state.m_sValueText.IsEmpty() ? NumberText(state.m_fValue) : state.m_sValueText;
    }

    m_Edit.m_iRow = iRow;
    m_Edit.m_iColumn = iColumn;
    m_Edit.m_bFocused = false;

    auto& board = GetSurface()->GetBlackboard();
    board.SetEntryValue("States.editText", sText);
    board.SetEntryValue(Name("States.editing.{}.{}", iRow, iColumn), true);

    GetSurface()->Update();
    GetSurface()->GetCanvas().FocusElement(Name("States.edit.{}.{}", iRow, iColumn), GetSurface()->GetLogicalSize());
    GetSurface()->GetCanvas().SetTextBinding("States.editText", sText);
  }

  void CommitEdit()
  {
    if (m_Edit.m_iRow < 0)
      return;

    const EditState edit = m_Edit;
    plStringBuilder sText = GetSurface()->GetBlackboard().GetEntryValue(plTempHashedString("States.editText"), plString()).ConvertTo<plString>();
    EndEdit();

    if (edit.m_iColumn == Entry)
    {
      const bool bDisabledRow = edit.m_iRow == static_cast<plInt32>(GetStateCount());
      const plString& sCurrent = bDisabledRow ? m_pView->m_Info.m_sDisabledBinding : m_pView->m_Info.m_States[edit.m_iRow].m_sEntry;
      if (sCurrent != sText)
        SetEntry(edit.m_iRow, sText);
      return;
    }

    if (edit.m_iRow >= static_cast<plInt32>(GetStateCount()))
      return;

    auto row = m_pView->m_Info.m_States[edit.m_iRow];
    if (edit.m_iColumn == State)
    {
      sText.Trim(" \t");
      if (row.m_sName == sText)
        return;
      row.m_sName = sText;
    }
    else
    {
      // a number, or a text operand that makes = and != compare as strings
      sText.Trim(" \t");
      double fValue = 0.0;
      const char* szEnd = nullptr;
      const bool bNumber = !sText.IsEmpty() && plConversionUtils::StringToFloat(sText, fValue, &szEnd).Succeeded() && szEnd == sText.GetData() + sText.GetElementCount();

      if (bNumber || sText.IsEmpty() || sText == "-")
      {
        row.m_fValue = bNumber ? fValue : 0.0;
        row.m_sValueText.Clear();
      }
      else
      {
        row.m_fValue = 0.0;
        row.m_sValueText = sText;
      }
    }

    m_pView->EmitStateEdited(row);
  }

  void EndEdit()
  {
    if (m_Edit.m_iRow < 0)
      return;

    GetSurface()->GetBlackboard().SetEntryValue(Name("States.editing.{}.{}", m_Edit.m_iRow, m_Edit.m_iColumn), false);
    GetSurface()->GetCanvas().CancelInput();
    m_Edit = EditState();
  }

  plUiStatesView* m_pView = nullptr;
  plInt32 m_iSelected = -1;
  EditState m_Edit;
  plDynamicArray<bool> m_Previews;
  bool m_bDisabledPreview = false;
  plInt32 m_MenuRow = -1;
  RowChange m_MenuChange = nullptr;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
  plEventSubscriptionID m_PointerSubscription = 0;
  plEventSubscriptionID m_KeySubscription = 0;
  plEditorUiTimerGroup m_Timers; ///< work that waits for the UI update to end
};

//////////////////////////////////////////////////////////////////////////
// plUiStatesView

plUiStatesView::plUiStatesView() = default;

plUiStatesView::~plUiStatesView()
{
  PL_ASSERT_DEV(m_pSurface == nullptr, "The states panel's content must go before the view");
}

plUniquePtr<plEditorUiContent> plUiStatesView::CreateContent()
{
  // the content's surface needs the UI renderer
  plEngineUiRenderer::EnsureRenderer();

  plUniquePtr<plUiStatesSurface> pSurface(PL_DEFAULT_NEW(plUiStatesSurface, this));
  m_pSurface = pSurface.Borrow();
  return pSurface;
}

void plUiStatesView::ResetSurface(bool bNewElement)
{
  if (m_pSurface != nullptr)
    m_pSurface->Reset(bNewElement);
}

void plUiStatesView::SetTemplateEntries(const plDynamicArray<plString>& entries)
{
  m_TemplateEntries = entries;
}

void plUiStatesView::SetClipNames(const plDynamicArray<plString>& clipNames)
{
  if (m_ClipNames == clipNames)
    return;

  m_ClipNames = clipNames;
  ResetSurface(false);
}

void plUiStatesView::SetElement(const ElementStateInfo* pInfo)
{
  const plUuid previousElement = m_bHasElement ? m_Info.m_ElementGuid : plUuid();

  m_bHasElement = pInfo != nullptr;
  m_Info = pInfo != nullptr ? *pInfo : ElementStateInfo();

  ResetSurface(!m_bHasElement || previousElement != m_Info.m_ElementGuid);
}

void plUiStatesView::EmitStateEdited(const StateRow& row)
{
  StateEdited(row);
}
