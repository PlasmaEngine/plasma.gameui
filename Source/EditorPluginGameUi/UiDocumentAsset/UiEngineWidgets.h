#pragma once

#include <EditorUi/Controls.h>
#include <Foundation/Math/Rect.h>

class plEditorUiSurface;
class plRTTI;
class plUiDrawListWriter;

/// \brief Builders shared by the engine-drawn panels and dialogs of the Game UI editors: the package's line icons, the
/// system colours the panels use and the few controls the toolkit has no builder for (dropdown fields, icon toggles,
/// check boxes, chips, table headers, fields with a value list, secret fields and field marks).
namespace plGameUiEditor
{
  /// Path of a package line icon by name ("Pointer"), for engine UI images.
  plString IconPath(plStringView sName);

  /// The line icon of an element type, as the palette and the hierarchy show it.
  plString TypeIconPath(const plRTTI* pType);

  /// System colours: UI for elements and styles, XR for blackboard entries, Scripting for states.
  plColorGammaUB UiColor();
  plColorGammaUB XrColor();
  plColorGammaUB ScriptingColor();

  /// a + (b - a) * fAmount, per channel, opaque.
  plColorGammaUB Mix(plColorGammaUB a, plColorGammaUB b, float fAmount);
  plColorGammaUB WithAlpha(plColorGammaUB color, plUInt8 uiAlpha);

  /// The colour of a "connected" status dot: the theme green blended towards the text colour.
  plColorGammaUB ConnectedColor();

  /// Places an element at rect, in offsets from its parent's top left.
  void Place(plUiElement* pElement, const plRectFloat& rect);

  /// Height of a toolbar row (style pass: 30 px with 24 px controls).
  inline constexpr float ToolRowHeight = 30.0f;

  /// Height of a table header row.
  inline constexpr float TableHeaderHeight = 26.0f;

  /// A toolbar row at y: the surface colour with a hairline at its bottom (bTopLine: at its top instead).
  plUiPanel* ToolRow(plUiElement* pParent, float y, bool bTopLine = false);

  /// An empty state filling pParent: a package icon, a line and an optional second line.
  plUiPanel* EmptyState(plUiElement* pParent, plStringView sIconPath, plStringView sTitle, plStringView sLine = {});

  /// A section header whose title (uppercase) is followed by a dim suffix that keeps its case ("· Settings").
  plUiPanel* SectionHeader(plUiElement* pParent, plStringView sTitle, plStringView sSuffix, plColorGammaUB color);

  /// Width of a text button with an optional leading icon.
  float TextButtonWidth(plStringView sText, bool bIcon);

  /// A text button (plate with a hairline border) with an optional leading icon; sDisabledBinding dims it.
  plUiButton* TextButton(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, plStringView sIconPath = {}, plStringView sDisabledBinding = {});

  /// A borderless icon toggle: muted at rest, the accent icon on the selection plate while sOnBinding is true.
  /// sOnBinding and sOffBinding must hold opposite values.
  plUiButton* IconToggle(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sIconPath, plStringView sOnBinding, plStringView sOffBinding);

  /// A dropdown field: the field well with a hairline border, an optional dim lead word or icon, the choice and a
  /// chevron. Clicking raises sName. bEnabled false dims it and ignores clicks.
  plUiButton* Dropdown(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sLead, plStringView sLeadIcon, plStringView sText, bool bEnabled = true);

  /// A check box (style pass D9) centred in rect: a field well, or the selection fill with the accent tick while
  /// sOnBinding is true. Clicking raises sName.
  plUiButton* CheckBox(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sOnBinding, plStringView sDisabledBinding = {});

  /// Width of a segmented control with these labels.
  float SegmentedWidth(plArrayPtr<const plString> labels);

  /// A segmented control: joined text segments in a field well with a hairline border, the selected one (if any) on
  /// the selection fill in the text colour, the others muted. Clicking segment i raises "<sNamePrefix>.<i>".
  void Segmented(plUiElement* pParent, const plRectFloat& rect, plStringView sNamePrefix, plArrayPtr<const plString> labels, plInt32 iSelected);

  /// The click name of segment uiIndex of a segmented control.
  plString SegmentName(plStringView sNamePrefix, plUInt32 uiIndex);

  /// Width of a chip with this text.
  float ChipWidth(plStringView sText);

  /// A chip (scenario, state): a pill on the field colour; bOn shows it on the selection fill with an accent border.
  /// edgeColor (alpha > 0) draws a 3 px edge on its left.
  plUiButton* Chip(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sText, bool bOn, plColorGammaUB edgeColor = plColorGammaUB(0, 0, 0, 0));

  /// A table column for TableHeader: its title and its share of the width (or a fixed width when fWidth > 1).
  struct Column
  {
    plStringView m_sTitle;
    float m_fWidth = 0.0f;
    bool m_bRightAligned = false;
  };

  /// Resolves the column widths for a table width: fixed widths first, fractions share the rest.
  void LayoutColumns(plArrayPtr<const Column> columns, float fWidth, plDynamicArray<float>& out_lefts, plDynamicArray<float>& out_widths);

  /// A table header row at y: uppercase titles in the dim header face over a hairline.
  plUiPanel* TableHeader(plUiElement* pParent, float y, plArrayPtr<const Column> columns, plArrayPtr<const float> lefts, plArrayPtr<const float> widths);

  /// A text in a table cell or row: padded 8 px, vertically centred, cut with an ellipsis.
  plUiText* CellText(plUiElement* pParent, const plRectFloat& rect, plStringView sText, plColorGammaUB color, bool bMono = false, plStringView sBinding = {});

  /// An outline over a dialog field the validation line refers to, in the severity's colour; nothing for None.
  void MarkField(plUiElement* pParent, const plRectFloat& rect, plEditorUiSeverity severity);

  /// A text field whose text is the string entry sBinding, with a chevron button at its right that raises sPickAction
  /// (the host offers values in a menu). Without bPick the field takes the whole rect and has no chevron.
  plUiTextInput* ComboField(plUiElement* pParent, plBlackboard& ref_board, const plRectFloat& rect, plStringView sName, plStringView sBinding, plStringView sPickAction, bool bPick, bool bCode = false);

  /// A text field for a secret (an access token): typing and editing work as in a text field on the string entry
  /// sBinding, but the field shows one mask character per character, read from sMaskBinding, and the placeholder while
  /// sEmptyBinding is true. UpdateSecretMask writes both entries. The field uses the code face, so the caret and the
  /// selection line up with the mask.
  plUiTextInput* SecretField(plUiElement* pParent, const plRectFloat& rect, plStringView sName, plStringView sBinding, plStringView sMaskBinding, plStringView sEmptyBinding, plStringView sPlaceholder);

  /// Writes the mask and the empty state of a secret field for its text.
  void UpdateSecretMask(plBlackboard& ref_board, plStringView sText, plStringView sMaskBinding, plStringView sEmptyBinding);
} // namespace plGameUiEditor
