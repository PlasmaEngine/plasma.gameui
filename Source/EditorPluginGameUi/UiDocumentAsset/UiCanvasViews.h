#pragma once

#include <EditorUi/Content.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Delegate.h>

#include <functional>

/// \brief The canvas toolbar of the UI document window, engine-drawn: the Device and Zoom dropdowns, the language strip
/// (the project's languages and the pseudo-localization), Fit all buttons, then icon toggles for !Tr, Select /
/// Anchors, Interact, Snap, Safe Area, Nav and Checker. The labels of the toggles are in their tooltips.
///
/// The window keeps the state; the toolbar shows it and reports clicks through m_OnChanged (control, value). The
/// language strip reports the clicked segment, or -1 when the selected one is clicked again (authored texts).
class plUiCanvasToolBarView : public plEditorUiContent
{
public:
  enum class Control : plUInt8
  {
    Device,
    Zoom,
    Language,
    FitAll,
    HighlightUntranslated,
    SelectMode,
    AnchorsMode,
    Interact,
    Snap,
    SafeArea,
    NavGraph,
    Checker,
    Count
  };

  plUiCanvasToolBarView();

  /// Height of the toolbar row.
  static constexpr float Height = 30.0f;

  void SetDevice(plArrayPtr<const plString> choices, plInt32 iSelected);
  void SetZoom(plArrayPtr<const plString> choices, plInt32 iSelected, bool bEnabled);

  /// The language strip's segments and the selected one (-1: none, the authored texts show).
  void SetLanguage(plArrayPtr<const plString> choices, plInt32 iSelected);
  void SetChecked(Control control, bool bChecked);

  /// A dropdown reports the picked index, a toggle 1 or 0.
  plDelegate<void(Control, plInt32)> m_OnChanged;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  struct Choices
  {
    plDynamicArray<plString> m_Items;
    plInt32 m_iSelected = 0;
    bool m_bEnabled = true;
  };

  void BuildDocument();
  void OpenMenu(Control control);
  void WriteToggleState(Control control);
  Choices& GetChoices(Control control);
  bool HandleLanguageClick(plStringView sIdentifier);

  Choices m_Device;
  Choices m_Zoom;
  Choices m_Language;
  bool m_Checked[static_cast<int>(Control::Count)] = {};
  plRectFloat m_DropdownRects[3];
  float m_fBuiltWidth = -1.0f;
  bool m_bRebuild = true;
};

/// \brief A ruler beside the canvas, engine-drawn: canvas-space pixel coordinates with a labelled major tick at least
/// 60 px apart and minor ticks every quarter step. It follows the canvas transform, which m_GetTransform returns
/// (canvas to widget scale, canvas origin along the ruler in widget units).
///
/// The horizontal ruler can lead with the square that sits over the vertical one.
class plUiRulerView : public plEditorUiContent
{
public:
  plUiRulerView(bool bHorizontal, float fCorner = 0.0f);

  static constexpr float Thickness = 20.0f;

  std::function<void(float&, float&)> m_GetTransform;

protected:
  void OnUiUpdate() override;

private:
  void Draw();

  bool m_bHorizontal = true;
  float m_fCorner = 0.0f;
  bool m_bBuilt = false;
  plVec2 m_vDrawnSize = plVec2::MakeZero();
  float m_fDrawnScale = -1.0f;
  float m_fDrawnOrigin = 0.0f;
};

/// \brief The canvas status row, engine-drawn: the engine preview state with its dot on the left; the selected
/// element's name, type and rect, and the preview's draw statistics on the right.
class plUiCanvasStatusRow : public plEditorUiContent
{
public:
  plUiCanvasStatusRow();

  /// The row's height in logical units.
  static float GetHeight();

  void SetEngineState(bool bConnected, plStringView sText);

  /// The selected element ("Settings (Button)") and its rect, or a message when sRect is empty. Empty hides it.
  void SetSelection(plStringView sElement, plStringView sPosition, plStringView sSize);
  void SetMessage(plStringView sMessage);

  /// "draw: N batches · M quads".
  void SetDrawStats(plStringView sBatches, plStringView sQuads);

  /// The Text fit summary on the left after the engine state: "3 overflowing · 2 fit to content"; uiOverflowing in the
  /// warning colour. Both 0 hide it.
  void SetTextFitSummary(plUInt32 uiOverflowing, plUInt32 uiFitting);

protected:
  void OnUiUpdate() override;

private:
  void Draw();

  bool m_bConnected = true;
  plString m_sEngineText;
  plString m_sElement;
  plString m_sPosition;
  plString m_sSize;
  plString m_sMessage;
  plString m_sBatches;
  plString m_sQuads;
  plUInt32 m_uiOverflowing = 0;
  plUInt32 m_uiFitting = 0;
  bool m_bBuilt = false;
  bool m_bDirty = true;
  plVec2 m_vDrawnSize = plVec2::MakeZero();
};
