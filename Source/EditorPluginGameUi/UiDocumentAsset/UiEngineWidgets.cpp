#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <Core/Utils/Blackboard.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <Foundation/Math/ColorScheme.h>
#include <GameUiPlugin/Elements/UiElements.h>

namespace plGameUiEditor
{
  plString IconPath(plStringView sName)
  {
    plStringBuilder path(":/GameUiIcons/Icons/", sName, ".svg");
    return path;
  }

  plString TypeIconPath(const plRTTI* pType)
  {
    // the order matters: Item List and Text Input derive from types that have their own icon
    if (pType->IsDerivedFrom<plUiItemList>() || pType->IsDerivedFrom<plUiScrollView>())
      return IconPath("TypeList");
    if (pType->IsDerivedFrom<plUiTextInput>())
      return IconPath("TypeKeyboard");
    if (pType->IsDerivedFrom<plUiText>())
      return IconPath("Font");
    if (pType->IsDerivedFrom<plUiToggle>() || pType->IsDerivedFrom<plUiSlider>())
      return IconPath("TypeSliders");
    if (pType->IsDerivedFrom<plUiButton>())
      return IconPath("TypeButton");
    if (pType->IsDerivedFrom<plUiProgressBar>())
      return IconPath("TypeMeter");
    if (pType->IsDerivedFrom<plUiImage>())
      return IconPath("TypeImage");
    if (pType->IsDerivedFrom<plUiPath>())
      return IconPath("TypePath");
    if (pType->IsDerivedFrom<plUiStackPanel>())
      return IconPath("TypeStack");
    if (pType->IsDerivedFrom<plUiGridPanel>())
      return IconPath("Grid");
    if (pType->IsDerivedFrom<plUiSubDocument>())
      return IconPath("TypeDocument");
    if (pType->IsDerivedFrom<plUiSpacer>())
      return IconPath("TypeSpacer");

    return IconPath("TypePanel");
  }

  plColorGammaUB UiColor()
  {
    return plColorScheme::GetCategoryColor("UI", plColorScheme::CategoryColorUsage::BorderColor);
  }

  plColorGammaUB XrColor()
  {
    return plColorScheme::GetCategoryColor("XR", plColorScheme::CategoryColorUsage::BorderColor);
  }

  plColorGammaUB ScriptingColor()
  {
    return plColorScheme::GetCategoryColor("Scripting", plColorScheme::CategoryColorUsage::BorderColor);
  }

  plColorGammaUB Mix(plColorGammaUB a, plColorGammaUB b, float fAmount)
  {
    auto mix = [fAmount](plUInt8 x, plUInt8 y)
    { return static_cast<plUInt8>(plMath::Clamp(x + (y - x) * fAmount + 0.5f, 0.0f, 255.0f)); };

    return plColorGammaUB(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255);
  }

  plColorGammaUB WithAlpha(plColorGammaUB color, plUInt8 uiAlpha)
  {
    color.a = uiAlpha;
    return color;
  }

  plColorGammaUB ConnectedColor()
  {
    return Mix(plColorGammaUB(0x69, 0xDB, 0x7C), plEditorUi::Foreground(), 0.18f);
  }

  void Place(plUiElement* pElement, const plRectFloat& rect)
  {
    pElement->m_vAnchorMin = pElement->m_vAnchorMax = plVec2::MakeZero();
    pElement->m_vOffsets = plVec4(rect.x, rect.y, rect.width, rect.height);
  }

  plUiPanel* ToolRow(plUiElement* pParent, float y, bool bTopLine)
  {
    using namespace plEditorUi;

    auto* pRow = Panel(pParent);
    pRow->m_vAnchorMax.y = 0;
    pRow->m_vOffsets = plVec4(0, y, 0, ToolRowHeight);
    pRow->m_Color = Surface();
    pRow->m_bClipChildren = false;

    auto* pLine = Panel(pRow);
    pLine->m_vAnchorMin.y = bTopLine ? 0.0f : 1.0f;
    pLine->m_vAnchorMax.y = bTopLine ? 0.0f : 1.0f;
    pLine->m_vOffsets = plVec4(0, bTopLine ? 0.0f : -1.0f, 0, 1);
    pLine->m_Color = Line();

    return pRow;
  }

  plUiPanel* EmptyState(plUiElement* pParent, plStringView sIconPath, plStringView sTitle, plStringView sLine)
  {
    using namespace plEditorUi;

    const bool bLine = !sLine.IsEmpty();
    const float fHeight = 22.0f + 8.0f + 18.0f + (bLine ? 18.0f : 0.0f);

    auto* pRoot = Panel(pParent);
    pRoot->m_Color = plColorGammaUB(0, 0, 0, 0);

    auto* pColumn = Panel(pRoot);
    pColumn->m_vAnchorMin = plVec2(0, 0.5f);
    pColumn->m_vAnchorMax = plVec2(1, 0.5f);
    pColumn->m_vOffsets = plVec4(16, -fHeight * 0.5f, -16, fHeight);
    pColumn->m_Color = plColorGammaUB(0, 0, 0, 0);

    auto* pIcon = SvgImage(pColumn, MonochromeIconPath(sIconPath));
    pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(0.5f, 0);
    pIcon->m_vOffsets = plVec4(-11, 0, 22, 22);
    pIcon->m_Color = Dim();

    float y = 30.0f;
    auto addLine = [&](plStringView sText, plColorGammaUB color, plUInt32 uiSize)
    {
      auto* pText = Text(pColumn, sText);
      pText->m_vAnchorMin = plVec2(0, 0);
      pText->m_vAnchorMax = plVec2(1, 0);
      pText->m_vOffsets = plVec4(0, y, 0, 18);
      pText->m_HAlign = plUiTextHAlign::Center;
      pText->m_Color = color;
      pText->m_uiFontSize = uiSize;
      y += 18.0f;
    };

    addLine(sTitle, Muted(), FontSize);
    if (bLine)
      addLine(sLine, Dim(), FontSize - 1);

    return pRoot;
  }

  plUiPanel* SectionHeader(plUiElement* pParent, plStringView sTitle, plStringView sSuffix, plColorGammaUB color)
  {
    using namespace plEditorUi;

    auto* pHeader = plEditorUi::SectionHeader(pParent, sTitle, color);
    if (sSuffix.IsEmpty())
      return pHeader;

    plStringBuilder sUpper(sTitle);
    sUpper.ToUpper();
    const float x = 12.0f + MeasureText(sUpper, MinFontSize + 1) + 10.0f;

    auto* pSuffix = Text(pHeader, sSuffix);
    pSuffix->m_vOffsets = plVec4(x, 0, 8, 0);
    pSuffix->m_uiFontSize = FontSize - 1;
    pSuffix->m_Color = Dim();
    return pHeader;
  }

  float TextButtonWidth(plStringView sText, bool bIcon)
  {
    return plMath::Ceil(plEditorUi::MeasureText(sText, plEditorUi::FontSize - 1)) + (bIcon ? 40.0f : 20.0f);
  }

  plUiButton* TextButton(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sIconPath, plStringView sDisabledBinding)
  {
    using namespace plEditorUi;

    auto* pButton = plEditorUi::TextButton(pParent, sText, sName);
    Place(pButton, rect);
    pButton->m_sDisabledBinding = sDisabledBinding;

    auto* pText = static_cast<plUiText*>(pButton->m_Children[0]);
    if (!sIconPath.IsEmpty())
    {
      pText->m_vOffsets.x = 26;

      auto* pIcon = ButtonIcon(pButton, MonochromeIconPath(sIconPath));
      pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(0, 0.5f);
      pIcon->m_vOffsets = plVec4(8, -7, 14, 14);
      pIcon->m_Color = Muted();
    }

    return pButton;
  }

  plUiButton* IconToggle(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sIconPath, plStringView sOnBinding, plStringView sOffBinding)
  {
    using namespace plEditorUi;

    auto* pPlate = Panel(pParent);
    Place(pPlate, rect);
    pPlate->m_Color = Selection();
    pPlate->m_vCornerRadius = plVec4(4);
    pPlate->m_sVisibleBinding = sOnBinding;

    auto* pButton = Button(pParent, plStringView(), sName);
    Place(pButton, rect);
    pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
    pButton->m_hStyle = GetGhostStyle();
    pButton->ClearChildren();

    const plString sIcon = MonochromeIconPath(sIconPath);
    ButtonIcon(pButton, sIcon, sOffBinding)->m_Color = Muted();
    ButtonIcon(pButton, sIcon, sOnBinding)->m_Color = Accent();
    return pButton;
  }

  plUiButton* Dropdown(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLead, plStringView sLeadIcon, plStringView sText, bool bEnabled)
  {
    using namespace plEditorUi;

    auto* pButton = plEditorUi::TextButton(pParent, plStringView(), sName);
    Place(pButton, rect);
    pButton->m_Color = Field();
    pButton->m_HoverColor = Field();
    pButton->m_PressedColor = Field();
    pButton->m_BorderColor = Line();
    pButton->ClearChildren();

    if (!bEnabled)
      pButton->m_sClickEvent.Clear();

    float x = 8.0f;
    if (!sLeadIcon.IsEmpty())
    {
      auto* pIcon = SvgImage(pButton, MonochromeIconPath(sLeadIcon));
      pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(0, 0.5f);
      pIcon->m_vOffsets = plVec4(x, -7, 14, 14);
      pIcon->m_Color = Dim();
      x += 20.0f;
    }

    if (!sLead.IsEmpty())
    {
      const float fLead = plMath::Ceil(MeasureText(sLead, FontSize - 1));
      auto* pLead = Text(pButton, sLead);
      pLead->m_vAnchorMax.x = 0;
      pLead->m_vOffsets = plVec4(x, 0, fLead + 2.0f, 0);
      pLead->m_uiFontSize = FontSize - 1;
      pLead->m_Color = Dim();
      x += fLead + 6.0f;
    }

    auto* pText = Text(pButton, sText);
    pText->m_vOffsets = plVec4(x, 0, 22, 0);
    pText->m_uiFontSize = FontSize - 1;
    pText->m_Color = bEnabled ? Foreground() : Dim();

    auto* pChevron = SvgImage(pButton, plEditorUi::IconPath(Icon::Expand));
    pChevron->m_vAnchorMin = pChevron->m_vAnchorMax = plVec2(1, 0.5f);
    pChevron->m_vOffsets = plVec4(-18, -6, 12, 12);
    pChevron->m_Color = WithAlpha(Dim(), bEnabled ? 255 : 120);
    return pButton;
  }

  plUiButton* CheckBox(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sOnBinding, plStringView sDisabledBinding)
  {
    using namespace plEditorUi;

    auto* pButton = Button(pParent, plStringView(), sName);
    Place(pButton, rect);
    pButton->m_Color = plColorGammaUB(0, 0, 0, 0);
    pButton->m_hStyle = GetGhostStyle();
    pButton->m_sDisabledBinding = sDisabledBinding;
    pButton->ClearChildren();

    auto* pBox = Panel(pButton);
    pBox->m_vAnchorMin = pBox->m_vAnchorMax = plVec2(0.5f);
    pBox->m_vOffsets = plVec4(-7, -7, 14, 14);
    pBox->m_Color = Field();
    pBox->m_fBorderWidth = 1;
    pBox->m_BorderColor = Border();
    pBox->m_vCornerRadius = plVec4(3);

    auto* pOn = Panel(pBox);
    pOn->m_Color = Selection();
    pOn->m_fBorderWidth = 1;
    pOn->m_BorderColor = WithAlpha(Accent(), 140);
    pOn->m_vCornerRadius = plVec4(3);
    pOn->m_sVisibleBinding = sOnBinding;

    auto* pMark = SvgImage(pOn, plEditorUi::IconPath(Icon::Check));
    pMark->m_vAnchorMin = pMark->m_vAnchorMax = plVec2(0.5f);
    pMark->m_vOffsets = plVec4(-5, -5, 10, 10);
    pMark->m_Color = Accent();
    return pButton;
  }

  namespace
  {
    constexpr float s_fSegmentPadding = 8.0f;

    float SegmentWidth(plStringView sLabel)
    {
      return plMath::Ceil(plEditorUi::MeasureText(sLabel, plEditorUi::FontSize - 1)) + 2.0f * s_fSegmentPadding;
    }
  } // namespace

  float SegmentedWidth(plArrayPtr<const plString> labels)
  {
    float fWidth = 0.0f;
    for (const plString& sLabel : labels)
      fWidth += SegmentWidth(sLabel);

    return fWidth;
  }

  plString SegmentName(plStringView sNamePrefix, plUInt32 uiIndex)
  {
    plStringBuilder sName;
    sName.SetFormat("{}.{}", sNamePrefix, uiIndex);
    return sName;
  }

  void Segmented(plUiElement* pParent, const plRectFloat& rect, plStringView sNamePrefix, plArrayPtr<const plString> labels, plInt32 iSelected)
  {
    using namespace plEditorUi;

    auto* pWell = Panel(pParent);
    Place(pWell, rect);
    pWell->m_Color = Field();
    pWell->m_fBorderWidth = 1.0f;
    pWell->m_BorderColor = Line();
    pWell->m_vCornerRadius = plVec4(4);
    pWell->m_bClipChildren = true;

    // the segments share the well's height; a hairline separates neighbours
    float x = 0.0f;
    for (plUInt32 i = 0; i < labels.GetCount(); ++i)
    {
      const bool bSelected = static_cast<plInt32>(i) == iSelected;
      const float fWidth = SegmentWidth(labels[i]);

      if (i > 0)
      {
        auto* pDivider = Panel(pWell);
        Place(pDivider, plRectFloat(x, 1.0f, 1.0f, rect.height - 2.0f));
        pDivider->m_Color = Line();
      }

      const plRectFloat segmentRect(x + (i > 0 ? 1.0f : 0.0f), 0.0f, fWidth - (i > 0 ? 1.0f : 0.0f), rect.height);

      // the ghost style replaces a button's own colours, so the selection fill is a panel under it
      if (bSelected)
      {
        auto* pFill = Panel(pWell);
        Place(pFill, segmentRect);
        pFill->m_Color = WithAlpha(Accent(), 56);
      }

      auto* pSegment = Button(pWell, plStringView(), SegmentName(sNamePrefix, i));
      Place(pSegment, segmentRect);
      pSegment->ClearChildren();
      pSegment->m_hStyle = GetGhostStyle();

      auto* pLabel = Text(pSegment, labels[i]);
      pLabel->m_HAlign = plUiTextHAlign::Center;
      pLabel->m_VAlign = plUiTextVAlign::Center;
      pLabel->m_uiFontSize = FontSize - 1;
      pLabel->m_Color = bSelected ? Foreground() : Muted();

      x += fWidth;
    }
  }

  float ChipWidth(plStringView sText)
  {
    return plMath::Ceil(plEditorUi::MeasureText(sText, plEditorUi::FontSize - 1)) + 18.0f;
  }

  plUiButton* Chip(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, bool bOn, plColorGammaUB edgeColor)
  {
    using namespace plEditorUi;

    auto* pChip = Button(pParent, sText, sName);
    Place(pChip, rect);
    pChip->m_vCornerRadius = plVec4(rect.height * 0.5f);
    pChip->m_fBorderWidth = 1;
    pChip->m_Color = bOn ? Selection() : Field();
    pChip->m_HoverColor = bOn ? Selection() : Raised();
    pChip->m_BorderColor = bOn ? Mix(Selection(), Accent(), 0.55f) : Line();

    auto* pText = static_cast<plUiText*>(pChip->m_Children[0]);
    pText->m_uiFontSize = FontSize - 1;
    pText->m_Color = bOn ? Foreground() : Muted();
    pText->m_Overflow = plUiTextOverflow::Overflow;
    pText->m_vOffsets = plVec4(edgeColor.a > 0 ? 11.0f : 8.0f, 0, -8.0f, 0);

    if (edgeColor.a > 0)
    {
      auto* pEdge = Panel(pChip);
      pEdge->m_vAnchorMax.x = 0;
      pEdge->m_vOffsets = plVec4(1, 3, 3, -6);
      pEdge->m_Color = edgeColor;
    }

    return pChip;
  }

  void LayoutColumns(plArrayPtr<const Column> columns, float fWidth, plDynamicArray<float>& out_lefts, plDynamicArray<float>& out_widths)
  {
    out_lefts.Clear();
    out_widths.Clear();

    float fFixed = 0.0f;
    float fShares = 0.0f;
    for (const Column& column : columns)
    {
      if (column.m_fWidth > 1.0f)
        fFixed += column.m_fWidth;
      else
        fShares += column.m_fWidth;
    }

    const float fRest = plMath::Max(fWidth - fFixed, 0.0f);
    float x = 0.0f;
    for (const Column& column : columns)
    {
      const float w = column.m_fWidth > 1.0f ? column.m_fWidth : (fShares > 0.0f ? fRest * column.m_fWidth / fShares : 0.0f);
      out_lefts.PushBack(x);
      out_widths.PushBack(w);
      x += w;
    }
  }

  plUiPanel* TableHeader(plUiElement* pParent, float y, plArrayPtr<const Column> columns, plArrayPtr<const float> lefts, plArrayPtr<const float> widths)
  {
    using namespace plEditorUi;

    auto* pHeader = Panel(pParent);
    pHeader->m_vAnchorMax.y = 0;
    pHeader->m_vOffsets = plVec4(0, y, 0, TableHeaderHeight);
    pHeader->m_Color = Surface();

    auto* pLine = Panel(pHeader);
    pLine->m_vAnchorMin.y = 1;
    pLine->m_vOffsets = plVec4(0, -1, 0, 1);
    pLine->m_Color = Line();

    for (plUInt32 i = 0; i < columns.GetCount(); ++i)
    {
      plStringBuilder sTitle(columns[i].m_sTitle);
      sTitle.ToUpper();

      auto* pText = Text(pHeader, sTitle);
      pText->m_vAnchorMax = plVec2(0, 1);
      pText->m_vOffsets = plVec4(lefts[i] + 8.0f, 0, plMath::Max(widths[i] - 16.0f, 1.0f), 0);
      pText->m_uiFontSize = MinFontSize;
      pText->m_Color = Dim();
      pText->m_HAlign = columns[i].m_bRightAligned ? plUiTextHAlign::Right : plUiTextHAlign::Left;
    }

    return pHeader;
  }

  plUiText* CellText(plUiElement* pParent, const plRectFloat& rect, plStringView sText, plColorGammaUB color, bool bMono, plStringView sBinding)
  {
    using namespace plEditorUi;

    auto* pText = Text(pParent, sText, sBinding);
    Place(pText, plRectFloat(rect.x + 8.0f, rect.y, plMath::Max(rect.width - 16.0f, 1.0f), rect.height));
    pText->m_Color = color;
    if (bMono)
    {
      pText->m_hFont = GetCodeFont();
      pText->m_uiFontSize = FontSize - 2;
    }

    return pText;
  }

  void MarkField(plUiElement* pParent, const plRectFloat& rect, plEditorUiSeverity severity)
  {
    using namespace plEditorUi;

    if (severity == plEditorUiSeverity::None)
      return;

    plUiPanel* pMark = Panel(pParent);
    SetRect(pMark, rect);
    pMark->m_Color = plColorGammaUB(0, 0, 0, 0);
    pMark->m_fBorderWidth = 1.0f;
    pMark->m_BorderColor = SeverityColor(severity);
    pMark->m_vCornerRadius = plVec4(4);
  }

  plUiTextInput* ComboField(plUiElement* pParent, plBlackboard& ref_board, const plRectFloat& rect, plStringView sName, plStringView sBinding, plStringView sPickAction, bool bPick, bool bCode)
  {
    using namespace plEditorUi;

    plRectFloat field = rect;
    if (bPick)
    {
      field.width -= ControlHeight + 4.0f;
      IconPushButton(pParent, ref_board, plRectFloat(field.Right() + 4.0f, rect.y, ControlHeight, ControlHeight), plEditorUi::IconPath(Icon::Expand), sPickAction);
    }

    return TextField(pParent, field, sName, sBinding, {}, {}, bCode);
  }

  namespace
  {
    /// The mask character of secret fields: a bullet when the code face draws it as wide as its other characters, so the
    /// caret of the hidden text lines up with it, else an asterisk.
    plStringView GetMaskCharacter()
    {
      static const plStringView s_sMask = []()
      {
        const plStringView sBullet = "\xE2\x80\xA2";
        const bool bSameWidth = plMath::IsEqual(plEditorUi::MeasureCodeText(sBullet), plEditorUi::MeasureCodeText("x"), 0.5f);
        return bSameWidth ? sBullet : plStringView("*");
      }();

      return s_sMask;
    }
  } // namespace

  plUiTextInput* SecretField(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sBinding, plStringView sMaskBinding, plStringView sEmptyBinding, plStringView sPlaceholder)
  {
    using namespace plEditorUi;

    // the field edits the secret but draws no text of its own: its caret and selection show over the mask
    plUiTextInput* pInput = TextField(pParent, rect, sName, sBinding, {}, {}, true);
    pInput->m_TextColor = plColorGammaUB(0, 0, 0, 0);

    // the text field's own padding is 8 px
    const plRectFloat textRect(rect.x + 8.0f, rect.y, plMath::Max(rect.width - 12.0f, 1.0f), rect.height);

    plUiText* pMask = TextAt(pParent, {}, textRect, Foreground(), FontSize - 1, true);
    pMask->m_sTextBinding = sMaskBinding;
    pMask->m_Overflow = plUiTextOverflow::Clip;

    plUiText* pPlaceholder = TextAt(pParent, sPlaceholder, textRect, WithAlpha(Foreground(), 102), FontSize - 1, true);
    pPlaceholder->m_sVisibleBinding = sEmptyBinding;
    return pInput;
  }

  void UpdateSecretMask(plBlackboard& ref_board, plStringView sText, plStringView sMaskBinding, plStringView sEmptyBinding)
  {
    plStringBuilder sMask;
    for (auto it = sText.GetIteratorFront(); it.IsValid(); ++it)
      sMask.Append(GetMaskCharacter());

    ref_board.SetEntryValue(sMaskBinding, plString(sMask));
    ref_board.SetEntryValue(sEmptyBinding, sText.IsEmpty());
  }
} // namespace plGameUiEditor
