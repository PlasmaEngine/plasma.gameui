#pragma once

#include <GuiFoundation/EngineUi/EngineDialog.h>

class plEditorUiTable;
class plUiImportReport;
struct plEditorUiKeyEvent;

/// \brief The UI import report drawn by the engine: the summary (documents, elements and images imported, warnings and
/// errors) over a table of every entry with its severity, source path and message. Copy to Clipboard copies all
/// entries as tab-separated lines; OK, Escape and the close button close it.
class plUiImportReportEngineDlg : public plEngineDialog
{
public:
  plUiImportReportEngineDlg(const plUiImportReport& report, plStringView sTitle);
  ~plUiImportReportEngineDlg();

  /// Every entry as "severity<TAB>source<TAB>message" lines, as Copy to Clipboard copies them.
  static plString GetClipboardText(const plUiImportReport& report);

  /// "Error", "Warning" or "Info".
  static const char* GetSeverityText(plInt32 iSeverity);

protected:
  void BuildBody(plUiElement* pBody, const plVec2& vSize) override;
  void OnButton(plStringView sId) override;
  plVec2 GetPreferredSize() const override;

private:
  class EntryAdapter;

  plString GetSummary() const;
  void OnKey(const plEditorUiKeyEvent& e);

  const plUiImportReport& m_Report;
  plUniquePtr<EntryAdapter> m_pAdapter;
  plUniquePtr<plEditorUiTable> m_pTable;
  plEventSubscriptionID m_KeySubscription = 0;
};
