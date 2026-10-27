#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <GuiFoundation/EngineUi/EngineDialog.h>

class plRTTI;

/// \brief The Add Binding dialog drawn by the engine: the element (seeded from the selection), the binding property
/// (filtered by the element's type) and the blackboard entry (the template's entries behind the chevron, any name
/// allowed). OK stays disabled until there is an entry; the validation line says why.
class plUiAddBindingDialog : public plEngineDialog
{
public:
  struct Element
  {
    plString m_sLabel;
    const plRTTI* m_pType = nullptr;
  };

  plUiAddBindingDialog(plArrayPtr<const Element> elements, plInt32 iPreselect, plArrayPtr<const plString> templateEntries);

  plInt32 GetElementIndex() const { return m_iElement; }
  plString GetBindingProperty() const;

  /// The entry without surrounding spaces.
  const plString& GetEntry() const { return m_sEntry; }

protected:
  void BuildBody(plUiElement* pBody, const plVec2& vSize) override;
  void OnButton(plStringView sId) override;
  void OnUiEvent(const plMsgUiEvent& event) override;
  void OnUiUpdate() override;
  plVec2 GetPreferredSize() const override;

private:
  void RepopulateProperties();
  void ReadEntry();
  void Validate();
  void ShowElementMenu();
  void ShowPropertyMenu();
  void ShowEntryMenu();

  plDynamicArray<Element> m_Elements;
  plDynamicArray<plString> m_TemplateEntries;
  plDynamicArray<plString> m_Properties;
  plInt32 m_iElement = 0;
  plInt32 m_iProperty = 0;
  plString m_sEntry;
  plRectFloat m_ElementRect = plRectFloat(0, 0, 0, 0);
  plRectFloat m_PropertyRect = plRectFloat(0, 0, 0, 0);
  plRectFloat m_EntryRect = plRectFloat(0, 0, 0, 0);
  bool m_bValidated = false;
  bool m_bEntryMarked = false;
  bool m_bFocusOnShow = true;
};
