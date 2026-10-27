#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Delegate.h>
#include <Foundation/Types/Uuid.h>

/// \brief Bottom dock of the UI Document editor: the selected element's STATE MACHINE.
///
/// Each row is one state: a condition over any blackboard entry ("Health < 20") plus what
/// happens while it is active — an optional Style asset that cross-fades over the
/// element's look, and/or an animation clip that plays while the state is active. States
/// are evaluated top to bottom, the first matching condition wins. The Preview column
/// writes a value that satisfies (or clears) the condition into the mock blackboard.
///
/// Style/transition details of a state are edited in the property grid (double-click the
/// state's name). The panel never writes to the document itself — all mutations go out
/// through the delegates and are applied by the document window through accessor transactions.
/// The engine-drawn panel (plUiStatesView) implements this interface.
class plUiStatesPanelBase
{
public:
  struct StateRow
  {
    plUuid m_Guid;
    plString m_sName;
    plString m_sEntry;
    plInt32 m_iCondition = 0;  ///< plUiStateCondition value
    double m_fValue = 0.0;
    plString m_sValueText;     ///< string operand (Equal/NotEqual compare as text when set)
    plInt32 m_iVisibility = 0; ///< plUiStateVisibility value
    plString m_sStyleRef;      ///< raw asset reference (guid string) of the state's Style
    plString m_sStyleName;     ///< display name of the state's Style asset; empty = none
    plString m_sPlayClip;      ///< clip driven by this state; empty = none
  };

  struct ElementStateInfo
  {
    plUuid m_ElementGuid;
    plString m_sElementLabel;
    bool m_bIsButton = false;

    plString m_sStyleName; ///< display name of the element's Style asset; empty = unstyled
    plString m_sDisabledBinding;

    plDynamicArray<StateRow> m_States;
  };

  virtual ~plUiStatesPanelBase() = default;

  /// Template entries offered in the binding combos.
  virtual void SetTemplateEntries(const plDynamicArray<plString>& entries) = 0;

  /// The document's animation clip names, offered in the Clip column.
  virtual void SetClipNames(const plDynamicArray<plString>& clipNames) = 0;

  /// nullptr = nothing (or a non-element) selected.
  virtual void SetElement(const ElementStateInfo* pInfo) = 0;

  /// \name What the panel asks of the document window, which sets the delegates.
  ///@{
  plDelegate<void(const plUuid&)> m_OnAddStateRequested; ///< element
  plDelegate<void(const plUuid&)> m_OnRemoveStateRequested;
  plDelegate<void(const StateRow&)> m_OnStateEdited;                             ///< the state's new values
  plDelegate<void(const plUuid&, plStringView)> m_OnStateStyleChanged;           ///< asset-picker result (guid string, empty = clear)
  plDelegate<void(const plUuid&)> m_OnStateActivated;                            ///< select the state object in the property grid
  plDelegate<void(const plUuid&, plStringView)> m_OnDisabledBindingChanged;      ///< element, entry
  plDelegate<void(plStringView, plStringView)> m_OnTestValueChanged;             ///< mock blackboard write: entry, value
  ///@}

  /// Call the delegates that are set.
  void AddStateRequested(const plUuid& elementGuid);
  void RemoveStateRequested(const plUuid& stateGuid);
  void StateEdited(const StateRow& state);
  void StateStyleChanged(const plUuid& stateGuid, plStringView sStyleRef);
  void StateActivated(const plUuid& stateGuid);
  void DisabledBindingChanged(const plUuid& elementGuid, plStringView sEntry);
  void TestValueChanged(plStringView sEntry, plStringView sValue);
};
