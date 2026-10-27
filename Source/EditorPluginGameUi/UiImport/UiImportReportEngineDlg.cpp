#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiImport/UiImportModel.h>
#include <EditorPluginGameUi/UiImport/UiImportReportEngineDlg.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Platform/EditorPlatform.h>
#include <EditorUi/Table.h>

#include <algorithm>

namespace
{
  const char* s_szOk = "Ok";
  const char* s_szCopy = "Copy";

  constexpr float s_fWidth = 720.0f;
  constexpr float s_fHeight = 420.0f;
  constexpr float s_fGap = 10.0f;
  constexpr float s_fBottom = 12.0f;
  constexpr float s_fSeverityWidth = 90.0f;
  constexpr float s_fCellPadding = 16.0f;
  constexpr float s_fMinSourceWidth = 80.0f;
  constexpr float s_fMaxSourceShare = 0.4f;
} // namespace

//////////////////////////////////////////////////////////////////////////
// The entries in report order, or sorted by a column

class plUiImportReportEngineDlg::EntryAdapter : public plEditorUiTableAdapter
{
public:
  explicit EntryAdapter(const plUiImportReport& report)
    : m_Report(report)
  {
    for (plUInt32 i = 0; i < m_Report.m_Entries.GetCount(); ++i)
      m_Order.PushBack(i);
  }

  plUInt32 GetRowCount() const override { return m_Order.GetCount(); }

  plString GetCell(plUInt32 uiRow, plUInt32 uiColumn) const override
  {
    const plUiImportReportEntry& entry = m_Report.m_Entries[m_Order[uiRow]];
    switch (uiColumn)
    {
      case 0:
        return GetSeverityText(entry.m_Severity);
      case 1:
        return entry.m_sSourcePath;
      default:
        return entry.m_sMessage;
    }
  }

  void Sort(plUInt32 uiColumn, bool bAscending) override
  {
    // stable, so entries with the same text keep the report's order
    std::stable_sort(m_Order.GetData(), m_Order.GetData() + m_Order.GetCount(), [this, uiColumn, bAscending](plUInt32 a, plUInt32 b)
      {
        const plString sA = GetEntryText(a, uiColumn);
        const plString sB = GetEntryText(b, uiColumn);
        return bAscending ? sA.Compare_NoCase(sB) < 0 : sB.Compare_NoCase(sA) < 0; });
  }

private:
  plString GetEntryText(plUInt32 uiEntry, plUInt32 uiColumn) const
  {
    const plUiImportReportEntry& entry = m_Report.m_Entries[uiEntry];
    return uiColumn == 0 ? plString(GetSeverityText(entry.m_Severity)) : (uiColumn == 1 ? entry.m_sSourcePath : entry.m_sMessage);
  }

  const plUiImportReport& m_Report;
  plDynamicArray<plUInt32> m_Order;
};

//////////////////////////////////////////////////////////////////////////
// plUiImportReportEngineDlg

plUiImportReportEngineDlg::plUiImportReportEngineDlg(const plUiImportReport& report, plStringView sTitle)
  : plEngineDialog("GameUi.ImportReport", sTitle)
  , m_Report(report)
{
  if (!IsReady())
    return;

  plEditorUiDialogButton buttons[2];
  buttons[0].m_sId = s_szCopy;
  buttons[0].m_sLabel = "Copy to Clipboard";
  buttons[0].m_Role = plEditorUiDialogButton::Role::Left;
  buttons[1].m_sId = s_szOk;
  buttons[1].m_sLabel = "OK";
  buttons[1].m_Role = plEditorUiDialogButton::Role::Accept;
  GetHost().SetButtons(buttons);

  // subscribed before the table exists: Escape closes the report, which has no Cancel button
  m_KeySubscription = GetSurface()->m_KeyEvents.AddEventHandler(plMakeDelegate(&plUiImportReportEngineDlg::OnKey, this));

  m_pAdapter = PL_DEFAULT_NEW(EntryAdapter, m_Report);
  m_pTable = PL_DEFAULT_NEW(plEditorUiTable, *GetSurface(), "ImportReport.Table", *m_pAdapter);
}

plUiImportReportEngineDlg::~plUiImportReportEngineDlg()
{
  if (!IsReady())
    return;

  m_pTable.Clear();
  GetSurface()->m_KeyEvents.RemoveEventHandler(m_KeySubscription);
}

const char* plUiImportReportEngineDlg::GetSeverityText(plInt32 iSeverity)
{
  return iSeverity == plUiImportSeverity::Error ? "Error" : (iSeverity == plUiImportSeverity::Warning ? "Warning" : "Info");
}

plString plUiImportReportEngineDlg::GetClipboardText(const plUiImportReport& report)
{
  plStringBuilder sText;
  for (const plUiImportReportEntry& e : report.m_Entries)
    sText.AppendFormat("{}\t{}\t{}\n", GetSeverityText(e.m_Severity), e.m_sSourcePath, e.m_sMessage);

  return sText;
}

plVec2 plUiImportReportEngineDlg::GetPreferredSize() const
{
  return plVec2(s_fWidth, s_fHeight);
}

plString plUiImportReportEngineDlg::GetSummary() const
{
  plUInt32 uiWarnings = 0;
  plUInt32 uiErrors = 0;
  for (const plUiImportReportEntry& e : m_Report.m_Entries)
  {
    if (e.m_Severity == plUiImportSeverity::Warning)
      ++uiWarnings;
    else if (e.m_Severity == plUiImportSeverity::Error)
      ++uiErrors;
  }

  plStringBuilder sSummary;
  sSummary.SetFormat("{} document(s), {} element(s), {} image(s) imported \xE2\x80\x94 {} warning(s), {} error(s). Everything listed below was approximated or dropped.", m_Report.m_uiDocuments, m_Report.m_uiElements, m_Report.m_uiImages, uiWarnings, uiErrors);
  return sSummary;
}

void plUiImportReportEngineDlg::BuildBody(plUiElement* pBody, const plVec2& vSize)
{
  using namespace plEditorUi;

  const float x = plEditorUiDialogHost::Padding;
  const float fWidth = vSize.x - 2.0f * x;
  const float y = DialogIntro(pBody, vSize.x, GetSummary()) + s_fGap;

  // Severity fixed, Source as wide as its longest path (up to a share of the width), Message takes the rest
  float fSource = s_fMinSourceWidth;
  for (const plUiImportReportEntry& e : m_Report.m_Entries)
    fSource = plMath::Max(fSource, plMath::Ceil(MeasureText(e.m_sSourcePath, FontSize)) + s_fCellPadding);
  fSource = plMath::Min(fSource, fWidth * s_fMaxSourceShare);

  plEditorUiTableColumn columns[3];
  columns[0].m_sTitle = "Severity";
  columns[0].m_fWidth = s_fSeverityWidth;
  columns[1].m_sTitle = "Source";
  columns[1].m_fWidth = fSource;
  columns[2].m_sTitle = "Message";
  columns[2].m_fWidth = plMath::Max(fWidth - 2.0f - s_fSeverityWidth - fSource, 120.0f);
  m_pTable->SetColumns(columns);

  // a hairline frame around the table
  const plRectFloat rect(x, y, fWidth, plMath::Max(vSize.y - y - s_fBottom, 60.0f));
  plUiPanel* pFrame = Panel(pBody);
  SetRect(pFrame, rect);
  pFrame->m_Color = Background();
  pFrame->m_fBorderWidth = 1.0f;
  pFrame->m_BorderColor = Line();
  pFrame->m_vCornerRadius = plVec4(4);
  pFrame->m_bClipChildren = true;

  plUiPanel* pHolder = Panel(pFrame);
  SetRect(pHolder, plRectFloat(1.0f, 1.0f, rect.width - 2.0f, rect.height - 2.0f));
  pHolder->m_Color = plColorGammaUB(0, 0, 0, 0);
  m_pTable->Build(pHolder);
  m_pTable->Refresh();
}

void plUiImportReportEngineDlg::OnButton(plStringView sId)
{
  if (sId == s_szCopy)
  {
    plEditorPlatform::SetClipboardText(GetClipboardText(m_Report));
    return;
  }

  plEngineDialog::OnButton(sId);
}

void plUiImportReportEngineDlg::OnKey(const plEditorUiKeyEvent& e)
{
  if (e.m_bHandled || e.m_Key != plEditorUiKeyEvent::Key::Escape)
    return;

  e.m_bHandled = true;
  Close(plEngineDialog::Rejected);
}
