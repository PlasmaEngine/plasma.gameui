#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiAddBindingDialog.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetWindow.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>

namespace
{
  const char* s_szOk = "Ok";
  const char* s_szCancel = "Cancel";

  const char* s_szElement = "Bind.Element";
  const char* s_szProperty = "Bind.Property";
  const char* s_szEntry = "Bind.Entry";
  const char* s_szEntryText = "Bind.Entry.Text";
  const char* s_szEntries = "Bind.Entries";

  const char* s_szNoEntry = "Enter a blackboard entry.";
  const char* s_szEntriesToolTip = "Pick a value from the list";

  /// The narrowest width at which the form keeps its labels beside the fields (it stacks them on narrower bodies).
  constexpr float s_fWidth = plEditorUiForm::StackWidth + 2.0f * plEditorUiDialogHost::Padding;
  constexpr float s_fLabelWidth = 120.0f;
  constexpr float s_fTop = 12.0f;
  constexpr float s_fBottom = 12.0f;

  plStringView GetPropertyLabel(plStringView sProperty)
  {
    return plTranslate(plUiDocumentAssetDocumentWindow::GetBindingPropertyDisplayName(sProperty));
  }
} // namespace

plUiAddBindingDialog::plUiAddBindingDialog(plArrayPtr<const Element> elements, plInt32 iPreselect, plArrayPtr<const plString> templateEntries)
  : plEngineDialog("GameUi.AddBinding", plTranslate("GameUi.Bindings.DlgTitle"))
{
  m_Elements = elements;
  m_TemplateEntries = templateEntries;
  m_iElement = (iPreselect >= 0 && iPreselect < static_cast<plInt32>(m_Elements.GetCount())) ? iPreselect : 0;
  RepopulateProperties();

  if (!IsReady())
    return;

  plEditorUiDialogButton buttons[2];
  buttons[0].m_sId = s_szOk;
  buttons[0].m_sLabel = "OK";
  buttons[0].m_Role = plEditorUiDialogButton::Role::Accept;
  buttons[1].m_sId = s_szCancel;
  buttons[1].m_sLabel = "Cancel";
  buttons[1].m_Role = plEditorUiDialogButton::Role::Reject;
  GetHost().SetButtons(buttons);

  GetSurface()->GetCanvas().SetTextBinding(s_szEntryText, {});
  GetSurface()->GetBlackboard().SetEntryValue(s_szEntryText, plString());
  Validate();
}

plString plUiAddBindingDialog::GetBindingProperty() const
{
  return (m_iProperty >= 0 && m_iProperty < static_cast<plInt32>(m_Properties.GetCount())) ? m_Properties[m_iProperty] : plString();
}

plVec2 plUiAddBindingDialog::GetPreferredSize() const
{
  const float fForm = 3.0f * plEditorUi::ControlHeight + 2.0f * plEditorUiForm::RowGap;
  return plVec2(s_fWidth, s_fTop + fForm + s_fBottom + plEditorUiDialogHost::FooterHeight);
}

void plUiAddBindingDialog::RepopulateProperties()
{
  m_Properties.Clear();
  m_iProperty = 0;

  if (m_iElement >= 0 && m_iElement < static_cast<plInt32>(m_Elements.GetCount()) && m_Elements[m_iElement].m_pType != nullptr)
    plUiDocumentAssetDocumentWindow::GetBindablePropertiesForType(m_Elements[m_iElement].m_pType, m_Properties);
}

void plUiAddBindingDialog::BuildBody(plUiElement* pBody, const plVec2& vSize)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();
  const float x = plEditorUiDialogHost::Padding;
  plEditorUiForm form(pBody, plRectFloat(x, s_fTop, vSize.x - 2.0f * x, plMath::Max(vSize.y - s_fTop, 0.0f)), s_fLabelWidth);

  m_ElementRect = form.Row(plTranslate("GameUi.Bindings.DlgElement"));
  const plStringView sElement = m_Elements.IsEmpty() ? plStringView() : m_Elements[m_iElement].m_sLabel.GetView();
  Dropdown(pBody, board, m_ElementRect, sElement, s_szElement);

  m_PropertyRect = form.Row(plTranslate("GameUi.Bindings.DlgProperty"));
  const plString sProperty = GetBindingProperty();
  Dropdown(pBody, board, m_PropertyRect, sProperty.IsEmpty() ? plStringView() : GetPropertyLabel(sProperty), s_szProperty, !m_Properties.IsEmpty());

  // the entry: any name, or one of the template's entries from the chevron; entries read in the XR colour
  m_EntryRect = form.Row(plTranslate("GameUi.Bindings.DlgEntry"));
  plUiTextInput* pEntry = plGameUiEditor::ComboField(pBody, board, m_EntryRect, s_szEntry, s_szEntryText, s_szEntries, !m_TemplateEntries.IsEmpty(), true);
  pEntry->m_TextColor = plGameUiEditor::XrColor();
  plGameUiEditor::MarkField(pBody, m_EntryRect, m_bEntryMarked ? plEditorUiSeverity::Error : plEditorUiSeverity::None);
  SetToolTip(board, s_szEntries, s_szEntriesToolTip);

  AddBodyToolTips(form.GetToolTips());
}

void plUiAddBindingDialog::ReadEntry()
{
  const plVariant value = GetSurface()->GetBlackboard().GetEntryValue(plTempHashedString(s_szEntryText));
  plStringBuilder sEntry = value.IsValid() ? value.ConvertTo<plString>() : plString();
  sEntry.Trim(" \t");

  if (m_bValidated && sEntry == m_sEntry)
    return;

  m_sEntry = sEntry;
  Validate();
}

void plUiAddBindingDialog::Validate()
{
  m_bValidated = true;

  // OK needs an entry and a property; every element has at least the Visible property
  const bool bEntryValid = !m_sEntry.IsEmpty();
  GetHost().SetValidation(plEditorUiSeverity::Error, bEntryValid ? plStringView() : plStringView(s_szNoEntry));
  GetHost().SetButtonEnabled(s_szOk, bEntryValid && !m_Properties.IsEmpty());

  if (bEntryValid == !m_bEntryMarked)
    return;

  m_bEntryMarked = !bEntryValid;
  Rebuild();
}

void plUiAddBindingDialog::ShowElementMenu()
{
  plEditorUiMenu menu;
  menu.m_fMinWidth = m_ElementRect.width;

  for (plUInt32 i = 0; i < m_Elements.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(m_Elements[i].m_sLabel, i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == m_iElement;
  }

  const plVec2 vPos(GetBodyRect().x + m_ElementRect.x, GetBodyRect().y + m_ElementRect.y + m_ElementRect.height);
  plEditorUiMenuView::Show(*GetSurface(), vPos, menu, [this](plUInt32 uiPicked)
    {
      if (uiPicked >= m_Elements.GetCount())
        return;

      m_iElement = static_cast<plInt32>(uiPicked);
      RepopulateProperties();
      Validate();
      Rebuild(); });
}

void plUiAddBindingDialog::ShowPropertyMenu()
{
  plEditorUiMenu menu;
  menu.m_fMinWidth = m_PropertyRect.width;

  for (plUInt32 i = 0; i < m_Properties.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(GetPropertyLabel(m_Properties[i]), i);
    item.m_bCheckable = true;
    item.m_bChecked = static_cast<plInt32>(i) == m_iProperty;
  }

  const plVec2 vPos(GetBodyRect().x + m_PropertyRect.x, GetBodyRect().y + m_PropertyRect.y + m_PropertyRect.height);
  plEditorUiMenuView::Show(*GetSurface(), vPos, menu, [this](plUInt32 uiPicked)
    {
      if (uiPicked >= m_Properties.GetCount())
        return;

      m_iProperty = static_cast<plInt32>(uiPicked);
      Rebuild(); });
}

void plUiAddBindingDialog::ShowEntryMenu()
{
  plEditorUiMenu menu;
  menu.m_fMinWidth = m_EntryRect.width;

  for (plUInt32 i = 0; i < m_TemplateEntries.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(m_TemplateEntries[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = m_TemplateEntries[i] == m_sEntry;
  }

  const plVec2 vPos(GetBodyRect().x + m_EntryRect.x, GetBodyRect().y + m_EntryRect.y + m_EntryRect.height);
  plEditorUiMenuView::Show(*GetSurface(), vPos, menu, [this](plUInt32 uiPicked)
    {
      if (uiPicked >= m_TemplateEntries.GetCount())
        return;

      GetSurface()->GetCanvas().SetTextBinding(s_szEntryText, m_TemplateEntries[uiPicked]);
      GetSurface()->GetBlackboard().SetEntryValue(s_szEntryText, m_TemplateEntries[uiPicked]);
      ReadEntry(); });
}

void plUiAddBindingDialog::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  if (sId == s_szElement)
    ShowElementMenu();
  else if (sId == s_szProperty)
    ShowPropertyMenu();
  else if (sId == s_szEntries)
    ShowEntryMenu();
}

void plUiAddBindingDialog::OnUiUpdate()
{
  ReadEntry();

  // the entry has the keyboard when the dialog opens
  if (m_bFocusOnShow && GetBodyRect().width > 0.0f && GetSurface()->GetCanvas().FocusElement(s_szEntry, GetSurface()->GetLogicalSize()))
    m_bFocusOnShow = false;
}

void plUiAddBindingDialog::OnButton(plStringView sId)
{
  if (sId == s_szOk)
  {
    // Enter can arrive before the frame that saw the last key
    ReadEntry();
    if (!GetHost().IsButtonEnabled(s_szOk))
      return;
  }

  plEngineDialog::OnButton(sId);
}
