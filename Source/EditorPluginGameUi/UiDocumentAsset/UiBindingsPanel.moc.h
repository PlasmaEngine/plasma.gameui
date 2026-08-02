#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/ArrayPtr.h>
#include <Foundation/Types/Uuid.h>

// declares Q_DECLARE_METATYPE(plUuid), which must precede the moc-generated code
// for signals that carry plUuid parameters
#include <GuiFoundation/GuiFoundationDLL.h>

#include <QWidget>

class QTableWidget;
class QHBoxLayout;

/// \brief Bottom dock content of the UI Document editor: manages the document's blackboard
/// bindings and hosts an editable session-only mock blackboard that drives the live preview.
///
/// The panel never writes to the document itself — all binding mutations are emitted as
/// signals and applied by the document window through accessor transactions.
class plQtUiBindingsPanel : public QWidget
{
  Q_OBJECT

public:
  struct BindingInfo
  {
    plUuid m_ElementGuid;
    plString m_sElement;
    plString m_sProperty;        ///< display name (e.g. "Fill Amount")
    plString m_sBindingProperty; ///< reflected property ("FillAmountBinding" etc.)
    plString m_sEntry;
    plString m_sSeedValue; ///< value to seed the mock table with when mirroring this binding
  };

  struct MockEntry
  {
    plString m_sName;
    plString m_sValue;
  };

  explicit plQtUiBindingsPanel(QWidget* pParent);

  /// The entries declared by the document's Blackboard Template (the design-time binding
  /// contract). Seeds the mock table and drives validation styling.
  void SetTemplateEntries(plArrayPtr<const MockEntry> entries);

  /// Refreshes the bindings table and mirrors template + bound entries into the mock table
  /// (adding missing rows with seed values; never overwriting existing values;
  /// styling rows that are neither in the template nor referenced).
  void SetBindings(plArrayPtr<const BindingInfo> bindings);

  void GetMockEntries(plDynamicArray<MockEntry>& out_entries) const;

  /// Replaces the values of existing rows (adding missing ones) — scenario loading.
  /// Emits MockDataChanged when anything changed.
  void SetMockEntries(plArrayPtr<const MockEntry> entries);

  /// Rebuilds the scenario chip row (Default + saved scenarios + Save).
  void SetScenarios(const QStringList& names);

  /// Updates one mock value without emitting MockDataChanged (used for engine -> editor
  /// sync when interact-mode clicks write the preview blackboard).
  void SetMockValueSilently(plStringView sName, plStringView sValue);

Q_SIGNALS:
  void MockDataChanged();
  void AddBindingRequested();
  void RemoveBindingRequested(plUuid elementGuid, QString sBindingProperty);
  void BindingEntryEdited(plUuid elementGuid, QString sBindingProperty, QString sNewEntry);
  void BindingRowActivated(plUuid elementGuid);
  void ScenarioSaveRequested(QString sName);   ///< snapshot the current table under this name
  void ScenarioDeleteRequested(QString sName);
  void ScenarioSelected(QString sName);        ///< load the named scenario into the table

private:
  void OnAddMockEntry();
  void OnRemoveMockEntry();
  void OnResetValues();
  void OnRemoveBinding();
  void OnSaveScenario();
  bool GetRowPayload(int iRow, plUuid& out_elementGuid, QString& out_sBindingProperty) const;
  bool IsTemplateEntry(plStringView sName, plStringView* out_pInitialValue = nullptr) const;
  void MirrorEntriesIntoMockTable(bool& ref_bMockChanged);
  void RefreshRowStyles();
  void UpdateValueSliders(); ///< slider cell widgets for numeric mock values

  QTableWidget* m_pBindingsTable = nullptr;
  QTableWidget* m_pMockTable = nullptr;
  QHBoxLayout* m_pScenarioLayout = nullptr; ///< chip row above the mock table
  plDynamicArray<BindingInfo> m_Bindings;
  plDynamicArray<MockEntry> m_TemplateEntries;
  bool m_bIgnoreChanges = false;
};
