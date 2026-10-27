#pragma once

#include <EditorPluginGameUi/UiDocumentAsset/UiBindingsPanel.h>
#include <Foundation/Types/UniquePtr.h>

class plEditorUiContent;
class plUiBindingsSurface;

/// \brief The engine-drawn bindings panel: the Bindings table on the left (Element / Property / Entry, entries in
/// the XR colour or the warning colour when the Blackboard Template does not declare them) with Add Binding... and
/// Remove Binding, and the session-only mock blackboard on the right (scenario chips, Entry / Value with a slider for
/// numbers, Add Entry, Remove Entry and Reset Values).
///
/// Double-click or F2 edits an entry in place; Enter or leaving the field commits it, Escape cancels.
///
/// The view keeps the panel's state; the content that draws it comes from CreateContent, and the document window shows
/// it as a panel.
class plUiBindingsView : public plUiBindingsPanelBase
{
public:
  plUiBindingsView();
  ~plUiBindingsView();

  /// The content that draws the panel; it must go before the view.
  plUniquePtr<plEditorUiContent> CreateContent();

  void SetTemplateEntries(plArrayPtr<const MockEntry> entries) override;
  void SetBindings(plArrayPtr<const BindingInfo> bindings) override;
  void GetMockEntries(plDynamicArray<MockEntry>& out_entries) const override;
  void SetMockEntries(plArrayPtr<const MockEntry> entries) override;
  void SetScenarios(plArrayPtr<const plString> names) override;
  void SetMockValueSilently(plStringView sName, plStringView sValue) override;

private:
  friend class plUiBindingsSurface;

  /// One row of the mock blackboard; numbers get a slider over [m_fMin, m_fMax], which grows to include the value.
  struct MockRow
  {
    plString m_sName;
    plString m_sValue;
    float m_fMin = 0.0f;
    float m_fMax = 1.0f;
  };

  /// The content draws the panel again on its next update; nothing without a content.
  void RebuildSurface();

  bool IsTemplateEntry(plStringView sName, plStringView* out_pInitialValue = nullptr) const;
  bool MirrorEntriesIntoMockTable();
  plInt32 FindMockRow(plStringView sName) const;
  void ResetValues();

  plDynamicArray<BindingInfo> m_Bindings;
  plDynamicArray<MockEntry> m_TemplateEntries;
  plDynamicArray<MockRow> m_MockRows;
  plDynamicArray<plString> m_Scenarios;
  plString m_sCurrentScenario; ///< the chip shown as chosen; empty = Default

  plUiBindingsSurface* m_pSurface = nullptr;
};
