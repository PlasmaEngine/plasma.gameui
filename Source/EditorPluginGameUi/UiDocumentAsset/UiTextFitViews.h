#pragma once

#include <EditorUi/Content.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Delegate.h>

class plRTTI;

/// \brief The Size section of the UI document window, engine-drawn: for the selected element its type, the text it
/// shows, Size (Fixed or Fit content), the width limits (Fit content) or the width (Fixed), the overflow of its text
/// and what the text needs against what it has.
///
/// The window keeps the document; the section shows the state it is given and reports edits through its delegates.
class plUiSizeFitView : public plEditorUiContent
{
public:
  struct State
  {
    bool m_bHasElement = false;
    const plRTTI* m_pType = nullptr;
    plString m_sTypeName;
    plString m_sText;        ///< the displayed text of the element or its label; empty without one
    bool m_bFit = false;     ///< SizeMode FitContent
    plVec2 m_vMinSize = plVec2::MakeZero();
    plVec2 m_vMaxSize = plVec2::MakeZero();
    float m_fWidth = 0.0f;   ///< the authored width
    plInt32 m_iOverflow = -1; ///< the text's plUiTextOverflow; -1 without a text
    bool m_bHasNeeds = false;
    float m_fNeeded = 0.0f;    ///< px the text needs
    float m_fAvailable = 0.0f; ///< px its rect has
  };

  plUiSizeFitView();

  void SetState(const State& state);

  /// Size was switched: true for Fit content.
  plDelegate<void(bool)> m_OnSizeModeChanged;

  /// A width limit slider moved (bFinal false while dragging, true when it was let go).
  plDelegate<void(float, float, bool)> m_OnWidthLimitsChanged;

  /// An overflow mode was picked from the menu.
  plDelegate<void(plInt32)> m_OnOverflowChanged;

  float GetHeight() const;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  void BuildDocument();
  void BuildTypeHeader(class plUiElement* pRoot, float fY);
  float BuildRows(class plUiElement* pRoot, float fY);
  void WriteSliderValues();
  void PollSliders();
  void OpenOverflowMenu();

  State m_State;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
  bool m_bDragging = false; ///< a slider value changed during a drag that has not ended yet
  float m_fReportedMin = -1.0f;
  float m_fReportedMax = -1.0f;
  plRectFloat m_OverflowRect = plRectFloat(0, 0, 0, 0);
};

/// \brief The Text fit panel of the UI document window, engine-drawn: every text of the preview that does not fit its
/// rect, would shrink, or uses characters its font lacks, for the previewed language and device. Clicking a row
/// selects the element; missing glyphs offer Add characters.
class plUiTextFitView : public plEditorUiContent
{
public:
  struct Issue
  {
    enum class Severity : plUInt8
    {
      Error,   ///< missing glyphs
      Warning, ///< overflows, clipped, ellipsized
      Info,    ///< shrunk, fits
    };

    Severity m_Severity = Severity::Warning;
    plString m_sTitle;
    plString m_sDetail;
    bool m_bSelected = false;
    bool m_bAddCharacters = false; ///< the row offers Add characters
  };

  plUiTextFitView();

  /// sScope names what was checked ("Deutsch"); empty issues show the empty state.
  void SetIssues(plStringView sScope, plArrayPtr<const Issue> issues);

  plDelegate<void(plUInt32)> m_OnIssueActivated;
  plDelegate<void(plUInt32)> m_OnAddCharacters;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  void BuildDocument();
  void BuildRow(class plUiElement* pList, plUInt32 uiIndex, const Issue& issue, float fWidth);

  plString m_sScope;
  plDynamicArray<Issue> m_Issues;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
};
