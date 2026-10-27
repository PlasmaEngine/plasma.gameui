#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/ArrayPtr.h>
#include <Foundation/Types/Delegate.h>
#include <Foundation/Types/Uuid.h>

/// \brief Bottom panel of the UI Document editor: manages the document's blackboard
/// bindings and hosts an editable session-only mock blackboard that drives the live preview.
///
/// The panel never writes to the document itself — all binding mutations go out through the
/// delegates and are applied by the document window through accessor transactions. The
/// engine-drawn panel (plUiBindingsView) implements this interface.
class plUiBindingsPanelBase
{
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

  virtual ~plUiBindingsPanelBase() = default;

  /// The entries declared by the document's Blackboard Template (the design-time binding
  /// contract). Seeds the mock table and drives validation styling.
  virtual void SetTemplateEntries(plArrayPtr<const MockEntry> entries) = 0;

  /// Refreshes the bindings table and mirrors template + bound entries into the mock table
  /// (adding missing rows with seed values; never overwriting existing values;
  /// styling rows that are neither in the template nor referenced).
  virtual void SetBindings(plArrayPtr<const BindingInfo> bindings) = 0;

  virtual void GetMockEntries(plDynamicArray<MockEntry>& out_entries) const = 0;

  /// Replaces the values of existing rows (adding missing ones) — scenario loading.
  /// Reports MockDataChanged when anything changed.
  virtual void SetMockEntries(plArrayPtr<const MockEntry> entries) = 0;

  /// Rebuilds the scenario chip row (Default + saved scenarios + Save).
  virtual void SetScenarios(plArrayPtr<const plString> names) = 0;

  /// Updates one mock value without reporting MockDataChanged (used for engine -> editor
  /// sync when interact-mode clicks write the preview blackboard).
  virtual void SetMockValueSilently(plStringView sName, plStringView sValue) = 0;

  /// Asks for the name of a mock scenario ("Save Mock Scenario") with the engine text prompt. Returns the name without
  /// surrounding spaces; empty on Cancel.
  static plString AskScenarioName();

  /// \name What the panel asks of the document window, which sets the delegates.
  ///@{
  plDelegate<void()> m_OnMockDataChanged;
  plDelegate<void()> m_OnAddBindingRequested;
  plDelegate<void(const plUuid&, plStringView)> m_OnRemoveBindingRequested;               ///< element, binding property
  plDelegate<void(const plUuid&, plStringView, plStringView)> m_OnBindingEntryEdited;    ///< element, binding property, new entry
  plDelegate<void(const plUuid&)> m_OnBindingRowActivated;
  plDelegate<void(plStringView)> m_OnScenarioSaveRequested; ///< snapshot the current table under this name
  plDelegate<void(plStringView)> m_OnScenarioDeleteRequested;
  plDelegate<void(plStringView)> m_OnScenarioSelected; ///< load the named scenario into the table
  ///@}

  /// Call the delegates that are set.
  void MockDataChanged();
  void AddBindingRequested();
  void RemoveBindingRequested(const plUuid& elementGuid, plStringView sBindingProperty);
  void BindingEntryEdited(const plUuid& elementGuid, plStringView sBindingProperty, plStringView sNewEntry);
  void BindingRowActivated(const plUuid& elementGuid);
  void ScenarioSaveRequested(plStringView sName);
  void ScenarioDeleteRequested(plStringView sName);
  void ScenarioSelected(plStringView sName);
};
