#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiAnimationEngineDlgs.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>
#include <GameUiPlugin/Elements/UiAnimationClip.h>

namespace
{
  const char* s_szOk = "Ok";
  const char* s_szCancel = "Cancel";

  const char* s_szClipName = "AddClip.Name";
  const char* s_szClipNameText = "AddClip.Name.Text";
  const char* s_szClipDuration = "AddClip.Duration";
  const char* s_szClipDurationText = "AddClip.Duration.Text";

  const char* s_szTrackElement = "AddTrack.Element";
  const char* s_szTrackElementText = "AddTrack.Element.Text";
  const char* s_szTrackElements = "AddTrack.Elements";
  const char* s_szTrackTarget = "AddTrack.Target";

  const char* s_szNoClipName = "Enter a clip name.";
  const char* s_szBadDuration = "Duration is 0.01 to 3600 s.";
  const char* s_szNoElementName = "Enter an element name.";
  const char* s_szElementToolTip = "Named elements; a free name is allowed (the element may be named later)";
  const char* s_szElementsToolTip = "Pick a value from the list";
  const char* s_szClipNamePlaceholder = "Intro";

  /// The narrowest width at which the form keeps its labels beside the fields (it stacks them on narrower bodies).
  constexpr float s_fWidth = plEditorUiForm::StackWidth + 2.0f * plEditorUiDialogHost::Padding;
  constexpr float s_fLabelWidth = 80.0f;
  constexpr float s_fTop = 12.0f;
  constexpr float s_fBottom = 12.0f;
  constexpr float s_fDurationWidth = 120.0f;
  constexpr float s_fMinDuration = 0.01f;
  constexpr float s_fMaxDuration = 3600.0f;

  /// The targets in the order the dialog lists them, with their translation keys.
  struct TargetInfo
  {
    plUiAnimTarget::Enum m_Target;
    const char* m_szLabel;
  };

  const TargetInfo s_Targets[] = {
    {plUiAnimTarget::OffsetX, "plUiAnimTarget::OffsetX"},
    {plUiAnimTarget::OffsetY, "plUiAnimTarget::OffsetY"},
    {plUiAnimTarget::Width, "plUiAnimTarget::Width"},
    {plUiAnimTarget::Height, "plUiAnimTarget::Height"},
    {plUiAnimTarget::Opacity, "plUiAnimTarget::Opacity"},
    {plUiAnimTarget::Scale, "plUiAnimTarget::Scale"},
    {plUiAnimTarget::Rotation, "plUiAnimTarget::Rotation"},
    {plUiAnimTarget::Tint, "plUiAnimTarget::Tint"},
    {plUiAnimTarget::Fill, "plUiAnimTarget::Fill"},
  };

  /// OK and Cancel; OK is the default button.
  void SetOkCancel(plEditorUiDialogHost& ref_host)
  {
    plEditorUiDialogButton buttons[2];
    buttons[0].m_sId = s_szOk;
    buttons[0].m_sLabel = "OK";
    buttons[0].m_Role = plEditorUiDialogButton::Role::Accept;
    buttons[1].m_sId = s_szCancel;
    buttons[1].m_sLabel = "Cancel";
    buttons[1].m_Role = plEditorUiDialogButton::Role::Reject;
    ref_host.SetButtons(buttons);
  }

  plString ReadField(plEditorUiSurface& ref_surface, const char* szEntry)
  {
    const plVariant value = ref_surface.GetBlackboard().GetEntryValue(plTempHashedString(szEntry));
    return value.IsValid() ? value.ConvertTo<plString>() : plString();
  }

  void WriteField(plEditorUiSurface& ref_surface, const char* szEntry, plStringView sText)
  {
    ref_surface.GetCanvas().SetTextBinding(szEntry, sText);
    ref_surface.GetBlackboard().SetEntryValue(szEntry, plString(sText));
  }

  /// Gives a field the keyboard once the body is built; true when it has it.
  bool FocusField(plEngineDialog& ref_dialog, const char* szField, bool bSelectAll)
  {
    if (ref_dialog.GetBodyRect().width <= 0.0f)
      return false;

    plUiCanvasComponent& canvas = ref_dialog.GetSurface()->GetCanvas();
    if (!canvas.FocusElement(szField, ref_dialog.GetSurface()->GetLogicalSize()))
      return false;

    if (bSelectAll)
      canvas.InjectEditKey(plUiEditKey::SelectAll);

    return true;
  }

  /// A duration: a number, rounded to two decimals, from 0.01 to 3600.
  bool ParseDuration(plStringView sText, float& out_fDuration)
  {
    plStringBuilder sTrimmed = sText;
    sTrimmed.Trim(" \t");

    double fValue = 0.0;
    const char* szEnd = nullptr;
    if (sTrimmed.IsEmpty() || plConversionUtils::StringToFloat(sTrimmed, fValue, &szEnd).Failed())
      return false;

    if (szEnd != sTrimmed.GetData() + sTrimmed.GetElementCount() || !plMath::IsFinite(fValue))
      return false;

    const float fRounded = plMath::Round(static_cast<float>(fValue) * 100.0f) / 100.0f;
    if (fRounded < s_fMinDuration || fRounded > s_fMaxDuration)
      return false;

    out_fDuration = fRounded;
    return true;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plUiAddClipEngineDlg

plUiAddClipEngineDlg::plUiAddClipEngineDlg()
  : plEngineDialog("GameUi.AddClip", plTranslate("GameUi.Animations.DlgClipTitle"))
{
  if (!IsReady())
    return;

  SetOkCancel(GetHost());

  m_sDurationText = "0.80";
  WriteField(*GetSurface(), s_szClipNameText, {});
  WriteField(*GetSurface(), s_szClipDurationText, m_sDurationText);
  Validate();
}

plVec2 plUiAddClipEngineDlg::GetPreferredSize() const
{
  const float fForm = 2.0f * plEditorUi::ControlHeight + plEditorUiForm::RowGap;
  return plVec2(s_fWidth, s_fTop + fForm + s_fBottom + plEditorUiDialogHost::FooterHeight);
}

void plUiAddClipEngineDlg::BuildBody(plUiElement* pBody, const plVec2& vSize)
{
  using namespace plEditorUi;

  const float x = plEditorUiDialogHost::Padding;
  plEditorUiForm form(pBody, plRectFloat(x, s_fTop, vSize.x - 2.0f * x, plMath::Max(vSize.y - s_fTop, 0.0f)), s_fLabelWidth);

  const plRectFloat name = form.Row(plTranslate("GameUi.Animations.DlgName"));
  TextField(pBody, name, s_szClipName, s_szClipNameText, s_szClipNamePlaceholder);
  plGameUiEditor::MarkField(pBody, name, m_bNameMarked ? plEditorUiSeverity::Error : plEditorUiSeverity::None);

  plRectFloat duration = form.Row(plTranslate("GameUi.Animations.DlgDuration"));
  duration.width = plMath::Min(duration.width, s_fDurationWidth);
  NumberField(pBody, duration, s_szClipDuration, s_szClipDurationText, {}, {}, "s");
  plGameUiEditor::MarkField(pBody, duration, m_bDurationMarked ? plEditorUiSeverity::Error : plEditorUiSeverity::None);

  AddBodyToolTips(form.GetToolTips());
}

void plUiAddClipEngineDlg::ReadFields()
{
  const plString sName = ReadField(*GetSurface(), s_szClipNameText);
  const plString sDuration = ReadField(*GetSurface(), s_szClipDurationText);

  if (m_bValidated && sName == m_sName && sDuration == m_sDurationText)
    return;

  m_sName = sName;
  m_sDurationText = sDuration;
  Validate();
}

void plUiAddClipEngineDlg::Validate()
{
  m_bValidated = true;

  // any name that is not empty is taken as typed
  const bool bNameValid = !m_sName.IsEmpty();
  const bool bDurationValid = ParseDuration(m_sDurationText, m_fDuration);

  const plStringView sError = !bNameValid ? plStringView(s_szNoClipName) : (!bDurationValid ? plStringView(s_szBadDuration) : plStringView());
  GetHost().SetValidation(plEditorUiSeverity::Error, sError);
  GetHost().SetButtonEnabled(s_szOk, sError.IsEmpty());

  if (bNameValid == !m_bNameMarked && bDurationValid == !m_bDurationMarked)
    return;

  m_bNameMarked = !bNameValid;
  m_bDurationMarked = !bDurationValid;
  Rebuild();
}

void plUiAddClipEngineDlg::OnUiUpdate()
{
  ReadFields();

  if (m_bFocusOnShow && FocusField(*this, s_szClipName, false))
    m_bFocusOnShow = false;
}

void plUiAddClipEngineDlg::OnButton(plStringView sId)
{
  if (sId == s_szOk)
  {
    // Enter can arrive before the frame that saw the last key
    ReadFields();
    if (!GetHost().IsButtonEnabled(s_szOk))
      return;
  }

  plEngineDialog::OnButton(sId);
}

//////////////////////////////////////////////////////////////////////////
// plUiAddTrackEngineDlg

plUiAddTrackEngineDlg::plUiAddTrackEngineDlg(plArrayPtr<const plString> elementNames, plStringView sPreselect)
  : plEngineDialog("GameUi.AddTrack", plTranslate("GameUi.Animations.DlgTrackTitle"))
{
  m_ElementNames = elementNames;
  m_sElement = sPreselect;

  if (!IsReady())
    return;

  SetOkCancel(GetHost());

  WriteField(*GetSurface(), s_szTrackElementText, m_sElement);
  Validate();
}

plInt32 plUiAddTrackEngineDlg::GetTarget() const
{
  return s_Targets[m_uiTarget].m_Target;
}

plVec2 plUiAddTrackEngineDlg::GetPreferredSize() const
{
  const float fForm = 2.0f * plEditorUi::ControlHeight + plEditorUiForm::RowGap;
  return plVec2(s_fWidth, s_fTop + fForm + s_fBottom + plEditorUiDialogHost::FooterHeight);
}

void plUiAddTrackEngineDlg::BuildBody(plUiElement* pBody, const plVec2& vSize)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();
  const float x = plEditorUiDialogHost::Padding;
  plEditorUiForm form(pBody, plRectFloat(x, s_fTop, vSize.x - 2.0f * x, plMath::Max(vSize.y - s_fTop, 0.0f)), s_fLabelWidth);

  // the named elements behind the chevron; any other name may be typed
  m_ElementRect = form.Row(plTranslate("GameUi.Bindings.DlgElement"), ControlHeight, s_szElementToolTip);
  plGameUiEditor::ComboField(pBody, board, m_ElementRect, s_szTrackElement, s_szTrackElementText, s_szTrackElements, !m_ElementNames.IsEmpty());
  plGameUiEditor::MarkField(pBody, m_ElementRect, m_bElementMarked ? plEditorUiSeverity::Error : plEditorUiSeverity::None);
  SetToolTip(board, s_szTrackElement, s_szElementToolTip);
  SetToolTip(board, s_szTrackElements, s_szElementsToolTip);

  m_TargetRect = form.Row(plTranslate("GameUi.Animations.DlgTarget"));
  Dropdown(pBody, board, m_TargetRect, plTranslate(s_Targets[m_uiTarget].m_szLabel), s_szTrackTarget);

  AddBodyToolTips(form.GetToolTips());
}

void plUiAddTrackEngineDlg::ReadFields()
{
  const plString sElement = ReadField(*GetSurface(), s_szTrackElementText);

  if (m_bValidated && sElement == m_sElement)
    return;

  m_sElement = sElement;
  Validate();
}

void plUiAddTrackEngineDlg::Validate()
{
  m_bValidated = true;

  // any element name that is not empty is taken as typed
  const bool bElementValid = !m_sElement.IsEmpty();
  GetHost().SetValidation(plEditorUiSeverity::Error, bElementValid ? plStringView() : plStringView(s_szNoElementName));
  GetHost().SetButtonEnabled(s_szOk, bElementValid);

  if (bElementValid == !m_bElementMarked)
    return;

  m_bElementMarked = !bElementValid;
  Rebuild();
}

void plUiAddTrackEngineDlg::ShowElementMenu()
{
  plEditorUiMenu menu;
  menu.m_fMinWidth = m_ElementRect.width;

  for (plUInt32 i = 0; i < m_ElementNames.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(m_ElementNames[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = m_ElementNames[i] == m_sElement;
  }

  const plVec2 vPos(GetBodyRect().x + m_ElementRect.x, GetBodyRect().y + m_ElementRect.y + m_ElementRect.height);
  plEditorUiMenuView::Show(*GetSurface(), vPos, menu, [this](plUInt32 uiPicked)
    {
      if (uiPicked >= m_ElementNames.GetCount())
        return;

      WriteField(*GetSurface(), s_szTrackElementText, m_ElementNames[uiPicked]);
      ReadFields(); });
}

void plUiAddTrackEngineDlg::ShowTargetMenu()
{
  plEditorUiMenu menu;
  menu.m_fMinWidth = m_TargetRect.width;

  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_Targets); ++i)
  {
    plEditorUiMenuItem& item = menu.AddAction(plTranslate(s_Targets[i].m_szLabel), i);
    item.m_bCheckable = true;
    item.m_bChecked = i == m_uiTarget;
  }

  const plVec2 vPos(GetBodyRect().x + m_TargetRect.x, GetBodyRect().y + m_TargetRect.y + m_TargetRect.height);
  plEditorUiMenuView::Show(*GetSurface(), vPos, menu, [this](plUInt32 uiPicked)
    {
      if (uiPicked >= PL_ARRAY_SIZE(s_Targets))
        return;

      m_uiTarget = uiPicked;
      Rebuild(); });
}

void plUiAddTrackEngineDlg::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  if (sId == s_szTrackElements)
    ShowElementMenu();
  else if (sId == s_szTrackTarget)
    ShowTargetMenu();
}

void plUiAddTrackEngineDlg::OnUiUpdate()
{
  ReadFields();

  if (m_bFocusOnShow && FocusField(*this, s_szTrackElement, true))
    m_bFocusOnShow = false;
}

void plUiAddTrackEngineDlg::OnButton(plStringView sId)
{
  if (sId == s_szOk)
  {
    // Enter can arrive before the frame that saw the last key
    ReadFields();
    if (!GetHost().IsButtonEnabled(s_szOk))
      return;
  }

  plEngineDialog::OnButton(sId);
}
