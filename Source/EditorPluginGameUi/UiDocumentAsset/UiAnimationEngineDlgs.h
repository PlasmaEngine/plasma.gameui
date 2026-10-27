#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <GuiFoundation/EngineUi/EngineDialog.h>

/// \brief Add Animation Clip drawn by the engine: the clip's name and its duration (0.01 to 3600 s, 0.80 s by
/// default). OK stays disabled until the clip has a name and the duration is in range; the validation line says why.
/// Triggers and loop mode are edited in the property grid afterwards.
class plUiAddClipEngineDlg : public plEngineDialog
{
public:
  plUiAddClipEngineDlg();

  /// The name as typed.
  const plString& GetName() const { return m_sName; }

  /// The duration in seconds, rounded to two decimals.
  float GetDuration() const { return m_fDuration; }

protected:
  void BuildBody(plUiElement* pBody, const plVec2& vSize) override;
  void OnButton(plStringView sId) override;
  void OnUiUpdate() override;
  plVec2 GetPreferredSize() const override;

private:
  void ReadFields();
  void Validate();

  plString m_sName;
  plString m_sDurationText;
  float m_fDuration = 0.8f;
  bool m_bValidated = false;
  bool m_bNameMarked = false;
  bool m_bDurationMarked = false;
  bool m_bFocusOnShow = true;
};

/// \brief Add Track drawn by the engine: the animated element by name and the target channel. The chevron offers the
/// named elements; any name is allowed, because the element may be named later. OK stays disabled without an element
/// name.
class plUiAddTrackEngineDlg : public plEngineDialog
{
public:
  /// sPreselect: the name the element field starts with (the selected element's name, or empty).
  plUiAddTrackEngineDlg(plArrayPtr<const plString> elementNames, plStringView sPreselect);

  /// The element name as typed or picked.
  const plString& GetElementName() const { return m_sElement; }

  /// The plUiAnimTarget value.
  plInt32 GetTarget() const;

protected:
  void BuildBody(plUiElement* pBody, const plVec2& vSize) override;
  void OnButton(plStringView sId) override;
  void OnUiEvent(const plMsgUiEvent& event) override;
  void OnUiUpdate() override;
  plVec2 GetPreferredSize() const override;

private:
  void ReadFields();
  void Validate();
  void ShowElementMenu();
  void ShowTargetMenu();

  plDynamicArray<plString> m_ElementNames;
  plString m_sElement;
  plUInt32 m_uiTarget = 0; ///< index into the target list
  plRectFloat m_ElementRect = plRectFloat(0, 0, 0, 0);
  plRectFloat m_TargetRect = plRectFloat(0, 0, 0, 0);
  bool m_bValidated = false;
  bool m_bElementMarked = false;
  bool m_bFocusOnShow = true;
};
