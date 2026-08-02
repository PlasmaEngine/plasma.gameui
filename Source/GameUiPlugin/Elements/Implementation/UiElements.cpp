#include <GameUiPlugin/GameUiPluginPCH.h>

#include <GameUiPlugin/Elements/UiElements.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plUiBrushMode, 1)
  PL_ENUM_CONSTANTS(plUiBrushMode::Solid, plUiBrushMode::Image, plUiBrushMode::NineSlice)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiGradientMode, 1)
  PL_ENUM_CONSTANTS(plUiGradientMode::None, plUiGradientMode::Horizontal, plUiGradientMode::Vertical)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiTextHAlign, 1)
  PL_ENUM_CONSTANTS(plUiTextHAlign::Left, plUiTextHAlign::Center, plUiTextHAlign::Right)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiTextVAlign, 1)
  PL_ENUM_CONSTANTS(plUiTextVAlign::Top, plUiTextVAlign::Center, plUiTextVAlign::Bottom)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiTextOverflow, 1)
  PL_ENUM_CONSTANTS(plUiTextOverflow::Overflow, plUiTextOverflow::Clip, plUiTextOverflow::Ellipsis)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiAnimationMode, 1)
  PL_ENUM_CONSTANTS(plUiAnimationMode::Trigger, plUiAnimationMode::Value)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiAnimEasing, 1)
  PL_ENUM_CONSTANTS(plUiAnimEasing::Linear, plUiAnimEasing::EaseIn, plUiAnimEasing::EaseOut, plUiAnimEasing::EaseInOut)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiStateCondition, 1)
  PL_ENUM_CONSTANTS(plUiStateCondition::IsTrue, plUiStateCondition::IsFalse, plUiStateCondition::Equal, plUiStateCondition::NotEqual)
  PL_ENUM_CONSTANTS(plUiStateCondition::Less, plUiStateCondition::LessEqual, plUiStateCondition::Greater, plUiStateCondition::GreaterEqual)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiStateVisibility, 1)
  PL_ENUM_CONSTANTS(plUiStateVisibility::NoChange, plUiStateVisibility::Show, plUiStateVisibility::Hide)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiEventActionType, 1)
  PL_ENUM_CONSTANTS(plUiEventActionType::SetState, plUiEventActionType::Show, plUiEventActionType::Hide, plUiEventActionType::ToggleVisibility, plUiEventActionType::PlayClip)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiEventAction, 1, plRTTIDefaultAllocator<plUiEventAction>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Type", plUiEventActionType, m_Type),
    PL_MEMBER_PROPERTY("Entry", m_sEntry),
    PL_MEMBER_PROPERTY("Value", m_Value)->AddAttributes(new plDefaultValueAttribute(0)),
    PL_MEMBER_PROPERTY("Target", m_sTarget),
    PL_MEMBER_PROPERTY("Clip", m_sClip),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiEventReaction, 1, plRTTIDefaultAllocator<plUiEventReaction>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Event", m_sEvent),
    PL_ARRAY_MEMBER_PROPERTY("Actions", m_Actions)->AddFlags(plPropertyFlags::PointerOwner),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("On {Event}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiElementState, 2, plRTTIDefaultAllocator<plUiElementState>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Entry", m_sEntry),
    PL_ENUM_MEMBER_PROPERTY("Condition", plUiStateCondition, m_Condition),
    PL_MEMBER_PROPERTY("Value", m_fValue),
    PL_MEMBER_PROPERTY("ValueText", m_sValueText),
    PL_ENUM_MEMBER_PROPERTY("Visibility", plUiStateVisibility, m_Visibility),
    PL_RESOURCE_MEMBER_PROPERTY("Style", m_hStyle)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Style")),
    PL_MEMBER_PROPERTY("PlayClip", m_sPlayClip),
    PL_MEMBER_PROPERTY("ExitClip", m_sExitClip),
    PL_MEMBER_PROPERTY("HideDelay", m_fHideDelay)->AddAttributes(new plDefaultValueAttribute(0.0f), new plClampValueAttribute(0.0f, 60.0f), new plSuffixAttribute(" sec")),
    PL_MEMBER_PROPERTY("EnterDuration", m_fEnterDuration)->AddAttributes(new plDefaultValueAttribute(0.2f), new plClampValueAttribute(0.0f, 60.0f), new plSuffixAttribute(" sec")),
    PL_ENUM_MEMBER_PROPERTY("Easing", plUiAnimEasing, m_Easing),
    PL_MEMBER_PROPERTY("Pulse", m_bPulse),
    PL_MEMBER_PROPERTY("PulsePeriod", m_fPulsePeriod)->AddAttributes(new plDefaultValueAttribute(1.1f), new plClampValueAttribute(0.05f, 60.0f), new plSuffixAttribute(" sec")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("State: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiElement, 1, plRTTINoAllocator)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Visible", m_bVisible)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("Opacity", m_fOpacity)->AddAttributes(new plDefaultValueAttribute(1.0f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("AnchorMin", m_vAnchorMin)->AddAttributes(new plDefaultValueAttribute(plVec2(0.5f, 0.5f)), new plClampValueAttribute(plVec2(0.0f), plVec2(1.0f))),
    PL_MEMBER_PROPERTY("AnchorMax", m_vAnchorMax)->AddAttributes(new plDefaultValueAttribute(plVec2(0.5f, 0.5f)), new plClampValueAttribute(plVec2(0.0f), plVec2(1.0f))),
    PL_MEMBER_PROPERTY("Offsets", m_vOffsets)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\x80;\xE2\x96\xB2;\xE2\x96\xB6;\xE2\x96\xBC"), new plDefaultValueAttribute(plVec4(-100.0f, -25.0f, 200.0f, 50.0f)), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("LayoutMargin", m_vLayoutMargin)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\x80;\xE2\x96\xB2;\xE2\x96\xB6;\xE2\x96\xBC"), new plDefaultValueAttribute(plVec4::MakeZero()), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("TransformOrigin", m_vTransformOrigin)->AddAttributes(new plDefaultValueAttribute(plVec2(0.5f))),
    PL_MEMBER_PROPERTY("TransformCenter", m_vTransformCenter)->AddAttributes(new plDefaultValueAttribute(plVec3::MakeZero()), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("TransformTranslation", m_vTransformTranslation)->AddAttributes(new plDefaultValueAttribute(plVec3::MakeZero()), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("TransformRotation", m_vTransformRotation)->AddAttributes(new plDefaultValueAttribute(plVec3::MakeZero()), new plSuffixAttribute("\xC2\xB0")),
    PL_MEMBER_PROPERTY("TransformScale", m_vTransformScale)->AddAttributes(new plDefaultValueAttribute(plVec3(1.0f))),
    PL_MEMBER_PROPERTY("TransformPerspective", m_fTransformPerspective)->AddAttributes(new plDefaultValueAttribute(1000.0f), new plClampValueAttribute(1.0f, 100000.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("VisibleBinding", m_sVisibleBinding),
    PL_RESOURCE_MEMBER_PROPERTY("Style", m_hStyle)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Style")),
    PL_MEMBER_PROPERTY("DefaultFocus", m_bDefaultFocus),
    PL_MEMBER_PROPERTY("NavUp", m_sNavUp),
    PL_MEMBER_PROPERTY("NavDown", m_sNavDown),
    PL_MEMBER_PROPERTY("NavLeft", m_sNavLeft),
    PL_MEMBER_PROPERTY("NavRight", m_sNavRight),
    // tool-managed: EditorLocked via the hierarchy lock column, States via the States
    // panel, Children via hierarchy/palette/canvas
    PL_MEMBER_PROPERTY("EditorLocked", m_bEditorLocked)->AddAttributes(new plHiddenAttribute()),
    PL_ARRAY_MEMBER_PROPERTY("States", m_States)->AddFlags(plPropertyFlags::PointerOwner)->AddAttributes(new plHiddenAttribute()),
    PL_ARRAY_MEMBER_PROPERTY("Children", m_Children)->AddFlags(plPropertyFlags::PointerOwner)->AddAttributes(new plHiddenAttribute()),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiVisualElement, 1, plRTTINoAllocator)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Color", m_Color)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(255, 255, 255, 255))),
    PL_ENUM_MEMBER_PROPERTY("BrushMode", plUiBrushMode, m_BrushMode),
    PL_RESOURCE_MEMBER_PROPERTY("Texture", m_hTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D")),
    PL_MEMBER_PROPERTY("SliceMargins", m_vSliceMargins)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\x80;\xE2\x96\xB2;\xE2\x96\xB6;\xE2\x96\xBC"), new plDefaultValueAttribute(plVec4(16.0f)), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("ClipChildren", m_bClipChildren),
    PL_MEMBER_PROPERTY("CornerRadius", m_vCornerRadius)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\xA4;\xE2\x97\xA5;\xE2\x97\xA2;\xE2\x97\xA3"), new plClampValueAttribute(plVec4(0.0f), plVec4(1000.0f)), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("BorderWidth", m_fBorderWidth)->AddAttributes(new plClampValueAttribute(0.0f, 100.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("BorderColor", m_BorderColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(255, 255, 255, 255))),
    PL_ENUM_MEMBER_PROPERTY("GradientMode", plUiGradientMode, m_GradientMode),
    PL_MEMBER_PROPERTY("GradientColor", m_GradientColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(128, 128, 128, 255))),
    PL_MEMBER_PROPERTY("ShadowSize", m_fShadowSize)->AddAttributes(new plClampValueAttribute(0.0f, 200.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("ShadowOffset", m_vShadowOffset)->AddAttributes(new plVectorLabelsAttribute("\xE2\x96\xB6;\xE2\x96\xBC"), new plDefaultValueAttribute(plVec2(0.0f, 4.0f)), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("ShadowColor", m_ShadowColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(0, 0, 0, 150))),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiPanel, 1, plRTTIDefaultAllocator<plUiPanel>)
{
  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Panel: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiImage, 1, plRTTIDefaultAllocator<plUiImage>)
{
  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Image: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiStackDirection, 1)
  PL_ENUM_CONSTANTS(plUiStackDirection::Vertical, plUiStackDirection::Horizontal)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiStackJustification, 1)
  PL_ENUM_CONSTANTS(plUiStackJustification::Start, plUiStackJustification::Center, plUiStackJustification::End, plUiStackJustification::SpaceBetween)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiStackAlignment, 1)
  PL_ENUM_CONSTANTS(plUiStackAlignment::Stretch, plUiStackAlignment::Start, plUiStackAlignment::Center, plUiStackAlignment::End)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiStackPanel, 2, plRTTIDefaultAllocator<plUiStackPanel>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Direction", plUiStackDirection, m_Direction),
    PL_ENUM_MEMBER_PROPERTY("Justification", plUiStackJustification, m_Justification),
    PL_ENUM_MEMBER_PROPERTY("Alignment", plUiStackAlignment, m_Alignment),
    PL_MEMBER_PROPERTY("Spacing", m_fSpacing)->AddAttributes(new plDefaultValueAttribute(4.0f), new plClampValueAttribute(0.0f, 1000.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("Padding", m_vPadding)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\x80;\xE2\x96\xB2;\xE2\x96\xB6;\xE2\x96\xBC"), new plSuffixAttribute("px")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Stack Panel: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiScrollView, 1, plRTTIDefaultAllocator<plUiScrollView>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("ScrollSpeed", m_fScrollSpeed)->AddAttributes(new plDefaultValueAttribute(40.0f), new plClampValueAttribute(1.0f, 1000.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("ShowScrollbar", m_bShowScrollbar)->AddAttributes(new plDefaultValueAttribute(true)),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Scroll View: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiGridPanel, 1, plRTTIDefaultAllocator<plUiGridPanel>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Columns", m_uiColumns)->AddAttributes(new plDefaultValueAttribute(2), new plClampValueAttribute(1, 64)),
    PL_MEMBER_PROPERTY("RowHeight", m_fRowHeight)->AddAttributes(new plDefaultValueAttribute(50.0f), new plClampValueAttribute(1.0f, 4096.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("Spacing", m_vSpacing)->AddAttributes(new plVectorLabelsAttribute("\xE2\x86\x94;\xE2\x86\x95"), new plDefaultValueAttribute(plVec2(4.0f)), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("Padding", m_vPadding)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\x80;\xE2\x96\xB2;\xE2\x96\xB6;\xE2\x96\xBC"), new plSuffixAttribute("px")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Grid Panel: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiClickEntryAction, 1)
  PL_ENUM_CONSTANTS(plUiClickEntryAction::Toggle, plUiClickEntryAction::SetTrue, plUiClickEntryAction::SetFalse)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiButton, 1, plRTTIDefaultAllocator<plUiButton>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("HoverColor", m_HoverColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(230, 230, 230, 255))),
    PL_MEMBER_PROPERTY("PressedColor", m_PressedColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(170, 170, 170, 255))),
    PL_RESOURCE_MEMBER_PROPERTY("HoverTexture", m_hHoverTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D")),
    PL_RESOURCE_MEMBER_PROPERTY("PressedTexture", m_hPressedTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D")),
    PL_MEMBER_PROPERTY("ClickEvent", m_sClickEvent),
    PL_MEMBER_PROPERTY("ClickEntryBinding", m_sClickEntryBinding),
    PL_ENUM_MEMBER_PROPERTY("ClickEntryAction", plUiClickEntryAction, m_ClickEntryAction),
    PL_MEMBER_PROPERTY("DisabledBinding", m_sDisabledBinding),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Button: {Name} -> {ClickEvent}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiToggle, 1, plRTTIDefaultAllocator<plUiToggle>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("CheckedColor", m_CheckedColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(120, 190, 120, 255))),
    PL_RESOURCE_MEMBER_PROPERTY("CheckedTexture", m_hCheckedTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Toggle: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiSlider, 1, plRTTIDefaultAllocator<plUiSlider>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Min", m_fMin)->AddAttributes(new plDefaultValueAttribute(0.0f)),
    PL_MEMBER_PROPERTY("Max", m_fMax)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_MEMBER_PROPERTY("Value", m_fValue)->AddAttributes(new plDefaultValueAttribute(0.5f)),
    PL_MEMBER_PROPERTY("ValueBinding", m_sValueBinding),
    PL_MEMBER_PROPERTY("FillColor", m_FillColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(80, 140, 220, 255))),
    PL_MEMBER_PROPERTY("HandleColor", m_HandleColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(240, 240, 240, 255))),
    PL_RESOURCE_MEMBER_PROPERTY("HandleTexture", m_hHandleTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Slider: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiProgressBar, 1, plRTTIDefaultAllocator<plUiProgressBar>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("FillAmount", m_fFillAmount)->AddAttributes(new plDefaultValueAttribute(0.5f), new plClampValueAttribute(0.0f, 1.0f)),
    PL_MEMBER_PROPERTY("FillColor", m_FillColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(220, 80, 70, 255))),
    PL_RESOURCE_MEMBER_PROPERTY("FillTexture", m_hFillTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D")),
    PL_MEMBER_PROPERTY("FillAmountBinding", m_sFillAmountBinding),
    PL_MEMBER_PROPERTY("BindingMin", m_fBindingMin)->AddAttributes(new plDefaultValueAttribute(0.0f)),
    PL_MEMBER_PROPERTY("BindingMax", m_fBindingMax)->AddAttributes(new plDefaultValueAttribute(1.0f)),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Progress Bar: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiSubDocument, 2, plRTTIDefaultAllocator<plUiSubDocument>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_RESOURCE_MEMBER_PROPERTY("Document", m_hDocument)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Document")),
    PL_ARRAY_MEMBER_PROPERTY("Reactions", m_Reactions)->AddFlags(plPropertyFlags::PointerOwner)->AddAttributes(new plGroupAttribute("Events / Flow")),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Sub-Document: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiItemList, 1, plRTTIDefaultAllocator<plUiItemList>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_RESOURCE_MEMBER_PROPERTY("ItemTemplate", m_hItemTemplate)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Document")),
    PL_MEMBER_PROPERTY("ItemsBinding", m_sItemsBinding),
    PL_MEMBER_PROPERTY("ItemExtent", m_fItemExtent)->AddAttributes(new plDefaultValueAttribute(60.0f), new plClampValueAttribute(1.0f, 10000.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("SelectedBinding", m_sSelectedBinding),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Item List: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiSpacer, 2, plRTTIDefaultAllocator<plUiSpacer>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Width", m_fWidth)->AddAttributes(new plDefaultValueAttribute(100.0f), new plClampValueAttribute(0.0f, 100000.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("Height", m_fHeight)->AddAttributes(new plDefaultValueAttribute(20.0f), new plClampValueAttribute(0.0f, 100000.0f), new plSuffixAttribute("px")),
    PL_MEMBER_PROPERTY("Expand", m_bExpand)->AddAttributes(new plDefaultValueAttribute(false)),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Spacer: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiTextInput, 1, plRTTIDefaultAllocator<plUiTextInput>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Text", m_sText),
    PL_MEMBER_PROPERTY("TextBinding", m_sTextBinding),
    PL_MEMBER_PROPERTY("Placeholder", m_sPlaceholder),
    PL_RESOURCE_MEMBER_PROPERTY("Font", m_hFont)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Font")),
    PL_MEMBER_PROPERTY("TextColor", m_TextColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(255, 255, 255, 255))),
    PL_MEMBER_PROPERTY("FontSize", m_uiFontSize)->AddAttributes(new plDefaultValueAttribute(16), new plClampValueAttribute(4, 256)),
    PL_MEMBER_PROPERTY("MaxLength", m_uiMaxLength)->AddAttributes(new plClampValueAttribute(0, 4096)),
    PL_MEMBER_PROPERTY("CaretColor", m_CaretColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(6, 250, 201, 255))),
    PL_MEMBER_PROPERTY("SubmitEvent", m_sSubmitEvent),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Text Input: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiText, 1, plRTTIDefaultAllocator<plUiText>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Text", m_sText),
    PL_MEMBER_PROPERTY("TextBinding", m_sTextBinding),
    PL_MEMBER_PROPERTY("Localized", m_bLocalized),
    PL_RESOURCE_MEMBER_PROPERTY("Font", m_hFont)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Font")),
    PL_MEMBER_PROPERTY("TextColor", m_Color)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(255, 255, 255, 255))),
    PL_MEMBER_PROPERTY("FontSize", m_uiFontSize)->AddAttributes(new plDefaultValueAttribute(16), new plClampValueAttribute(4, 256)),
    PL_ENUM_MEMBER_PROPERTY("HAlign", plUiTextHAlign, m_HAlign),
    PL_ENUM_MEMBER_PROPERTY("VAlign", plUiTextVAlign, m_VAlign),
    PL_MEMBER_PROPERTY("Wrap", m_bWrap),
    PL_ENUM_MEMBER_PROPERTY("Overflow", plUiTextOverflow, m_Overflow),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Text: {Name} \"{Text}\""),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiElementState::plUiElementState() = default;
plUiElementState::~plUiElementState() = default;

plUiElement::plUiElement() = default;

plUiElement::~plUiElement()
{
  ClearChildren();

  for (plUiElementState* pState : m_States)
  {
    PL_DEFAULT_DELETE(pState);
  }

  m_States.Clear();
}

void plUiElement::ClearChildren()
{
  for (plUiElement* pChild : m_Children)
  {
    PL_DEFAULT_DELETE(pChild);
  }

  m_Children.Clear();
}

plRectFloat plUiElement::ComputeRect(const plRectFloat& parentRect) const
{
  // spacers size themselves via explicit Width/Height (point-anchored axes)
  float fPointSizeX = m_vOffsets.z;
  float fPointSizeY = m_vOffsets.w;

  if (const plUiSpacer* pSpacer = plDynamicCast<const plUiSpacer*>(this))
  {
    fPointSizeX = pSpacer->m_fWidth;
    fPointSizeY = pSpacer->m_fHeight;
  }

  const float fAnchorMinX = parentRect.x + m_vAnchorMin.x * parentRect.width;
  const float fAnchorMinY = parentRect.y + m_vAnchorMin.y * parentRect.height;

  const float x0 = fAnchorMinX + m_vOffsets.x;
  const float y0 = fAnchorMinY + m_vOffsets.y;

  float x1;
  if (m_vAnchorMax.x == m_vAnchorMin.x)
  {
    x1 = x0 + fPointSizeX; // point anchor: z is the width
  }
  else
  {
    x1 = parentRect.x + m_vAnchorMax.x * parentRect.width + m_vOffsets.z;
  }

  float y1;
  if (m_vAnchorMax.y == m_vAnchorMin.y)
  {
    y1 = y0 + fPointSizeY; // point anchor: w is the height
  }
  else
  {
    y1 = parentRect.y + m_vAnchorMax.y * parentRect.height + m_vOffsets.w;
  }

  return plRectFloat(x0, y0, plMath::Max(x1 - x0, 0.0f), plMath::Max(y1 - y0, 0.0f));
}

plUiVisualElement::plUiVisualElement() = default;
plUiVisualElement::~plUiVisualElement() = default;

plUiPanel::plUiPanel() = default;
plUiPanel::~plUiPanel() = default;

plUiImage::plUiImage()
{
  m_BrushMode = plUiBrushMode::Image;
}

plUiImage::~plUiImage() = default;

plUiStackPanel::plUiStackPanel() = default;
plUiStackPanel::~plUiStackPanel() = default;

plUiScrollView::plUiScrollView() = default;
plUiScrollView::~plUiScrollView() = default;

plUiGridPanel::plUiGridPanel() = default;
plUiGridPanel::~plUiGridPanel() = default;

plUiButton::plUiButton() = default;
plUiButton::~plUiButton() = default;

plUiToggle::plUiToggle() = default;
plUiToggle::~plUiToggle() = default;

plUiSlider::plUiSlider() = default;
plUiSlider::~plUiSlider() = default;

plUiProgressBar::plUiProgressBar() = default;
plUiProgressBar::~plUiProgressBar() = default;

plUiSubDocument::plUiSubDocument() = default;
plUiSubDocument::~plUiSubDocument()
{
  for (plUiEventReaction* pReaction : m_Reactions)
  {
    PL_DEFAULT_DELETE(pReaction);
  }
  m_Reactions.Clear();
}

plUiEventReaction::plUiEventReaction() = default;
plUiEventReaction::~plUiEventReaction()
{
  for (plUiEventAction* pAction : m_Actions)
  {
    PL_DEFAULT_DELETE(pAction);
  }
  m_Actions.Clear();
}

plUiItemList::plUiItemList() = default;
plUiItemList::~plUiItemList() = default;

plUiSpacer::plUiSpacer() = default;
plUiSpacer::~plUiSpacer() = default;

plUiTextInput::plUiTextInput() = default;
plUiTextInput::~plUiTextInput() = default;

plUiText::plUiText() = default;
plUiText::~plUiText() = default;

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Elements_Implementation_UiElements);
