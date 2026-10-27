#pragma once

#include <EditorPluginGameUi/UiImport/UiImportFigmaSession.h>
#include <EditorUi/Runtime.h>
#include <GuiFoundation/EngineUi/EngineDialog.h>

class plEditorUiTree;
struct plEditorUiKeyEvent;
struct plEditorUiPointerEvent;

/// \brief Import UI from Figma drawn by the engine: the file URL or key, the personal access token (masked), the output
/// folder with "...", Fetch Frames, the fetched top-level frames by page with a check box each (all checked after a
/// fetch) and a status line under them.
///
/// Import Selected stays disabled until a fetch found frames; after that while the output folder is empty or no frame
/// is checked, and the validation line says why. Fetch and Import run outside the engine UI's frame and disable the
/// buttons while they run. The URL, the token and the output folder are remembered per user (editor settings
/// FigmaImport/LastUrl, FigmaImport/Token, FigmaImport/LastFolder).
class plUiFigmaImportEngineDlg : public plEngineDialog
{
public:
  plUiFigmaImportEngineDlg();
  ~plUiFigmaImportEngineDlg();

protected:
  void BuildBody(plUiElement* pBody, const plVec2& vSize) override;
  void OnButton(plStringView sId) override;
  void OnUiEvent(const plMsgUiEvent& event) override;
  void OnUiUpdate() override;
  plVec2 GetPreferredSize() const override;

private:
  class FrameAdapter;

  plString ReadField(const char* szEntry) const;
  void WriteField(const char* szEntry, plStringView sText);
  void ReadFields();
  void UpdateState();
  void SetStatus(plStringView sText);
  void SetBusy(bool bBusy);

  void BuildFolderRow(plUiElement* pBody, const plRectFloat& rect);
  void BuildFrameList(plUiElement* pBody, const plRectFloat& rect);

  void Browse();
  void Fetch();
  void Import();

  plUInt32 GetCheckedCount() const;
  void ToggleFrame(plUInt32 uiFrame);
  void OnPointer(const plEditorUiPointerEvent& e);
  void OnKey(const plEditorUiKeyEvent& e);

  plUiFigmaRestSession m_Session;
  plDynamicArray<bool> m_Checked;   ///< per fetched frame
  plDynamicArray<plString> m_Pages; ///< page names in the order of their first frame
  plUniquePtr<FrameAdapter> m_pAdapter;
  plUniquePtr<plEditorUiTree> m_pTree;

  plString m_sUrl;
  plString m_sToken;
  plString m_sFolder; ///< trimmed
  bool m_bFieldsRead = false;
  bool m_bBusy = false;
  bool m_bFolderMarked = false;

  plEventSubscriptionID m_PointerSubscription = 0;
  plEventSubscriptionID m_KeySubscription = 0;
  plEditorUiTimerGroup m_Timers; ///< file dialogs and requests run after the current UI dispatch
};
