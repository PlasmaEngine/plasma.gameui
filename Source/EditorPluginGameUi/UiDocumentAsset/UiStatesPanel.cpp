#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetBrowserDlg.moc.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEditorTheme.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiStatesPanel.moc.h>
#include <Foundation/Strings/StringBuilder.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GameUiPlugin/Elements/UiElements.h>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
  // mockup .stnode: alt surface, violet top border; active state gets the violet glow look
  QLabel* MakeChip(QWidget* pParent, const QString& sText, bool bAccent, bool bDimmed = false)
  {
    QLabel* pChip = new QLabel(sText, pParent);

    if (bAccent)
    {
      pChip->setStyleSheet("padding: 5px 14px; background: #2b1e38; color: #ffffff; border: 1px solid #845ef7; border-top: 2px solid #845ef7; border-radius: 4px;");
    }
    else if (bDimmed)
    {
      pChip->setStyleSheet("padding: 5px 14px; background: #2d2d2d; color: #8e8e8e; border: 1px solid #3c3c3c; border-top: 2px solid #505050; border-radius: 4px;");
    }
    else
    {
      pChip->setStyleSheet("padding: 5px 14px; background: #2d2d2d; color: #ffffff; border: 1px solid #3c3c3c; border-top: 2px solid #845ef7; border-radius: 4px;");
    }

    return pChip;
  }

  // mockup .stedge: violet arrow with the condition in dim monospace
  QLabel* MakeArrow(QWidget* pParent, const QString& sCondition)
  {
    QLabel* pArrow = new QLabel(pParent);
    pArrow->setTextFormat(Qt::RichText);
    pArrow->setText(QStringLiteral("<span style='color:#845ef7; font-size:12px;'>-&gt;</span> <span style='color:#8e8e8e; font-family:Consolas, monospace; font-size:9px;'>%1</span>").arg(sCondition.toHtmlEscaped()));
    return pArrow;
  }

  QLabel* MakeHint(QWidget* pParent, const char* szTranslationKey)
  {
    QLabel* pHint = new QLabel(plMakeQString(plTranslate(szTranslationKey)), pParent);
    pHint->setStyleSheet(plUiEditorTheme::szHintText);
    pHint->setWordWrap(true);
    return pHint;
  }

  const char* GetConditionDisplay(plInt32 iCondition)
  {
    switch (iCondition)
    {
      case plUiStateCondition::IsTrue:
        return "is true";
      case plUiStateCondition::IsFalse:
        return "is false";
      case plUiStateCondition::Equal:
        return "=";
      case plUiStateCondition::NotEqual:
        return "!=";
      case plUiStateCondition::Less:
        return "<";
      case plUiStateCondition::LessEqual:
        return "<=";
      case plUiStateCondition::Greater:
        return ">";
      case plUiStateCondition::GreaterEqual:
        return ">=";
      default:
        return "?";
    }
  }

  bool ConditionUsesValue(plInt32 iCondition)
  {
    return iCondition != plUiStateCondition::IsTrue && iCondition != plUiStateCondition::IsFalse;
  }

  /// mock-blackboard value that satisfies (or clears) the row's condition
  QString ComputeConditionValue(plInt32 iCondition, double fValue, bool bSatisfy)
  {
    switch (iCondition)
    {
      case plUiStateCondition::IsTrue:
        return bSatisfy ? QStringLiteral("true") : QStringLiteral("false");
      case plUiStateCondition::IsFalse:
        return bSatisfy ? QStringLiteral("false") : QStringLiteral("true");
      case plUiStateCondition::Equal:
        return QString::number(bSatisfy ? fValue : fValue + 1.0);
      case plUiStateCondition::NotEqual:
        return QString::number(bSatisfy ? fValue + 1.0 : fValue);
      case plUiStateCondition::Less:
        return QString::number(bSatisfy ? fValue - 1.0 : fValue);
      case plUiStateCondition::LessEqual:
        return QString::number(bSatisfy ? fValue : fValue + 1.0);
      case plUiStateCondition::Greater:
        return QString::number(bSatisfy ? fValue + 1.0 : fValue);
      case plUiStateCondition::GreaterEqual:
      default:
        return QString::number(bSatisfy ? fValue : fValue - 1.0);
    }
  }

  QString MakeConditionText(const plQtUiStatesPanel::StateRow& state)
  {
    if (state.m_sEntry.IsEmpty())
      return plMakeQString(plTranslate("GameUi.States.NoEntry"));

    if (ConditionUsesValue(state.m_iCondition))
    {
      const QString sOperand = state.m_sValueText.IsEmpty() ? QString::number(state.m_fValue) : QStringLiteral("'%1'").arg(QString::fromUtf8(state.m_sValueText.GetData()));
      return QStringLiteral("%1 %2 %3").arg(QString::fromUtf8(state.m_sEntry.GetData()), QString::fromUtf8(GetConditionDisplay(state.m_iCondition)), sOperand);
    }

    return QStringLiteral("%1 %2").arg(QString::fromUtf8(state.m_sEntry.GetData()), QString::fromUtf8(GetConditionDisplay(state.m_iCondition)));
  }
} // namespace

plQtUiStatesPanel::plQtUiStatesPanel(QWidget* pParent)
  : QWidget(pParent)
{
  QVBoxLayout* pOuterLayout = new QVBoxLayout(this);
  pOuterLayout->setContentsMargins(4, 4, 4, 4);

  // mockup panel chrome: "States — <element>" strip with the violet anim dot
  QVBoxLayout* pLayout = nullptr;
  QHBoxLayout* pHeaderRow = nullptr;
  QFrame* pPanel = plUiEditorTheme::CreatePanel(this, plUiEditorTheme::szAccentAnim, QString(), pLayout, &pHeaderRow, &m_pElementLabel);
  pOuterLayout->addWidget(pPanel);
  pLayout->setSpacing(6);

  // ————— hint when nothing is selected —————
  {
    m_pNoSelectionLabel = new QLabel(plMakeQString(plTranslate("GameUi.States.NoSelection")), this);
    m_pNoSelectionLabel->setAlignment(Qt::AlignCenter);
    m_pNoSelectionLabel->setStyleSheet("color: #8e8e8e; padding: 14px;");
    pLayout->addWidget(m_pNoSelectionLabel);
  }

  // ————— state chain chips —————
  {
    m_pChipRow = new QWidget(this);
    m_pChipLayout = new QHBoxLayout(m_pChipRow);
    m_pChipLayout->setContentsMargins(0, 0, 0, 0);
    m_pChipLayout->setSpacing(8);
    pLayout->addWidget(m_pChipRow);
  }

  // ————— editors —————
  {
    m_pEditorArea = new QWidget(this);
    QVBoxLayout* pEditors = new QVBoxLayout(m_pEditorArea);
    pEditors->setContentsMargins(0, 0, 0, 0);
    pEditors->setSpacing(4);

    // the state machine table
    {
      m_pStatesTable = new QTableWidget(m_pEditorArea);
      m_pStatesTable->setStyleSheet(plUiEditorTheme::szTableStyle);
      m_pStatesTable->setColumnCount(8);
      m_pStatesTable->setHorizontalHeaderLabels({plMakeQString(plTranslate("GameUi.States.ColState")),
        plMakeQString(plTranslate("GameUi.States.ColEntry")), plMakeQString(plTranslate("GameUi.States.ColCondition")),
        plMakeQString(plTranslate("GameUi.States.ColValue")), plMakeQString(plTranslate("GameUi.States.ColStyle")),
        plMakeQString(plTranslate("GameUi.States.ColClip")), plMakeQString(plTranslate("GameUi.States.ColVisibility")),
        plMakeQString(plTranslate("GameUi.States.Preview"))});
      m_pStatesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      m_pStatesTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
      m_pStatesTable->setColumnWidth(7, 70);
      m_pStatesTable->verticalHeader()->setVisible(false);
      m_pStatesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
      m_pStatesTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
      m_pStatesTable->setMinimumHeight(90);

      connect(m_pStatesTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* pItem)
        {
          if (!m_bIgnoreChanges)
          {
            EmitStateEdited(pItem->row());
          }
        });

      connect(m_pStatesTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* pItem)
        {
          // the name/style cells select the state in the property grid (look editing);
          // other cells start inline editing instead
          if (pItem->column() == 0 || pItem->column() == 4)
          {
            const int iRow = pItem->row();
            if (iRow >= 0 && iRow < static_cast<int>(m_Info.m_States.GetCount()))
            {
              Q_EMIT StateActivated(m_Info.m_States[iRow].m_Guid);
            }
          }
        });

      pEditors->addWidget(m_pStatesTable, 1);
      pEditors->addWidget(MakeHint(m_pEditorArea, "GameUi.States.MachineHint"));

      QHBoxLayout* pButtons = new QHBoxLayout();

      QPushButton* pAdd = new QPushButton(plMakeQString(plTranslate("GameUi.States.AddState")), m_pEditorArea);
      connect(pAdd, &QPushButton::clicked, this, [this]()
        {
          if (m_bHasElement)
          {
            Q_EMIT AddStateRequested(m_Info.m_ElementGuid);
          }
        });
      pButtons->addWidget(pAdd);

      QPushButton* pRemove = new QPushButton(plMakeQString(plTranslate("GameUi.States.RemoveState")), m_pEditorArea);
      connect(pRemove, &QPushButton::clicked, this, [this]()
        {
          const int iRow = m_pStatesTable->currentRow();
          if (iRow >= 0 && iRow < static_cast<int>(m_Info.m_States.GetCount()))
          {
            Q_EMIT RemoveStateRequested(m_Info.m_States[iRow].m_Guid);
          }
        });
      pButtons->addWidget(pRemove);
      pButtons->addStretch();

      pEditors->addLayout(pButtons);
    }

    // disabled (buttons only) — "Disabled while <entry> is true [Preview]"
    {
      m_pDisabledRow = new QWidget(m_pEditorArea);
      QVBoxLayout* pDisabledLayout = new QVBoxLayout(m_pDisabledRow);
      pDisabledLayout->setContentsMargins(0, 0, 0, 0);
      pDisabledLayout->setSpacing(2);

      QHBoxLayout* pRow = new QHBoxLayout();
      pRow->setSpacing(6);

      QLabel* pLabel = new QLabel(plMakeQString(plTranslate("GameUi.States.Disabled")), m_pDisabledRow);
      pLabel->setStyleSheet("font-weight: 600;");
      pRow->addWidget(pLabel);

      pRow->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.States.While")), m_pDisabledRow));

      m_pDisabledEntryCombo = new QComboBox(m_pDisabledRow);
      m_pDisabledEntryCombo->setEditable(true);
      m_pDisabledEntryCombo->setMinimumWidth(120);
      m_pDisabledEntryCombo->setToolTip(plMakeQString(plTranslate("GameUi.States.DisabledEntryTooltip")));
      connect(m_pDisabledEntryCombo, &QComboBox::activated, this, [this](int)
        {
          if (!m_bIgnoreChanges && m_bHasElement)
          {
            Q_EMIT DisabledBindingChanged(m_Info.m_ElementGuid, m_pDisabledEntryCombo->currentText());
          }
        });
      pRow->addWidget(m_pDisabledEntryCombo);

      pRow->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.States.IsTrue")), m_pDisabledRow));

      m_pTestDisabled = new QCheckBox(plMakeQString(plTranslate("GameUi.States.Preview")), m_pDisabledRow);
      m_pTestDisabled->setToolTip(plMakeQString(plTranslate("GameUi.States.TestTooltip")));
      connect(m_pTestDisabled, &QCheckBox::toggled, this, [this](bool bChecked)
        {
          if (!m_bIgnoreChanges && !m_Info.m_sDisabledBinding.IsEmpty())
          {
            Q_EMIT TestValueChanged(QString::fromUtf8(m_Info.m_sDisabledBinding.GetData()), bChecked ? QStringLiteral("true") : QStringLiteral("false"));
          }
        });
      pRow->addWidget(m_pTestDisabled);
      pRow->addStretch();

      pDisabledLayout->addLayout(pRow);
      pDisabledLayout->addWidget(MakeHint(m_pDisabledRow, "GameUi.States.DisabledHint"));

      pEditors->addWidget(m_pDisabledRow);
    }

    pLayout->addWidget(m_pEditorArea, 1);
  }

  SetElement(nullptr);
}

void plQtUiStatesPanel::SetTemplateEntries(const plDynamicArray<plString>& entries)
{
  m_TemplateEntries = entries;

  m_bIgnoreChanges = true;

  const QString sCurrent = m_pDisabledEntryCombo->currentText();
  m_pDisabledEntryCombo->clear();
  m_pDisabledEntryCombo->addItem(QString());
  for (const plString& sEntry : m_TemplateEntries)
  {
    m_pDisabledEntryCombo->addItem(QString::fromUtf8(sEntry.GetData()));
  }
  m_pDisabledEntryCombo->setCurrentText(sCurrent);

  m_bIgnoreChanges = false;
}

void plQtUiStatesPanel::SetClipNames(const plDynamicArray<plString>& clipNames)
{
  if (m_ClipNames == clipNames)
    return;

  m_ClipNames = clipNames;

  // the clip combos live in table cell widgets: rebuild the rows
  m_bIgnoreChanges = true;
  RebuildStatesTable();
  m_bIgnoreChanges = false;
}

void plQtUiStatesPanel::SetElement(const ElementStateInfo* pInfo)
{
  m_bHasElement = pInfo != nullptr;

  if (pInfo != nullptr)
  {
    m_Info = *pInfo;
  }
  else
  {
    m_Info = ElementStateInfo();
  }

  m_bIgnoreChanges = true;

  m_pNoSelectionLabel->setVisible(!m_bHasElement);
  m_pEditorArea->setVisible(m_bHasElement);
  m_pChipRow->setVisible(m_bHasElement);

  if (!m_bHasElement)
  {
    m_pElementLabel->setText(plMakeQString(plTranslate("GameUi.Panel.States")));
  }
  else
  {
    plStringBuilder sHeader;
    sHeader.SetFormat("{} - {}", plTranslate("GameUi.Panel.States"), m_Info.m_sElementLabel);

    if (!m_Info.m_sStyleName.IsEmpty())
    {
      sHeader.AppendFormat("  ({}: {})", plTranslate("GameUi.States.StyleLabel"), m_Info.m_sStyleName);
    }

    m_pElementLabel->setText(QString::fromUtf8(sHeader.GetData()));

    m_pDisabledEntryCombo->setCurrentText(QString::fromUtf8(m_Info.m_sDisabledBinding.GetData()));
    m_pTestDisabled->setEnabled(!m_Info.m_sDisabledBinding.IsEmpty());
    m_pTestDisabled->setChecked(false);
    m_pDisabledRow->setVisible(m_Info.m_bIsButton);

    RebuildStatesTable();
  }

  RebuildChips();

  m_bIgnoreChanges = false;
}

void plQtUiStatesPanel::RebuildStatesTable()
{
  m_pStatesTable->setRowCount(static_cast<int>(m_Info.m_States.GetCount()));

  QFont monoFont;
  monoFont.setFamily("Consolas");

  for (plUInt32 uiRow = 0; uiRow < m_Info.m_States.GetCount(); ++uiRow)
  {
    const StateRow& state = m_Info.m_States[uiRow];
    const int iRow = static_cast<int>(uiRow);

    QTableWidgetItem* pNameItem = new QTableWidgetItem(QString::fromUtf8(state.m_sName.GetData()));
    pNameItem->setForeground(QBrush(QColor(0xff, 0xff, 0xff)));
    pNameItem->setToolTip(plMakeQString(plTranslate("GameUi.States.NameTooltip")));
    m_pStatesTable->setItem(iRow, 0, pNameItem);

    // entry combo: template entries offered, custom names allowed (same as the binding combos)
    {
      QComboBox* pEntry = new QComboBox(m_pStatesTable);
      pEntry->setEditable(true);
      pEntry->setToolTip(plMakeQString(plTranslate("GameUi.States.EntryTooltip")));
      pEntry->addItem(QString());
      for (const plString& sEntry : m_TemplateEntries)
      {
        pEntry->addItem(QString::fromUtf8(sEntry.GetData()));
      }
      pEntry->setCurrentText(QString::fromUtf8(state.m_sEntry.GetData()));

      // entry names in the mockup's cyan monospace
      QFont comboFont = pEntry->font();
      comboFont.setFamily("Consolas");
      pEntry->setFont(comboFont);
      pEntry->setStyleSheet("QComboBox { color: #7fd8e8; }");

      connect(pEntry, &QComboBox::activated, this, [this, iRow](int)
        {
          if (!m_bIgnoreChanges)
          {
            EmitStateEdited(iRow);
          }
        });
      connect(pEntry->lineEdit(), &QLineEdit::editingFinished, this, [this, iRow, pEntry]()
        {
          if (!m_bIgnoreChanges && iRow < static_cast<int>(m_Info.m_States.GetCount()) && pEntry->currentText() != QString::fromUtf8(m_Info.m_States[iRow].m_sEntry.GetData()))
          {
            EmitStateEdited(iRow);
          }
        });

      m_pStatesTable->setCellWidget(iRow, 1, pEntry);
    }

    // condition combo
    {
      QComboBox* pCondition = new QComboBox(m_pStatesTable);
      for (plInt32 i = plUiStateCondition::IsTrue; i <= plUiStateCondition::GreaterEqual; ++i)
      {
        pCondition->addItem(QString::fromUtf8(GetConditionDisplay(i)), i);
      }
      pCondition->setCurrentIndex(plMath::Clamp(state.m_iCondition, 0, static_cast<plInt32>(plUiStateCondition::GreaterEqual)));
      connect(pCondition, &QComboBox::activated, this, [this, iRow](int)
        {
          if (!m_bIgnoreChanges)
          {
            EmitStateEdited(iRow);
          }
        });
      m_pStatesTable->setCellWidget(iRow, 2, pCondition);
    }

    // number OR text operand: non-numeric input compares the entry as a STRING
    // (flow states like Ui_Active == Start)
    QTableWidgetItem* pValueItem = new QTableWidgetItem(
      state.m_sValueText.IsEmpty() ? QString::number(state.m_fValue) : QString::fromUtf8(state.m_sValueText.GetData()));
    pValueItem->setToolTip(plMakeQString(plTranslate("GameUi.States.ValueTooltip")));
    if (!ConditionUsesValue(state.m_iCondition))
    {
      pValueItem->setText(QStringLiteral("-"));
      pValueItem->setForeground(QBrush(plUiEditorTheme::GetDimTextColor()));
    }
    m_pStatesTable->setItem(iRow, 3, pValueItem);

    // style asset picker: browse button + clear button (standard asset browser dialog)
    {
      QWidget* pContainer = new QWidget(m_pStatesTable);
      QHBoxLayout* pCellLayout = new QHBoxLayout(pContainer);
      pCellLayout->setContentsMargins(2, 1, 2, 1);
      pCellLayout->setSpacing(2);

      QPushButton* pPick = new QPushButton(state.m_sStyleName.IsEmpty() ? plMakeQString(plTranslate("GameUi.States.PickStyle")) : QString::fromUtf8(state.m_sStyleName.GetData()), pContainer);
      pPick->setToolTip(plMakeQString(plTranslate("GameUi.States.StyleTooltip")));
      pPick->setStyleSheet(state.m_sStyleName.IsEmpty() ? QStringLiteral("color: #8e8e8e;") : QStringLiteral("color: #b996cd;"));
      connect(pPick, &QPushButton::clicked, this, [this, iRow]()
        {
          if (iRow >= static_cast<int>(m_Info.m_States.GetCount()))
            return;

          const StateRow& row = m_Info.m_States[iRow];

          plUuid currentGuid;
          if (plConversionUtils::IsStringUuid(row.m_sStyleRef))
          {
            currentGuid = plConversionUtils::ConvertStringToUuid(row.m_sStyleRef);
          }

          plQtAssetBrowserDlg dlg(this, currentGuid, "CompatibleAsset_Ui_Style");
          if (dlg.exec() == 0 || !dlg.GetSelectedAssetGuid().IsValid())
            return;

          plStringBuilder sGuid;
          plConversionUtils::ToString(dlg.GetSelectedAssetGuid(), sGuid);

          Q_EMIT StateStyleChanged(row.m_Guid, QString::fromUtf8(sGuid.GetData()));
        });
      pCellLayout->addWidget(pPick, 1);

      if (!state.m_sStyleName.IsEmpty())
      {
        QPushButton* pClear = new QPushButton(QStringLiteral("x"), pContainer);
        pClear->setFixedWidth(20);
        pClear->setToolTip(plMakeQString(plTranslate("GameUi.States.ClearStyleTooltip")));
        connect(pClear, &QPushButton::clicked, this, [this, iRow]()
          {
            if (iRow < static_cast<int>(m_Info.m_States.GetCount()))
            {
              Q_EMIT StateStyleChanged(m_Info.m_States[iRow].m_Guid, QString());
            }
          });
        pCellLayout->addWidget(pClear);
      }

      m_pStatesTable->setCellWidget(iRow, 4, pContainer);
    }

    // clip combo: the state plays this clip while active
    {
      QComboBox* pClip = new QComboBox(m_pStatesTable);
      pClip->setToolTip(plMakeQString(plTranslate("GameUi.States.ClipTooltip")));
      pClip->addItem(QStringLiteral("-"), QString());
      for (const plString& sClip : m_ClipNames)
      {
        pClip->addItem(QString::fromUtf8(sClip.GetData()), QString::fromUtf8(sClip.GetData()));
      }

      const int iIndex = pClip->findData(QString::fromUtf8(state.m_sPlayClip.GetData()));
      pClip->setCurrentIndex(iIndex >= 0 ? iIndex : 0);

      connect(pClip, &QComboBox::activated, this, [this, iRow](int)
        {
          if (!m_bIgnoreChanges)
          {
            EmitStateEdited(iRow);
          }
        });
      m_pStatesTable->setCellWidget(iRow, 5, pClip);
    }

    // visibility action combo: what the active state does to the element's visibility
    {
      QComboBox* pVisibility = new QComboBox(m_pStatesTable);
      pVisibility->setToolTip(plMakeQString(plTranslate("GameUi.States.VisibilityTooltip")));
      pVisibility->addItem(plMakeQString(plTranslate("GameUi.States.VisNoChange")), 0);
      pVisibility->addItem(plMakeQString(plTranslate("GameUi.States.VisShow")), 1);
      pVisibility->addItem(plMakeQString(plTranslate("GameUi.States.VisHide")), 2);
      pVisibility->setCurrentIndex(plMath::Clamp(state.m_iVisibility, 0, 2));
      connect(pVisibility, &QComboBox::activated, this, [this, iRow](int)
        {
          if (!m_bIgnoreChanges)
          {
            EmitStateEdited(iRow);
          }
        });
      m_pStatesTable->setCellWidget(iRow, 6, pVisibility);
    }

    // preview checkbox: writes a value that satisfies / clears the condition
    {
      QWidget* pContainer = new QWidget(m_pStatesTable);
      QHBoxLayout* pCellLayout = new QHBoxLayout(pContainer);
      pCellLayout->setContentsMargins(6, 0, 6, 0);

      QCheckBox* pPreview = new QCheckBox(pContainer);
      pPreview->setToolTip(plMakeQString(plTranslate("GameUi.States.TestTooltip")));
      pPreview->setEnabled(!state.m_sEntry.IsEmpty());
      connect(pPreview, &QCheckBox::toggled, this, [this, iRow](bool bChecked)
        {
          if (!m_bIgnoreChanges && iRow < static_cast<int>(m_Info.m_States.GetCount()))
          {
            const StateRow& row = m_Info.m_States[iRow];
            if (!row.m_sEntry.IsEmpty())
            {
              // string operands satisfy with the text itself (clear with a mismatching suffix)
              if (!row.m_sValueText.IsEmpty() && (row.m_iCondition == plUiStateCondition::Equal || row.m_iCondition == plUiStateCondition::NotEqual))
              {
                const bool bWriteMatch = (row.m_iCondition == plUiStateCondition::Equal) == bChecked;
                QString sValue = QString::fromUtf8(row.m_sValueText.GetData());
                if (!bWriteMatch)
                  sValue += QStringLiteral("_x");
                Q_EMIT TestValueChanged(QString::fromUtf8(row.m_sEntry.GetData()), sValue);
              }
              else
              {
                Q_EMIT TestValueChanged(QString::fromUtf8(row.m_sEntry.GetData()), ComputeConditionValue(row.m_iCondition, row.m_fValue, bChecked));
              }
            }
          }
        });
      pCellLayout->addWidget(pPreview, 0, Qt::AlignCenter);

      m_pStatesTable->setCellWidget(iRow, 7, pContainer);
    }
  }
}

void plQtUiStatesPanel::EmitStateEdited(int iRow)
{
  if (iRow < 0 || iRow >= static_cast<int>(m_Info.m_States.GetCount()))
    return;

  const plUuid stateGuid = m_Info.m_States[iRow].m_Guid;

  const QString sName = m_pStatesTable->item(iRow, 0) != nullptr ? m_pStatesTable->item(iRow, 0)->text() : QString();

  QString sEntry;
  if (QComboBox* pEntry = qobject_cast<QComboBox*>(m_pStatesTable->cellWidget(iRow, 1)))
  {
    sEntry = pEntry->currentText();
  }

  int iCondition = 0;
  if (QComboBox* pCondition = qobject_cast<QComboBox*>(m_pStatesTable->cellWidget(iRow, 2)))
  {
    iCondition = pCondition->currentData().toInt();
  }

  // the Value cell accepts a number OR a text operand ("Start", "XBOX"): non-numeric
  // input becomes the string comparison of Equal/NotEqual
  double fValue = 0.0;
  QString sValueText;
  if (m_pStatesTable->item(iRow, 3) != nullptr)
  {
    const QString sRaw = m_pStatesTable->item(iRow, 3)->text().trimmed();

    bool bIsNumber = false;
    const double fParsed = sRaw.toDouble(&bIsNumber);

    if (bIsNumber || sRaw.isEmpty() || sRaw == QStringLiteral("-"))
    {
      fValue = fParsed;
    }
    else
    {
      sValueText = sRaw;
    }
  }

  int iVisibility = 0;
  if (QComboBox* pVisibility = qobject_cast<QComboBox*>(m_pStatesTable->cellWidget(iRow, 6)))
  {
    iVisibility = pVisibility->currentData().toInt();
  }

  QString sPlayClip;
  if (QComboBox* pClip = qobject_cast<QComboBox*>(m_pStatesTable->cellWidget(iRow, 5)))
  {
    sPlayClip = pClip->currentData().toString();
  }

  Q_EMIT StateEdited(stateGuid, sName.trimmed(), sEntry, iCondition, fValue, sValueText, iVisibility, sPlayClip);
}

void plQtUiStatesPanel::RebuildChips()
{
  // clear the previous chips
  while (QLayoutItem* pItem = m_pChipLayout->takeAt(0))
  {
    delete pItem->widget();
    delete pItem;
  }

  if (!m_bHasElement)
    return;

  m_pChipLayout->addWidget(MakeChip(m_pChipRow, plMakeQString(plTranslate("GameUi.States.Normal")), false));

  if (m_Info.m_bIsButton)
  {
    m_pChipLayout->addWidget(MakeArrow(m_pChipRow, plMakeQString(plTranslate("GameUi.States.HoverCondition"))));
    m_pChipLayout->addWidget(MakeChip(m_pChipRow, plMakeQString(plTranslate("GameUi.States.Hover")), false));
    m_pChipLayout->addWidget(MakeArrow(m_pChipRow, plMakeQString(plTranslate("GameUi.States.PressedCondition"))));
    m_pChipLayout->addWidget(MakeChip(m_pChipRow, plMakeQString(plTranslate("GameUi.States.Pressed")), false));
  }

  // the state machine: first matching condition wins
  for (const StateRow& state : m_Info.m_States)
  {
    m_pChipLayout->addWidget(MakeArrow(m_pChipRow, MakeConditionText(state)));

    plStringBuilder sChip(state.m_sName.IsEmpty() ? "?" : state.m_sName.GetData());

    if (!state.m_sPlayClip.IsEmpty())
    {
      sChip.AppendFormat("  > {}", state.m_sPlayClip);
    }

    m_pChipLayout->addWidget(MakeChip(m_pChipRow, QString::fromUtf8(sChip.GetData()), true));
  }

  if (m_Info.m_bIsButton && !m_Info.m_sDisabledBinding.IsEmpty())
  {
    m_pChipLayout->addSpacing(14);
    m_pChipLayout->addWidget(MakeArrow(m_pChipRow, QString::fromUtf8(m_Info.m_sDisabledBinding.GetData())));
    m_pChipLayout->addWidget(MakeChip(m_pChipRow, plMakeQString(plTranslate("GameUi.States.Disabled")), false, true));
  }

  m_pChipLayout->addStretch();
}
