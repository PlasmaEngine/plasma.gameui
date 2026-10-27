#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiImport/UiImportFigmaEngineDlg.h>
#include <EditorUi/Surface.h>
#include <EditorUi/SettingsFile.h>
#include <EditorUi/Tree.h>
#include <GuiFoundation/Dialogs/FileDialog.h>
#include <ToolsFoundation/Project/ToolsProject.h>

namespace
{
  const char* s_szImport = "Import";
  const char* s_szCancel = "Cancel";

  const char* s_szUrl = "Figma.Url";
  const char* s_szUrlText = "Figma.Url.Text";
  const char* s_szToken = "Figma.Token";
  const char* s_szTokenText = "Figma.Token.Text";
  const char* s_szTokenMask = "Figma.Token.Mask";
  const char* s_szTokenEmpty = "Figma.Token.Empty";
  const char* s_szFolder = "Figma.Folder";
  const char* s_szFolderText = "Figma.Folder.Text";
  const char* s_szBrowse = "Figma.Browse";
  const char* s_szFetch = "Figma.Fetch";
  const char* s_szStatus = "Figma.Status";
  const char* s_szFrames = "Figma.Frames";
  const char* s_szFrameRow = "Figma.Frames.row";

  const char* s_szNoFolder = "Choose an output folder inside the project first.";
  const char* s_szNoFrames = "No frames checked.";

  constexpr float s_fWidth = 640.0f;
  constexpr float s_fHeight = 520.0f;
  constexpr float s_fLabelWidth = 120.0f;
  constexpr float s_fTop = 12.0f;
  constexpr float s_fBottom = 12.0f;
  constexpr float s_fBrowseWidth = 26.0f;
  constexpr float s_fStatusHeight = 36.0f; ///< two lines

  /// Where a frame row's check box is, from the row's left edge at depth 0 (the tree's icon slot); rows indent by 14.
  constexpr float s_fCheckBoxLeft = 16.0f;
  constexpr float s_fCheckBoxRight = 40.0f;
  constexpr float s_fIndentStep = 14.0f;

  /// Pages and frames share the item ids: frames are their index + 1, pages have the high bit set.
  constexpr plUInt64 s_uiPageBit = 1ull << 32;

  bool IsPageItem(plUInt64 uiItem) { return (uiItem & s_uiPageBit) != 0; }
  plUInt64 ToPageItem(plUInt32 uiPage) { return s_uiPageBit | uiPage; }
  plUInt64 ToFrameItem(plUInt32 uiFrame) { return static_cast<plUInt64>(uiFrame) + 1; }
  plUInt32 ToIndex(plUInt64 uiItem) { return IsPageItem(uiItem) ? static_cast<plUInt32>(uiItem & ~s_uiPageBit) : static_cast<plUInt32>(uiItem - 1); }

  // the user's last URL, token and folder (editor settings)
  constexpr const char* s_szSettingUrl = "FigmaImport/LastUrl";
  constexpr const char* s_szSettingToken = "FigmaImport/Token";
  constexpr const char* s_szSettingFolder = "FigmaImport/LastFolder";
} // namespace

//////////////////////////////////////////////////////////////////////////
// Frames by page: a page row per page, a frame row with a check box per frame

class plUiFigmaImportEngineDlg::FrameAdapter : public plEditorUiTreeAdapter
{
public:
  FrameAdapter(plUiFigmaImportEngineDlg& ref_dialog)
    : m_Dialog(ref_dialog)
  {
  }

  void GetChildren(plUInt64 uiParent, plDynamicArray<plUInt64>& out_children) const override
  {
    if (uiParent == 0)
    {
      for (plUInt32 i = 0; i < m_Dialog.m_Pages.GetCount(); ++i)
        out_children.PushBack(ToPageItem(i));

      return;
    }

    if (!IsPageItem(uiParent))
      return;

    const plString& sPage = m_Dialog.m_Pages[ToIndex(uiParent)];
    const auto frames = m_Dialog.m_Session.GetFrames();
    for (plUInt32 i = 0; i < frames.GetCount(); ++i)
    {
      if (frames[i].m_sPage == sPage)
        out_children.PushBack(ToFrameItem(i));
    }
  }

  void GetRow(plUInt64 uiItem, plEditorUiTreeRow& out_row) const override
  {
    const plUInt32 uiIndex = ToIndex(uiItem);
    if (IsPageItem(uiItem))
    {
      out_row.m_sText = m_Dialog.m_Pages[uiIndex];
      return;
    }

    const bool bChecked = m_Dialog.m_Checked[uiIndex];
    out_row.m_sText = m_Dialog.m_Session.GetFrames()[uiIndex].m_sName;
    out_row.m_sIcon = plGameUiEditor::IconPath(bChecked ? "CheckBoxOn" : "CheckBoxOff");
    out_row.m_IconColor = bChecked ? plEditorUi::Accent() : plEditorUi::Border();
  }

  plUInt64 GetParent(plUInt64 uiItem) const override
  {
    if (IsPageItem(uiItem))
      return 0;

    const plString& sPage = m_Dialog.m_Session.GetFrames()[ToIndex(uiItem)].m_sPage;
    for (plUInt32 i = 0; i < m_Dialog.m_Pages.GetCount(); ++i)
    {
      if (m_Dialog.m_Pages[i] == sPage)
        return ToPageItem(i);
    }

    return 0;
  }

private:
  plUiFigmaImportEngineDlg& m_Dialog;
};

//////////////////////////////////////////////////////////////////////////
// plUiFigmaImportEngineDlg

plUiFigmaImportEngineDlg::plUiFigmaImportEngineDlg()
  : plEngineDialog("GameUi.FigmaImport", "Import UI from Figma")
{
  if (!IsReady())
    return;

  plEditorUiDialogButton buttons[2];
  buttons[0].m_sId = s_szImport;
  buttons[0].m_sLabel = "Import Selected";
  buttons[0].m_Role = plEditorUiDialogButton::Role::Accept;
  buttons[1].m_sId = s_szCancel;
  buttons[1].m_sLabel = "Cancel";
  buttons[1].m_Role = plEditorUiDialogButton::Role::Reject;
  GetHost().SetButtons(buttons);

  plEditorUiSurface& surface = *GetSurface();

  // subscribed before the tree exists, so Space toggles a check box before the tree sees the key
  m_KeySubscription = surface.m_KeyEvents.AddEventHandler(plMakeDelegate(&plUiFigmaImportEngineDlg::OnKey, this));
  m_PointerSubscription = surface.m_PointerEvents.AddEventHandler(plMakeDelegate(&plUiFigmaImportEngineDlg::OnPointer, this));

  m_pAdapter = PL_DEFAULT_NEW(FrameAdapter, *this);
  m_pTree = PL_DEFAULT_NEW(plEditorUiTree, surface, s_szFrames, *m_pAdapter);

  // the last URL, token and folder of this user; the folder defaults to the project directory
  const plEditorUiSettingsFile& settings = plEditorUiSettingsFile::GetCurrent();
  plString sFolder = settings.GetValue(s_szSettingFolder);
  if (sFolder.IsEmpty() && plToolsProject::GetSingleton() != nullptr && plToolsProject::GetSingleton()->IsProjectOpen())
    sFolder = plToolsProject::GetSingleton()->GetProjectDirectory();

  WriteField(s_szUrlText, settings.GetValue(s_szSettingUrl));
  WriteField(s_szTokenText, settings.GetValue(s_szSettingToken));
  WriteField(s_szFolderText, sFolder);
  surface.GetBlackboard().SetEntryValue(s_szStatus, plString());

  ReadFields();
}

plUiFigmaImportEngineDlg::~plUiFigmaImportEngineDlg()
{
  if (!IsReady())
    return;

  m_pTree.Clear();
  GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
  GetSurface()->m_KeyEvents.RemoveEventHandler(m_KeySubscription);
}

plVec2 plUiFigmaImportEngineDlg::GetPreferredSize() const
{
  return plVec2(s_fWidth, s_fHeight);
}

plString plUiFigmaImportEngineDlg::ReadField(const char* szEntry) const
{
  const plVariant value = GetSurface()->GetBlackboard().GetEntryValue(plTempHashedString(szEntry));
  return value.IsValid() ? value.ConvertTo<plString>() : plString();
}

void plUiFigmaImportEngineDlg::WriteField(const char* szEntry, plStringView sText)
{
  GetSurface()->GetCanvas().SetTextBinding(szEntry, sText);
  GetSurface()->GetBlackboard().SetEntryValue(szEntry, plString(sText));
}

void plUiFigmaImportEngineDlg::ReadFields()
{
  const plString sUrl = ReadField(s_szUrlText);
  const plString sToken = ReadField(s_szTokenText);

  plStringBuilder sFolder = ReadField(s_szFolderText);
  sFolder.Trim(" \t\r\n");

  if (m_bFieldsRead && sUrl == m_sUrl && sToken == m_sToken && sFolder == m_sFolder)
    return;

  if (!m_bFieldsRead || sToken != m_sToken)
    plGameUiEditor::UpdateSecretMask(GetSurface()->GetBlackboard(), sToken, s_szTokenMask, s_szTokenEmpty);

  m_bFieldsRead = true;
  m_sUrl = sUrl;
  m_sToken = sToken;
  m_sFolder = sFolder;
  UpdateState();
}

plUInt32 plUiFigmaImportEngineDlg::GetCheckedCount() const
{
  plUInt32 uiChecked = 0;
  for (bool bChecked : m_Checked)
    uiChecked += bChecked ? 1 : 0;

  return uiChecked;
}

void plUiFigmaImportEngineDlg::UpdateState()
{
  // before a fetch found frames Import is only disabled; after it the folder and the check boxes decide
  const bool bFetched = !m_Checked.IsEmpty();
  const bool bNoFolder = bFetched && m_sFolder.IsEmpty();

  plStringView sError;
  if (bNoFolder)
    sError = s_szNoFolder;
  else if (bFetched && GetCheckedCount() == 0)
    sError = s_szNoFrames;

  GetHost().SetValidation(plEditorUiSeverity::Error, sError);
  GetHost().SetButtonEnabled(s_szImport, bFetched && sError.IsEmpty() && !m_bBusy);
  GetHost().SetButtonEnabled(s_szCancel, !m_bBusy);

  plBlackboard& board = GetSurface()->GetBlackboard();
  plEditorUi::WriteEnabled(board, s_szFetch, !m_bBusy);
  plEditorUi::WriteEnabled(board, s_szBrowse, !m_bBusy);

  if (bNoFolder != m_bFolderMarked)
  {
    m_bFolderMarked = bNoFolder;
    Rebuild();
  }
}

void plUiFigmaImportEngineDlg::SetStatus(plStringView sText)
{
  GetSurface()->GetBlackboard().SetEntryValue(s_szStatus, plString(sText));
  RequestRedraw();
}

void plUiFigmaImportEngineDlg::SetBusy(bool bBusy)
{
  m_bBusy = bBusy;
  UpdateState();
  RequestRedraw();
}

void plUiFigmaImportEngineDlg::BuildFolderRow(plUiElement* pBody, const plRectFloat& rect)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();

  const plRectFloat field(rect.x, rect.y, rect.width - s_fBrowseWidth - 4.0f, rect.height);
  TextField(pBody, field, s_szFolder, s_szFolderText, {}, {}, true);
  plGameUiEditor::MarkField(pBody, field, m_bFolderMarked ? plEditorUiSeverity::Error : plEditorUiSeverity::None);

  PushButton(pBody, board, plRectFloat(rect.Right() - s_fBrowseWidth, rect.y + 1.0f, s_fBrowseWidth, rect.height - 2.0f), "...", s_szBrowse, !m_bBusy);
  SetToolTip(board, s_szBrowse, "Choose Output Folder");
}

void plUiFigmaImportEngineDlg::BuildFrameList(plUiElement* pBody, const plRectFloat& rect)
{
  using namespace plEditorUi;

  // a hairline frame around the header and the tree
  plUiPanel* pFrame = Panel(pBody);
  SetRect(pFrame, rect);
  pFrame->m_Color = Background();
  pFrame->m_fBorderWidth = 1.0f;
  pFrame->m_BorderColor = Line();
  pFrame->m_vCornerRadius = plVec4(4);
  pFrame->m_bClipChildren = true;

  const plGameUiEditor::Column columns[] = {{"Frames (check what to import)", 1.0f}};
  plHybridArray<float, 1> lefts, widths;
  plGameUiEditor::LayoutColumns(columns, rect.width - 2.0f, lefts, widths);

  plUiPanel* pInner = Panel(pFrame);
  SetRect(pInner, plRectFloat(1.0f, 1.0f, rect.width - 2.0f, rect.height - 2.0f));
  pInner->m_Color = plColorGammaUB(0, 0, 0, 0);
  plGameUiEditor::TableHeader(pInner, 0.0f, columns, lefts, widths);

  plUiPanel* pTreeHolder = Panel(pInner);
  SetRect(pTreeHolder, plRectFloat(0.0f, plGameUiEditor::TableHeaderHeight, rect.width - 2.0f, plMath::Max(rect.height - 2.0f - plGameUiEditor::TableHeaderHeight, 0.0f)));
  pTreeHolder->m_Color = plColorGammaUB(0, 0, 0, 0);
  m_pTree->Build(pTreeHolder);
}

void plUiFigmaImportEngineDlg::BuildBody(plUiElement* pBody, const plVec2& vSize)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();
  const float x = plEditorUiDialogHost::Padding;
  const float fWidth = vSize.x - 2.0f * x;

  plEditorUiForm form(pBody, plRectFloat(x, s_fTop, fWidth, plMath::Max(vSize.y - s_fTop, 0.0f)), s_fLabelWidth);

  TextField(pBody, form.Row("File URL or key:"), s_szUrl, s_szUrlText, "https://www.figma.com/file/<key>/... or the raw file key");
  plGameUiEditor::SecretField(pBody, form.Row("Access token:"), s_szToken, s_szTokenText, s_szTokenMask, s_szTokenEmpty, "figd_... (Figma > Settings > Personal access tokens)");
  BuildFolderRow(pBody, form.Row("Output folder:"));

  const plRectFloat fetchRow = form.FieldRow();
  PushButton(pBody, board, plRectFloat(fetchRow.x, fetchRow.y, PushButtonWidth("Fetch Frames"), fetchRow.height), "Fetch Frames", s_szFetch, !m_bBusy);

  // the frames fill what the status line under them leaves
  const float fListTop = form.GetY() + 4.0f;
  const float fStatusTop = vSize.y - s_fBottom - s_fStatusHeight;
  BuildFrameList(pBody, plRectFloat(x, fListTop, fWidth, plMath::Max(fStatusTop - 6.0f - fListTop, 60.0f)));

  plUiText* pStatus = TextAt(pBody, {}, plRectFloat(x, fStatusTop, fWidth, s_fStatusHeight), Muted(), FontSize - 1);
  pStatus->m_sTextBinding = s_szStatus;
  pStatus->m_bWrap = true;
  pStatus->m_VAlign = plUiTextVAlign::Top;

  AddBodyToolTips(form.GetToolTips());
}

void plUiFigmaImportEngineDlg::OnUiUpdate()
{
  ReadFields();
}

void plUiFigmaImportEngineDlg::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  // file dialogs and requests run after the current UI dispatch
  if (sId == s_szBrowse)
    m_Timers.Start(plTime::MakeZero(), [this]()
      { Browse(); });
  else if (sId == s_szFetch)
    m_Timers.Start(plTime::MakeZero(), [this]()
      { Fetch(); });
}

void plUiFigmaImportEngineDlg::OnButton(plStringView sId)
{
  if (sId == s_szImport)
  {
    ReadFields();
    if (GetHost().IsButtonEnabled(s_szImport))
    {
      m_Timers.Start(plTime::MakeZero(), [this]()
        { Import(); });
    }

    return;
  }

  plEngineDialog::OnButton(sId);
}

void plUiFigmaImportEngineDlg::Browse()
{
  if (m_bBusy)
    return;

  const plString sDir = plFileDialog::GetExistingDirectory("Choose Output Folder", ReadField(s_szFolderText));
  if (!sDir.IsEmpty())
    WriteField(s_szFolderText, sDir);
}

void plUiFigmaImportEngineDlg::Fetch()
{
  if (m_bBusy)
    return;

  ReadFields();

  m_Checked.Clear();
  m_Pages.Clear();
  m_pTree->Refresh();
  SetBusy(true);

  const bool bFound = m_Session.Fetch(m_sUrl, m_sToken, [this](plStringView sText)
    { SetStatus(sText); });

  if (bFound)
  {
    for (const plUiFigmaRestSession::Frame& frame : m_Session.GetFrames())
    {
      m_Checked.PushBack(true);
      if (!m_Pages.Contains(frame.m_sPage))
        m_Pages.PushBack(frame.m_sPage);
    }

    plEditorUiSettingsFile& settings = plEditorUiSettingsFile::GetCurrent();
    settings.SetValue(s_szSettingUrl, m_sUrl);
    settings.SetValue(s_szSettingToken, m_sToken);
  }

  m_pTree->Refresh();
  m_pTree->ExpandAll();
  SetBusy(false);
}

void plUiFigmaImportEngineDlg::Import()
{
  if (m_bBusy)
    return;

  ReadFields();
  if (!GetHost().IsButtonEnabled(s_szImport))
    return;

  plEditorUiSettingsFile::GetCurrent().SetValue(s_szSettingFolder, m_sFolder);

  plDynamicArray<plInt32> frames;
  for (plUInt32 i = 0; i < m_Checked.GetCount(); ++i)
  {
    if (m_Checked[i])
      frames.PushBack(static_cast<plInt32>(i));
  }

  SetBusy(true);

  m_Session.Import(m_sUrl, m_sToken, m_sFolder, frames, [this](plStringView sText)
    { SetStatus(sText); });

  Close(plEngineDialog::Accepted);
}

void plUiFigmaImportEngineDlg::ToggleFrame(plUInt32 uiFrame)
{
  if (uiFrame >= m_Checked.GetCount() || m_bBusy)
    return;

  m_Checked[uiFrame] = !m_Checked[uiFrame];
  m_pTree->Refresh();
  UpdateState();
  RequestRedraw();
}

void plUiFigmaImportEngineDlg::OnPointer(const plEditorUiPointerEvent& e)
{
  // a click on a frame's check box toggles it
  if (e.m_Type != plEditorUiPointerEvent::Type::Press || e.m_Button != plEditorUiPointerEvent::Button::Left || e.m_sElement != s_szFrameRow)
    return;

  if (e.m_iItemIndex < 0 || e.m_iItemIndex >= static_cast<plInt32>(m_pTree->GetVisibleRowCount()))
    return;

  const plUInt64 uiItem = m_pTree->GetVisibleRow(static_cast<plUInt32>(e.m_iItemIndex));
  if (IsPageItem(uiItem))
    return;

  const float fIndent = m_pTree->GetVisibleRowDepth(static_cast<plUInt32>(e.m_iItemIndex)) * s_fIndentStep;
  const float fX = e.m_vPosition.x - e.m_ElementRect.x - fIndent;
  if (fX < s_fCheckBoxLeft || fX >= s_fCheckBoxRight)
    return;

  ToggleFrame(ToIndex(uiItem));
}

void plUiFigmaImportEngineDlg::OnKey(const plEditorUiKeyEvent& e)
{
  if (e.m_bHandled || !m_pTree->HasFocus())
    return;

  // Enter in the frame list presses Import Selected
  if (e.m_Key == plEditorUiKeyEvent::Key::Enter && e.m_uiModifiers == 0)
  {
    GetHost().PressButton(s_szImport);
    e.m_bHandled = true;
    return;
  }

  // Space toggles the current frame's check box
  const plUInt64 uiCurrent = m_pTree->GetCurrent();
  if (e.m_Key != plEditorUiKeyEvent::Key::Space || uiCurrent == 0 || IsPageItem(uiCurrent))
    return;

  ToggleFrame(ToIndex(uiCurrent));
  e.m_bHandled = true;
}
