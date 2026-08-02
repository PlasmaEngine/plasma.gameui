#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Uuid.h>

// declares Q_DECLARE_METATYPE(plUuid), which must precede the moc-generated code
// for signals that carry plUuid parameters
#include <GuiFoundation/GuiFoundationDLL.h>

#include <QWidget>

class QLabel;
class QComboBox;
class QCheckBox;
class QHBoxLayout;
class QTableWidget;

/// \brief Bottom dock of the UI Document editor: the selected element's STATE MACHINE.
///
/// Each row is one state: a condition over any blackboard entry ("Health < 20") plus what
/// happens while it is active — an optional Style asset that cross-fades over the
/// element's look, and/or an animation clip that plays while the state is active. States
/// are evaluated top to bottom, the first matching condition wins. The Preview column
/// writes a value that satisfies (or clears) the condition into the mock blackboard.
///
/// Style/transition details of a state are edited in the property grid (double-click the
/// state's name). The panel never writes to the document itself — all mutations are
/// emitted as signals and applied by the document window through accessor transactions.
class plQtUiStatesPanel : public QWidget
{
  Q_OBJECT

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

  explicit plQtUiStatesPanel(QWidget* pParent);

  /// Template entries offered in the binding combos.
  void SetTemplateEntries(const plDynamicArray<plString>& entries);

  /// The document's animation clip names, offered in the Clip column.
  void SetClipNames(const plDynamicArray<plString>& clipNames);

  /// nullptr = nothing (or a non-element) selected.
  void SetElement(const ElementStateInfo* pInfo);

Q_SIGNALS:
  void AddStateRequested(plUuid elementGuid);
  void RemoveStateRequested(plUuid stateGuid);
  void StateEdited(plUuid stateGuid, QString sName, QString sEntry, int iCondition, double fValue, QString sValueText, int iVisibility, QString sPlayClip);
  void StateStyleChanged(plUuid stateGuid, QString sStyleRef); ///< asset-picker result (guid string, empty = clear)
  void StateActivated(plUuid stateGuid);                       ///< select the state object in the property grid
  void DisabledBindingChanged(plUuid elementGuid, QString sEntry);
  void TestValueChanged(QString sEntry, QString sValue); ///< mock blackboard write

private:
  void RebuildChips();
  void RebuildStatesTable();
  void EmitStateEdited(int iRow);

  QLabel* m_pElementLabel = nullptr;
  QWidget* m_pChipRow = nullptr;
  QHBoxLayout* m_pChipLayout = nullptr;
  QLabel* m_pNoSelectionLabel = nullptr;

  QTableWidget* m_pStatesTable = nullptr;
  QComboBox* m_pDisabledEntryCombo = nullptr;
  QCheckBox* m_pTestDisabled = nullptr;
  QWidget* m_pDisabledRow = nullptr;
  QWidget* m_pEditorArea = nullptr;

  ElementStateInfo m_Info;
  bool m_bHasElement = false;
  plDynamicArray<plString> m_TemplateEntries;
  plDynamicArray<plString> m_ClipNames;
  bool m_bIgnoreChanges = false;
};
