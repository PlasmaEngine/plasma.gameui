#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Color8UNorm.h>
#include <Foundation/Math/Vec4.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Delegate.h>
#include <EditorUi/Content.h>

/// \brief The rect transform section above the UI document's property grid, engine-drawn: the 4x4 anchor presets
/// (the current one highlighted), the four offsets (X Y W H, or L T R B on stretched axes) and the anchor summary.
/// Children of stack and grid containers show it dimmed with a hint; without an element it shows an empty state.
class plUiRectTransformView : public plEditorUiContent
{
public:
  struct State
  {
    bool m_bHasElement = false;
    bool m_bInContainer = false; ///< the parent is a stack or grid panel: anchors do nothing
    plVec2 m_vAnchorMin = plVec2::MakeZero();
    plVec2 m_vAnchorMax = plVec2::MakeZero();
    plVec4 m_vOffsets = plVec4::MakeZero();
    plString m_sSummary; ///< "Anchor: Left-Top"
  };

  plUiRectTransformView();

  void SetState(const State& state);

  /// A preset was clicked: column 0..2 left / center / right, 3 stretch; row 0..2 top / middle / bottom, 3 stretch.
  plDelegate<void(plInt8, plInt8)> m_OnAnchorPreset;

  /// The offsets were edited (Enter or focus leaving a field).
  plDelegate<void(const plVec4&)> m_OnOffsetsEdited;

  /// The height the section needs.
  float GetHeight() const;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  /// The section header shows while the view is in no engine window; the engine shell's panel tab names the section.
  float GetHeaderHeight() const;
  void BuildDocument();
  void WriteFieldTexts();
  void CommitFields();

  State m_State;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
  bool m_bFieldFocused = false;
};

/// \brief The Style dock of the UI document window, engine-drawn: which level of the cascade set each visual value of
/// the selected element (element, its UI Style, a state's style), as Property / Value / Source with colour swatches.
class plUiEffectiveStyleView : public plEditorUiContent
{
public:
  struct Row
  {
    enum class Source : plUInt8
    {
      Element,
      Style,
      State,
    };

    plString m_sProperty;
    plString m_sValue;
    plString m_sSource;
    plColorGammaUB m_Swatch = plColorGammaUB(0, 0, 0, 0); ///< alpha 0: no swatch
    Source m_Source = Source::Element;
  };

  plUiEffectiveStyleView();

  /// Nothing to inspect: the empty state.
  void SetEmpty();
  void SetRows(plStringView sElementName, plArrayPtr<const Row> rows);

protected:
  void OnUiUpdate() override;

private:
  void BuildDocument();

  bool m_bEmpty = true;
  plString m_sElementName;
  plDynamicArray<Row> m_Rows;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
};
