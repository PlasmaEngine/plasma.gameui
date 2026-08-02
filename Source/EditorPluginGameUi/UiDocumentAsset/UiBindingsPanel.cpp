#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiBindingsPanel.moc.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEditorTheme.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
  constexpr int s_iGuidRole = Qt::UserRole;
  constexpr int s_iBindingPropRole = Qt::UserRole + 1;

  QString GuidToString(const plUuid& guid)
  {
    plStringBuilder tmp;
    return QString(plConversionUtils::ToString(guid, tmp).GetData());
  }
} // namespace

plQtUiBindingsPanel::plQtUiBindingsPanel(QWidget* pParent)
  : QWidget(pParent)
{
  QHBoxLayout* pLayout = new QHBoxLayout(this);
  pLayout->setContentsMargins(4, 4, 4, 4);
  pLayout->setSpacing(8);

  // ————— left: bindings management —————
  {
    QVBoxLayout* pLeft = nullptr;
    QFrame* pPanel = plUiEditorTheme::CreatePanel(this, plUiEditorTheme::szAccentBind, plMakeQString(plTranslate("GameUi.Section.Bindings")), pLeft);

    m_pBindingsTable = new QTableWidget(this);
    m_pBindingsTable->setStyleSheet(plUiEditorTheme::szTableStyle);
    m_pBindingsTable->setColumnCount(3);
    m_pBindingsTable->setHorizontalHeaderLabels({plMakeQString(plTranslate("GameUi.Bindings.ColElement")),
      plMakeQString(plTranslate("GameUi.Bindings.ColProperty")), plMakeQString(plTranslate("GameUi.Bindings.ColEntry"))});
    m_pBindingsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pBindingsTable->verticalHeader()->setVisible(false);
    m_pBindingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pBindingsTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    connect(m_pBindingsTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* pItem)
      {
        if (m_bIgnoreChanges || pItem->column() != 2)
          return;

        plUuid guid;
        QString sBindingProp;
        if (GetRowPayload(pItem->row(), guid, sBindingProp))
        {
          Q_EMIT BindingEntryEdited(guid, sBindingProp, pItem->text());
        }
      });

    connect(m_pBindingsTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* pItem)
      {
        // entry cell double-click starts inline editing instead
        if (pItem->column() == 2)
          return;

        plUuid guid;
        QString sBindingProp;
        if (GetRowPayload(pItem->row(), guid, sBindingProp))
        {
          Q_EMIT BindingRowActivated(guid);
        }
      });

    pLeft->addWidget(m_pBindingsTable, 1);

    QHBoxLayout* pButtons = new QHBoxLayout();

    QPushButton* pAdd = new QPushButton(plMakeQString(plTranslate("GameUi.Bindings.AddBinding")), this);
    connect(pAdd, &QPushButton::clicked, this, [this]() { Q_EMIT AddBindingRequested(); });
    pButtons->addWidget(pAdd);

    QPushButton* pRemove = new QPushButton(plMakeQString(plTranslate("GameUi.Bindings.RemoveBinding")), this);
    connect(pRemove, &QPushButton::clicked, this, [this]() { OnRemoveBinding(); });
    pButtons->addWidget(pRemove);

    pButtons->addStretch();
    pLeft->addLayout(pButtons);

    pLayout->addWidget(pPanel, 1);
  }

  // ————— right: editable mock blackboard —————
  {
    QVBoxLayout* pRight = nullptr;
    QFrame* pPanel = plUiEditorTheme::CreatePanel(this, plUiEditorTheme::szBrand, plMakeQString(plTranslate("GameUi.Section.MockData")), pRight);
    pPanel->setToolTip(plMakeQString(plTranslate("GameUi.Bindings.MockTooltip")));

    // scenario chips: Default + saved presets + Save (mockup's "Default / Low Health / + Save...")
    m_pScenarioLayout = new QHBoxLayout();
    m_pScenarioLayout->setSpacing(3);
    pRight->addLayout(m_pScenarioLayout);
    SetScenarios(QStringList());

    m_pMockTable = new QTableWidget(this);
    m_pMockTable->setStyleSheet(plUiEditorTheme::szTableStyle);
    m_pMockTable->setColumnCount(3);
    m_pMockTable->setHorizontalHeaderLabels({plMakeQString(plTranslate("GameUi.Bindings.ColEntry")),
      plMakeQString(plTranslate("GameUi.Bindings.ColValue")), QString()});
    m_pMockTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pMockTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_pMockTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_pMockTable->setColumnWidth(2, 90);
    m_pMockTable->verticalHeader()->setVisible(false);
    m_pMockTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pMockTable->setToolTip(plMakeQString(plTranslate("GameUi.Bindings.MockTooltip")));

    connect(m_pMockTable, &QTableWidget::itemChanged, this, [this]()
      {
        if (!m_bIgnoreChanges)
        {
          RefreshRowStyles();
          UpdateValueSliders();
          Q_EMIT MockDataChanged();
        }
      });

    pRight->addWidget(m_pMockTable, 1);

    QHBoxLayout* pButtons = new QHBoxLayout();

    QPushButton* pAdd = new QPushButton(plMakeQString(plTranslate("GameUi.Bindings.AddEntry")), this);
    connect(pAdd, &QPushButton::clicked, this, [this]() { OnAddMockEntry(); });
    pButtons->addWidget(pAdd);

    QPushButton* pRemove = new QPushButton(plMakeQString(plTranslate("GameUi.Bindings.RemoveEntry")), this);
    connect(pRemove, &QPushButton::clicked, this, [this]() { OnRemoveMockEntry(); });
    pButtons->addWidget(pRemove);

    QPushButton* pReset = new QPushButton(plMakeQString(plTranslate("GameUi.Bindings.ResetValues")), this);
    pReset->setToolTip(plMakeQString(plTranslate("GameUi.Bindings.ResetValuesTooltip")));
    connect(pReset, &QPushButton::clicked, this, [this]() { OnResetValues(); });
    pButtons->addWidget(pReset);

    pButtons->addStretch();
    pRight->addLayout(pButtons);

    pLayout->addWidget(pPanel, 1);
  }
}

void plQtUiBindingsPanel::SetTemplateEntries(plArrayPtr<const MockEntry> entries)
{
  m_TemplateEntries.Clear();
  m_TemplateEntries.PushBackRange(entries);
}

bool plQtUiBindingsPanel::IsTemplateEntry(plStringView sName, plStringView* out_pInitialValue) const
{
  for (const MockEntry& entry : m_TemplateEntries)
  {
    if (entry.m_sName == sName)
    {
      if (out_pInitialValue != nullptr)
      {
        *out_pInitialValue = entry.m_sValue;
      }
      return true;
    }
  }

  return false;
}

void plQtUiBindingsPanel::SetMockValueSilently(plStringView sName, plStringView sValue)
{
  m_bIgnoreChanges = true;

  bool bFound = false;

  plStringBuilder tmp;
  for (int i = 0; i < m_pMockTable->rowCount(); ++i)
  {
    const QTableWidgetItem* pName = m_pMockTable->item(i, 0);
    if (pName == nullptr)
      continue;

    tmp = pName->text().toUtf8().data();
    if (tmp == sName)
    {
      if (QTableWidgetItem* pValue = m_pMockTable->item(i, 1))
      {
        tmp = sValue;
        pValue->setText(tmp.GetData());
      }
      bFound = true;
      break;
    }
  }

  if (!bFound)
  {
    const int iRow = m_pMockTable->rowCount();
    m_pMockTable->insertRow(iRow);
    tmp = sName;
    m_pMockTable->setItem(iRow, 0, new QTableWidgetItem(tmp.GetData()));
    tmp = sValue;
    m_pMockTable->setItem(iRow, 1, new QTableWidgetItem(tmp.GetData()));
  }

  RefreshRowStyles();
  UpdateValueSliders();

  m_bIgnoreChanges = false;
}

void plQtUiBindingsPanel::OnResetValues()
{
  m_bIgnoreChanges = true;

  bool bChanged = false;

  for (int i = 0; i < m_pMockTable->rowCount(); ++i)
  {
    const QTableWidgetItem* pName = m_pMockTable->item(i, 0);
    QTableWidgetItem* pValue = m_pMockTable->item(i, 1);

    if (pName == nullptr || pValue == nullptr)
      continue;

    const plString sName = pName->text().toUtf8().data();

    plStringView sInitial;
    if (IsTemplateEntry(sName, &sInitial))
    {
      const plStringBuilder sInitialValue(sInitial);
      if (pValue->text() != QString(sInitialValue.GetData()))
      {
        pValue->setText(sInitialValue.GetData());
        bChanged = true;
      }
    }
  }

  UpdateValueSliders();

  m_bIgnoreChanges = false;

  if (bChanged)
  {
    Q_EMIT MockDataChanged();
  }
}

bool plQtUiBindingsPanel::GetRowPayload(int iRow, plUuid& out_elementGuid, QString& out_sBindingProperty) const
{
  const QTableWidgetItem* pItem = m_pBindingsTable->item(iRow, 0);
  if (pItem == nullptr)
    return false;

  out_elementGuid = plConversionUtils::ConvertStringToUuid(pItem->data(s_iGuidRole).toString().toUtf8().data());
  out_sBindingProperty = pItem->data(s_iBindingPropRole).toString();

  return out_elementGuid.IsValid() && !out_sBindingProperty.isEmpty();
}

void plQtUiBindingsPanel::SetBindings(plArrayPtr<const BindingInfo> bindings)
{
  m_bIgnoreChanges = true;

  m_Bindings.Clear();
  m_Bindings.PushBackRange(bindings);

  // ————— bindings table —————
  m_pBindingsTable->setRowCount(static_cast<int>(bindings.GetCount()));

  for (plUInt32 i = 0; i < bindings.GetCount(); ++i)
  {
    // mockup .brow look: bright monospace element, dim property, cyan entry chip
    QFont monoFont;
    monoFont.setFamily("Consolas");

    QTableWidgetItem* pElementItem = new QTableWidgetItem(bindings[i].m_sElement.GetData());
    pElementItem->setFlags(pElementItem->flags() & ~Qt::ItemIsEditable);
    pElementItem->setData(s_iGuidRole, GuidToString(bindings[i].m_ElementGuid));
    pElementItem->setData(s_iBindingPropRole, QString(bindings[i].m_sBindingProperty.GetData()));
    pElementItem->setFont(monoFont);
    pElementItem->setForeground(QBrush(QColor(0xff, 0xff, 0xff)));

    QTableWidgetItem* pPropertyItem = new QTableWidgetItem(bindings[i].m_sProperty.GetData());
    pPropertyItem->setFlags(pPropertyItem->flags() & ~Qt::ItemIsEditable);
    pPropertyItem->setFont(monoFont);
    pPropertyItem->setForeground(QBrush(plUiEditorTheme::GetDimTextColor()));

    QTableWidgetItem* pEntryItem = new QTableWidgetItem(bindings[i].m_sEntry.GetData()); // editable inline
    pEntryItem->setFont(monoFont);
    pEntryItem->setForeground(QBrush(QColor(0x7f, 0xd8, 0xe8)));

    // rows without a binding property (e.g. animations) are managed elsewhere: keep them read-only
    if (bindings[i].m_sBindingProperty.IsEmpty())
    {
      pEntryItem->setFlags(pEntryItem->flags() & ~Qt::ItemIsEditable);
    }

    m_pBindingsTable->setItem(i, 0, pElementItem);
    m_pBindingsTable->setItem(i, 1, pPropertyItem);
    m_pBindingsTable->setItem(i, 2, pEntryItem);
  }

  // ————— mirror template + bound entries into the mock table —————
  bool bMockChanged = false;
  MirrorEntriesIntoMockTable(bMockChanged);

  RefreshRowStyles();
  UpdateValueSliders();

  m_bIgnoreChanges = false;

  if (bMockChanged)
  {
    Q_EMIT MockDataChanged();
  }
}

void plQtUiBindingsPanel::MirrorEntriesIntoMockTable(bool& ref_bMockChanged)
{
  plHybridArray<plString, 16> existing;
  for (int i = 0; i < m_pMockTable->rowCount(); ++i)
  {
    if (const QTableWidgetItem* pName = m_pMockTable->item(i, 0))
    {
      existing.PushBack(pName->text().toUtf8().data());
    }
  }

  auto addRow = [&](const plString& sName, const plString& sValue)
  {
    existing.PushBack(sName);

    const int iRow = m_pMockTable->rowCount();
    m_pMockTable->insertRow(iRow);
    m_pMockTable->setItem(iRow, 0, new QTableWidgetItem(sName.GetData()));
    m_pMockTable->setItem(iRow, 1, new QTableWidgetItem(sValue.GetData()));
    ref_bMockChanged = true;
  };

  // template entries first (the design-time contract), seeded with their initial values
  for (const MockEntry& entry : m_TemplateEntries)
  {
    if (entry.m_sName.IsEmpty() || existing.Contains(entry.m_sName))
      continue;

    addRow(entry.m_sName, entry.m_sValue);
  }

  // bound entries that are not in the template (still useful, flagged by styling)
  for (const BindingInfo& binding : m_Bindings)
  {
    if (binding.m_sEntry.IsEmpty() || existing.Contains(binding.m_sEntry))
      continue;

    addRow(binding.m_sEntry, binding.m_sSeedValue);
  }
}

void plQtUiBindingsPanel::RefreshRowStyles()
{
  const bool bWasIgnoring = m_bIgnoreChanges;
  m_bIgnoreChanges = true;

  const bool bHasTemplate = !m_TemplateEntries.IsEmpty();

  // mock rows that are not part of the template: grey + italic
  for (int i = 0; i < m_pMockTable->rowCount(); ++i)
  {
    QTableWidgetItem* pName = m_pMockTable->item(i, 0);
    QTableWidgetItem* pValue = m_pMockTable->item(i, 1);

    if (pName == nullptr)
      continue;

    const plString sName = pName->text().toUtf8().data();
    const bool bInTemplate = !bHasTemplate || IsTemplateEntry(sName);

    const QString sTooltip = bInTemplate ? QString() : plMakeQString(plTranslate("GameUi.Bindings.NotInTemplateTooltip"));

    // entry names in monospace cyan (mockup .mrow .k); off-template rows dim + italic
    QFont nameFont = pName->font();
    nameFont.setItalic(!bInTemplate);
    nameFont.setFamily("Consolas");
    pName->setFont(nameFont);
    pName->setForeground(bInTemplate ? QBrush(QColor(0x7f, 0xd8, 0xe8)) : QBrush(plUiEditorTheme::GetDimTextColor()));
    pName->setToolTip(sTooltip);

    if (pValue != nullptr)
    {
      QFont valueFont = pValue->font();
      valueFont.setItalic(!bInTemplate);
      pValue->setFont(valueFont);
      pValue->setForeground(bInTemplate ? palette().text() : QBrush(plUiEditorTheme::GetDimTextColor()));
      pValue->setToolTip(sTooltip);
    }
  }

  // binding rows whose entry is unknown to the template: warning color
  if (bHasTemplate)
  {
    const QBrush warnBrush(QColor(0xE0, 0x9E, 0x3E));

    for (int i = 0; i < m_pBindingsTable->rowCount(); ++i)
    {
      QTableWidgetItem* pEntryItem = m_pBindingsTable->item(i, 2);
      if (pEntryItem == nullptr)
        continue;

      const plString sEntry = pEntryItem->text().toUtf8().data();
      const bool bKnown = IsTemplateEntry(sEntry);

      pEntryItem->setForeground(bKnown ? QBrush(QColor(0x7f, 0xd8, 0xe8)) : warnBrush);
      pEntryItem->setToolTip(bKnown ? QString() : plMakeQString(plTranslate("GameUi.Bindings.NotInTemplateTooltip")));
    }
  }

  m_bIgnoreChanges = bWasIgnoring;
}

void plQtUiBindingsPanel::UpdateValueSliders()
{
  const bool bWasIgnoring = m_bIgnoreChanges;
  m_bIgnoreChanges = true;

  for (int i = 0; i < m_pMockTable->rowCount(); ++i)
  {
    const QTableWidgetItem* pValue = m_pMockTable->item(i, 1);

    bool bNumeric = false;
    double fValue = 0.0;

    if (pValue != nullptr && !pValue->text().isEmpty())
    {
      fValue = pValue->text().toDouble(&bNumeric);
    }

    if (!bNumeric)
    {
      if (m_pMockTable->cellWidget(i, 2) != nullptr)
      {
        m_pMockTable->removeCellWidget(i, 2);
      }
      continue;
    }

    QSlider* pSlider = qobject_cast<QSlider*>(m_pMockTable->cellWidget(i, 2));

    if (pSlider == nullptr)
    {
      pSlider = new QSlider(Qt::Horizontal, m_pMockTable);
      pSlider->setRange(0, 1000);
      pSlider->setProperty("rmin", 0.0);
      pSlider->setProperty("rmax", 1.0);
      m_pMockTable->setCellWidget(i, 2, pSlider);

      connect(pSlider, &QSlider::valueChanged, this, [this, pSlider](int iValue)
        {
          if (m_bIgnoreChanges)
            return;

          // resolve the slider's CURRENT row (rows shift when entries are added/removed)
          for (int iRow = 0; iRow < m_pMockTable->rowCount(); ++iRow)
          {
            if (m_pMockTable->cellWidget(iRow, 2) == pSlider)
            {
              const double fMin = pSlider->property("rmin").toDouble();
              const double fMax = pSlider->property("rmax").toDouble();
              const double fNewValue = fMin + (fMax - fMin) * (iValue / 1000.0);

              if (QTableWidgetItem* pValueItem = m_pMockTable->item(iRow, 1))
              {
                // itemChanged fires MockDataChanged -> live preview push
                pValueItem->setText(QString::number(fNewValue, 'g', 4));
              }
              break;
            }
          }
        });
    }

    // range covers at least [0,1] and grows to include out-of-range values
    double fMin = plMath::Min(0.0, fValue);
    double fMax = plMath::Max(1.0, fValue);

    // keep a previously grown range (avoids the range jumping while sliding)
    fMin = plMath::Min(fMin, pSlider->property("rmin").toDouble());
    fMax = plMath::Max(fMax, pSlider->property("rmax").toDouble());

    pSlider->setProperty("rmin", fMin);
    pSlider->setProperty("rmax", fMax);

    QSignalBlocker blocker(pSlider);
    pSlider->setValue(static_cast<int>(((fValue - fMin) / plMath::Max(fMax - fMin, 1e-9)) * 1000.0));
  }

  m_bIgnoreChanges = bWasIgnoring;
}

void plQtUiBindingsPanel::SetMockEntries(plArrayPtr<const MockEntry> entries)
{
  m_bIgnoreChanges = true;

  bool bChanged = false;

  for (const MockEntry& entry : entries)
  {
    if (entry.m_sName.IsEmpty())
      continue;

    bool bFound = false;

    for (int i = 0; i < m_pMockTable->rowCount(); ++i)
    {
      const QTableWidgetItem* pName = m_pMockTable->item(i, 0);
      QTableWidgetItem* pValue = m_pMockTable->item(i, 1);

      if (pName == nullptr || pValue == nullptr)
        continue;

      if (plString(pName->text().toUtf8().data()) == entry.m_sName)
      {
        if (pValue->text() != QString(entry.m_sValue.GetData()))
        {
          pValue->setText(entry.m_sValue.GetData());
          bChanged = true;
        }

        bFound = true;
        break;
      }
    }

    if (!bFound)
    {
      const int iRow = m_pMockTable->rowCount();
      m_pMockTable->insertRow(iRow);
      m_pMockTable->setItem(iRow, 0, new QTableWidgetItem(entry.m_sName.GetData()));
      m_pMockTable->setItem(iRow, 1, new QTableWidgetItem(entry.m_sValue.GetData()));
      bChanged = true;
    }
  }

  RefreshRowStyles();
  UpdateValueSliders();

  m_bIgnoreChanges = false;

  if (bChanged)
  {
    Q_EMIT MockDataChanged();
  }
}

void plQtUiBindingsPanel::SetScenarios(const QStringList& names)
{
  // rebuild the chip row
  while (QLayoutItem* pItem = m_pScenarioLayout->takeAt(0))
  {
    delete pItem->widget();
    delete pItem;
  }

  const char* szChipStyle = "QPushButton { background: #2d2d2d; border: 1px solid #3c3c3c; border-radius: 8px; padding: 2px 10px; font-size: 10px; }"
                            "QPushButton:hover { border-color: #b996cd; }";

  QPushButton* pDefault = new QPushButton(plMakeQString(plTranslate("GameUi.Bindings.ScenarioDefault")), this);
  pDefault->setStyleSheet(szChipStyle);
  pDefault->setToolTip(plMakeQString(plTranslate("GameUi.Bindings.ScenarioDefaultTooltip")));
  connect(pDefault, &QPushButton::clicked, this, [this]() { OnResetValues(); });
  m_pScenarioLayout->addWidget(pDefault);

  for (const QString& sName : names)
  {
    QPushButton* pChip = new QPushButton(sName, this);
    pChip->setStyleSheet(szChipStyle);
    pChip->setToolTip(plMakeQString(plTranslate("GameUi.Bindings.ScenarioTooltip")));
    pChip->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(pChip, &QPushButton::clicked, this, [this, sName]() { Q_EMIT ScenarioSelected(sName); });
    connect(pChip, &QPushButton::customContextMenuRequested, this, [this, pChip, sName](const QPoint& pos)
      {
        QMenu menu(this);
        QAction* pDelete = menu.addAction(plMakeQString(plTranslate("GameUi.Bindings.ScenarioDelete")));

        if (menu.exec(pChip->mapToGlobal(pos)) == pDelete)
        {
          Q_EMIT ScenarioDeleteRequested(sName);
        }
      });

    m_pScenarioLayout->addWidget(pChip);
  }

  QPushButton* pSave = new QPushButton(plMakeQString(plTranslate("GameUi.Bindings.ScenarioSave")), this);
  pSave->setStyleSheet(szChipStyle);
  pSave->setToolTip(plMakeQString(plTranslate("GameUi.Bindings.ScenarioSaveTooltip")));
  connect(pSave, &QPushButton::clicked, this, [this]() { OnSaveScenario(); });
  m_pScenarioLayout->addWidget(pSave);

  m_pScenarioLayout->addStretch();
}

void plQtUiBindingsPanel::OnSaveScenario()
{
  bool bOk = false;
  const QString sName = QInputDialog::getText(this, plMakeQString(plTranslate("GameUi.Bindings.ScenarioSaveTitle")),
    plMakeQString(plTranslate("GameUi.Bindings.ScenarioSaveLabel")), QLineEdit::Normal, QString(), &bOk).trimmed();

  if (bOk && !sName.isEmpty())
  {
    Q_EMIT ScenarioSaveRequested(sName);
  }
}

void plQtUiBindingsPanel::GetMockEntries(plDynamicArray<MockEntry>& out_entries) const
{
  out_entries.Clear();

  for (int i = 0; i < m_pMockTable->rowCount(); ++i)
  {
    const QTableWidgetItem* pName = m_pMockTable->item(i, 0);
    const QTableWidgetItem* pValue = m_pMockTable->item(i, 1);

    if (pName == nullptr || pName->text().isEmpty())
      continue;

    MockEntry& entry = out_entries.ExpandAndGetRef();
    entry.m_sName = pName->text().toUtf8().data();
    entry.m_sValue = pValue != nullptr ? pValue->text().toUtf8().data() : "";
  }
}

void plQtUiBindingsPanel::OnAddMockEntry()
{
  m_bIgnoreChanges = true;

  const int iRow = m_pMockTable->rowCount();
  m_pMockTable->insertRow(iRow);
  m_pMockTable->setItem(iRow, 0, new QTableWidgetItem(""));
  m_pMockTable->setItem(iRow, 1, new QTableWidgetItem(""));

  m_bIgnoreChanges = false;

  m_pMockTable->editItem(m_pMockTable->item(iRow, 0));
}

void plQtUiBindingsPanel::OnRemoveMockEntry()
{
  const int iRow = m_pMockTable->currentRow();
  if (iRow < 0)
    return;

  m_pMockTable->removeRow(iRow);

  Q_EMIT MockDataChanged();
}

void plQtUiBindingsPanel::OnRemoveBinding()
{
  const int iRow = m_pBindingsTable->currentRow();
  if (iRow < 0)
    return;

  plUuid guid;
  QString sBindingProp;
  if (GetRowPayload(iRow, guid, sBindingProp))
  {
    Q_EMIT RemoveBindingRequested(guid, sBindingProp);
  }
}
