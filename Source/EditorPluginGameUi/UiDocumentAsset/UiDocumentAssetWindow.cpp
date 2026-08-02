#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Assets/AssetStatusIndicator.moc.h>
#include <EditorFramework/DocumentWindow/EngineViewWidget.moc.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetWindow.moc.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEditorTheme.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameEngine/Utils/BlackboardTemplateResource.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>
#include <GameUiPlugin/Elements/UiLayout.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <GuiFoundation/ActionViews/MenuBarActionMapView.moc.h>
#include <GuiFoundation/ActionViews/ToolBarActionMapView.moc.h>
#include <GuiFoundation/ContainerWindow/ContainerWindow.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>
#include <ToolsFoundation/Project/ToolsProject.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

#include <ads/DockAreaWidget.h>

#include <QActionGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

namespace
{
  // must match the preview component's default canvas scaling (ScaleWithHeight, 1080)
  constexpr float s_fReferenceHeight = 1080.0f;
  constexpr const char* szUiWidgetMimeType = "application/x-plasmaui-widget";
  constexpr float s_fResizeHandleZonePx = 8.0f;

  QColor GetElementTypeColor(const plRTTI* pType)
  {
    // widget-kind colors from plColorScheme::LightUI (see Documentation/GameUI)
    if (pType->IsDerivedFrom<plUiText>())
      return QColor(0xFC, 0xC4, 0x19); // Yellow: text
    if (pType->IsDerivedFrom<plUiButton>() || pType->IsDerivedFrom<plUiProgressBar>() || pType->IsDerivedFrom<plUiTextInput>())
      return QColor(0xFF, 0x92, 0x2B); // Orange: controls
    if (pType->IsDerivedFrom<plUiImage>())
      return QColor(0x94, 0xD8, 0x2D); // Lime: visual
    if (pType->IsDerivedFrom<plUiSubDocument>())
      return QColor(0xB9, 0x96, 0xCD); // Lavender: assets
    if (pType->IsDerivedFrom<plUiPanel>())
      return QColor(0x33, 0x9A, 0xF0); // Blue: containers

    return QColor(0xAD, 0xB5, 0xBD);
  }

  /// eye/lock glyphs matching the scenegraph panel's iconography (ScenegraphDelegate.cpp)
  QIcon MakeEyeIcon(bool bVisible)
  {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setOpacity(bVisible ? 0.45 : 0.90);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor penColor = bVisible ? QColor(200, 200, 208) : QColor(150, 150, 158);
    QPen pen(penColor);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal cx = 8.0, cy = 8.0;
    const qreal hw = 16 * 0.44;
    const qreal hh = 16 * 0.30;

    QPainterPath eyePath;
    eyePath.moveTo(cx - hw, cy);
    eyePath.quadTo(cx, cy - hh * 2.2, cx + hw, cy);
    eyePath.quadTo(cx, cy + hh * 2.2, cx - hw, cy);
    p.drawPath(eyePath);

    const qreal pr = hh * 0.72;
    p.setBrush(penColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(cx, cy), pr, pr);

    if (!bVisible)
    {
      p.setBrush(Qt::NoBrush);
      QPen slashPen(QColor(200, 80, 80));
      slashPen.setWidthF(1.6);
      slashPen.setCapStyle(Qt::RoundCap);
      p.setPen(slashPen);
      const qreal d = 16 * 0.38;
      p.drawLine(QPointF(cx - d, cy + d * 0.8), QPointF(cx + d, cy - d * 0.8));
    }

    return QIcon(pixmap);
  }

  QIcon MakeHierarchyLockIcon(bool bLocked)
  {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setOpacity(bLocked ? 0.95 : 0.35);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor color = bLocked ? QColor(220, 120, 110) : QColor(175, 175, 185);
    QPen pen(color);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    const qreal cx = 8.0;
    const qreal top = 16 * 0.05;
    const qreal bottom = 16 * 0.97;
    const qreal totalH = bottom - top;

    const qreal bodyTopY = top + totalH * 0.43;
    const qreal bodyW = 16 * 0.65;
    const qreal bodyH = totalH * 0.55;
    const qreal shackleW = bodyW * 0.56;
    const qreal shackleR = shackleW * 0.5;

    QRectF bodyRect(cx - bodyW * 0.5, bodyTopY, bodyW, bodyH);
    p.setBrush(bLocked ? QColor(220, 120, 110, 160) : Qt::NoBrush);
    p.drawRoundedRect(bodyRect, 2.0, 2.0);
    p.setBrush(Qt::NoBrush);

    const qreal shackleBaseY = bodyTopY + 1.5;
    const qreal archTop = top + totalH * 0.02;
    const qreal archH = (shackleBaseY - archTop) * 2.0;
    QRectF archRect(cx - shackleR, archTop, shackleW, archH);

    QPainterPath path;
    path.moveTo(cx - shackleR, shackleBaseY);
    path.arcTo(archRect, 180, -180);

    if (bLocked)
    {
      path.lineTo(cx + shackleR, shackleBaseY); // closed shackle
    }
    else
    {
      path.lineTo(cx + shackleR, archTop - totalH * 0.12); // right leg lifted
    }

    p.drawPath(path);

    return QIcon(pixmap);
  }

  QIcon MakeElementTypeIcon(const QColor& color)
  {
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(1, 1, 10, 10, 2, 2);

    return QIcon(pixmap);
  }

  /// Pictographic palette icon per widget type (mockup-style, not just text).
  QPixmap MakePaletteWidgetPixmap(const plRTTI* pType)
  {
    constexpr int size = 28;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor color = GetElementTypeColor(pType);
    QPen pen(color);
    pen.setWidthF(1.6);

    if (pType->IsDerivedFrom<plUiTextInput>())
    {
      // entry box with a caret
      painter.setPen(pen);
      painter.drawRoundedRect(4, 9, 20, 11, 3, 3);
      painter.setBrush(color);
      painter.drawLine(9, 12, 9, 17); // caret
      painter.drawLine(13, 17, 20, 17); // entered text baseline hint
    }
    else if (pType->IsDerivedFrom<plUiText>())
    {
      painter.setPen(pen);
      painter.drawLine(6, 8, 22, 8);   // T bar
      painter.drawLine(14, 8, 14, 22); // T stem
    }
    else if (pType->IsDerivedFrom<plUiToggle>())
    {
      // pill with a knob on the right (switch)
      painter.setPen(pen);
      painter.setBrush(QColor(color.red(), color.green(), color.blue(), 60));
      painter.drawRoundedRect(4, 10, 20, 9, 4, 4);
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPoint(19, 14), 3, 3);
    }
    else if (pType->IsDerivedFrom<plUiSlider>())
    {
      // track with a handle
      painter.setPen(pen);
      painter.drawLine(4, 14, 24, 14);
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      painter.drawRect(14, 9, 5, 10);
    }
    else if (pType->IsDerivedFrom<plUiItemList>())
    {
      // data rows behind a scrollbar, with filled row chips (data-driven list)
      painter.setPen(pen);
      painter.drawRect(4, 5, 16, 18);
      painter.setBrush(QColor(color.red(), color.green(), color.blue(), 90));
      painter.setPen(Qt::NoPen);
      painter.drawRect(6, 7, 12, 4);
      painter.drawRect(6, 12, 12, 4);
      painter.drawRect(6, 17, 12, 4);
      painter.setBrush(color);
      painter.drawRect(22, 8, 2, 8); // thumb
    }
    else if (pType->IsDerivedFrom<plUiScrollView>())
    {
      // stacked rows behind a scrollbar
      painter.setPen(pen);
      painter.drawRect(4, 5, 16, 18);
      painter.drawLine(6, 9, 18, 9);
      painter.drawLine(6, 14, 18, 14);
      painter.drawLine(6, 19, 18, 19);
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      painter.drawRect(22, 8, 2, 8); // thumb
    }
    else if (pType->IsDerivedFrom<plUiButton>())
    {
      painter.setPen(pen);
      painter.setBrush(QColor(color.red(), color.green(), color.blue(), 60));
      painter.drawRoundedRect(4, 8, 20, 12, 4, 4);
      painter.drawLine(10, 14, 18, 14);
    }
    else if (pType->IsDerivedFrom<plUiProgressBar>())
    {
      painter.setPen(pen);
      painter.drawRect(4, 11, 20, 7);
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawRect(5, 12, 12, 5);
    }
    else if (pType->IsDerivedFrom<plUiImage>())
    {
      painter.setPen(pen);
      painter.drawRect(5, 6, 18, 16);
      painter.setBrush(color);
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPoint(11, 11), 2, 2);
      QPolygon mountain;
      mountain << QPoint(7, 20) << QPoint(13, 13) << QPoint(17, 17) << QPoint(20, 14) << QPoint(22, 20);
      painter.drawPolygon(mountain);
    }
    else if (pType->IsDerivedFrom<plUiStackPanel>())
    {
      painter.setPen(pen);
      painter.drawRect(5, 5, 18, 4);
      painter.drawRect(5, 12, 18, 4);
      painter.drawRect(5, 19, 18, 4);
    }
    else if (pType->IsDerivedFrom<plUiGridPanel>())
    {
      painter.setPen(pen);
      painter.drawRect(5, 6, 8, 7);
      painter.drawRect(15, 6, 8, 7);
      painter.drawRect(5, 15, 8, 7);
      painter.drawRect(15, 15, 8, 7);
    }
    else if (pType->IsDerivedFrom<plUiSubDocument>())
    {
      // a document nested inside a frame
      painter.setPen(pen);
      painter.drawRect(4, 5, 20, 18);
      painter.setBrush(QColor(color.red(), color.green(), color.blue(), 70));
      painter.drawRect(9, 10, 10, 8);
      painter.drawLine(9, 21, 13, 21);
    }
    else if (pType->IsDerivedFrom<plUiSpacer>() || pType == plGetStaticRTTI<plUiElement>())
    {
      // spacer: dashed box with a vertical gap arrow
      pen.setStyle(Qt::DotLine);
      painter.setPen(pen);
      painter.drawRect(5, 6, 18, 16);
      pen.setStyle(Qt::SolidLine);
      painter.setPen(pen);
      painter.drawLine(14, 9, 14, 19);
      painter.drawLine(11, 12, 14, 9);
      painter.drawLine(17, 12, 14, 9);
      painter.drawLine(11, 16, 14, 19);
      painter.drawLine(17, 16, 14, 19);
    }
    else // panel / container
    {
      painter.setPen(pen);
      painter.drawRoundedRect(4, 5, 20, 18, 2, 2);
      pen.setStyle(Qt::DotLine);
      painter.setPen(pen);
      painter.drawRect(8, 9, 12, 10);
    }

    return pixmap;
  }

  /// Small preview pixmap for one anchor preset button: outer rect = parent, inner marker = anchor placement.
  QPixmap MakeAnchorPresetPixmap(plInt8 iCol, plInt8 iRow)
  {
    constexpr int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setPen(QColor(110, 110, 110));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(2, 2, size - 5, size - 5);

    const QColor accent(0xB9, 0x96, 0xCD); // GetCategoryColor("UI") PastelLavender
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);

    constexpr int inner = 5;
    const int minPos = 4;
    const int midPos = (size - inner) / 2;
    const int maxPos = size - 4 - inner;

    int x = midPos, y = midPos, w = inner, h = inner;

    if (iCol == 0)
      x = minPos;
    else if (iCol == 2)
      x = maxPos;
    else if (iCol == 3)
    {
      x = minPos;
      w = maxPos + inner - minPos;
    }

    if (iRow == 0)
      y = minPos;
    else if (iRow == 2)
      y = maxPos;
    else if (iRow == 3)
    {
      y = minPos;
      h = maxPos + inner - minPos;
    }

    painter.drawRect(x, y, w, h);

    return pixmap;
  }
} // namespace

plQtUiRulerWidget::plQtUiRulerWidget(Qt::Orientation orientation, QWidget* pParent)
  : QWidget(pParent)
  , m_Orientation(orientation)
{
  if (orientation == Qt::Horizontal)
  {
    setFixedHeight(20);
  }
  else
  {
    setFixedWidth(20);
  }
}

void plQtUiRulerWidget::paintEvent(QPaintEvent* pEvent)
{
  QPainter painter(this);
  painter.fillRect(rect(), QColor(25, 25, 25));

  if (!m_GetTransform)
    return;

  float fScale = 1.0f;
  float fOrigin = 0.0f;
  m_GetTransform(fScale, fOrigin);

  if (fScale <= 0.0f)
    return;

  // pick a canvas-space step that yields at least ~60 widget pixels between labels
  const float steps[] = {10, 25, 50, 100, 200, 500, 1000, 2000};
  float fStep = steps[PL_ARRAY_SIZE(steps) - 1];
  for (float fCandidate : steps)
  {
    if (fCandidate * fScale >= 60.0f)
    {
      fStep = fCandidate;
      break;
    }
  }

  painter.setPen(QColor(90, 90, 90));
  QFont font = painter.font();
  font.setPointSizeF(6.5);
  painter.setFont(font);

  const int iLength = m_Orientation == Qt::Horizontal ? width() : height();

  const float fFirstCanvas = plMath::Floor((0.0f - fOrigin) / fScale / fStep) * fStep;

  for (float fCanvas = fFirstCanvas;; fCanvas += fStep)
  {
    const float fWidgetPos = fCanvas * fScale + fOrigin;
    if (fWidgetPos > iLength)
      break;
    if (fWidgetPos < 0)
      continue;

    const QString label = QString::number(static_cast<int>(fCanvas));

    if (m_Orientation == Qt::Horizontal)
    {
      painter.drawLine(QPointF(fWidgetPos, 12), QPointF(fWidgetPos, 20));
      painter.drawText(QPointF(fWidgetPos + 2, 9), label);

      // minor ticks
      for (int i = 1; i < 4; ++i)
      {
        const float fMinor = fWidgetPos + (fStep * fScale * i) / 4.0f;
        painter.drawLine(QPointF(fMinor, 16), QPointF(fMinor, 20));
      }
    }
    else
    {
      painter.drawLine(QPointF(12, fWidgetPos), QPointF(20, fWidgetPos));

      painter.save();
      painter.translate(9, fWidgetPos + 2);
      painter.rotate(90);
      painter.drawText(QPointF(0, 0), label);
      painter.restore();

      for (int i = 1; i < 4; ++i)
      {
        const float fMinor = fWidgetPos + (fStep * fScale * i) / 4.0f;
        painter.drawLine(QPointF(16, fMinor), QPointF(20, fMinor));
      }
    }
  }
}

plQtUiHierarchyTreeWidget::plQtUiHierarchyTreeWidget(QWidget* pParent)
  : QTreeWidget(pParent)
{
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setDragDropMode(QAbstractItemView::InternalMove);
}

void plQtUiHierarchyTreeWidget::keyPressEvent(QKeyEvent* pEvent)
{
  if (pEvent->key() == Qt::Key_Delete && m_OnDeleteKey)
  {
    m_OnDeleteKey();
    pEvent->accept();
    return;
  }

  QTreeWidget::keyPressEvent(pEvent);
}

void plQtUiHierarchyTreeWidget::dropEvent(QDropEvent* pEvent)
{
  pEvent->accept();

  if (!m_OnMoveItems)
    return;

  QTreeWidgetItem* pTargetItem = itemAt(pEvent->position().toPoint());

  QTreeWidgetItem* pNewParent = nullptr;
  int iIndex = -1;

  switch (dropIndicatorPosition())
  {
    case QAbstractItemView::OnItem:
      pNewParent = pTargetItem;
      iIndex = -1; // append as child
      break;

    case QAbstractItemView::AboveItem:
      pNewParent = pTargetItem != nullptr ? pTargetItem->parent() : nullptr;
      iIndex = pTargetItem != nullptr ? (pNewParent != nullptr ? pNewParent->indexOfChild(pTargetItem) : indexOfTopLevelItem(pTargetItem)) : -1;
      break;

    case QAbstractItemView::BelowItem:
      pNewParent = pTargetItem != nullptr ? pTargetItem->parent() : nullptr;
      iIndex = pTargetItem != nullptr ? (pNewParent != nullptr ? pNewParent->indexOfChild(pTargetItem) : indexOfTopLevelItem(pTargetItem)) + 1 : -1;
      break;

    case QAbstractItemView::OnViewport:
    default:
      pNewParent = nullptr;
      iIndex = -1; // append at root
      break;
  }

  m_OnMoveItems(selectedItems(), pNewParent, iIndex);
}

plQtUiDocumentAssetDocumentWindow::plQtUiDocumentAssetDocumentWindow(plAssetDocument* pDocument)
  : plQtEngineDocumentWindow(pDocument)
{
  // Menu Bar
  {
    plQtMenuBarActionMapView* pMenuBar = static_cast<plQtMenuBarActionMapView*>(menuBar());
    plActionContext context;
    context.m_sMapping = "UiDocumentAssetMenuBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pMenuBar->SetActionContext(context);
  }

  // Tool Bar
  {
    plQtToolBarActionMapView* pToolBar = new plQtToolBarActionMapView("Toolbar", this);
    plActionContext context;
    context.m_sMapping = "UiDocumentAssetToolBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pToolBar->SetActionContext(context);
    pToolBar->setObjectName("UiDocumentAssetWindowToolBar");
    addToolBar(pToolBar);
  }

  // Canvas View (with rulers)
  plQtViewWidgetContainer* pContainer = nullptr;
  {
    SetTargetFramerate(25);

    m_ViewConfig.m_Camera.LookAt(plVec3(-1.6f, 0, 0), plVec3(0, 0, 0), plVec3(0, 0, 1));
    m_ViewConfig.ApplyPerspectiveSetting(90);

    m_pViewWidget = new plQtEngineViewWidget(nullptr, this, &m_ViewConfig);
    AddViewWidget(m_pViewWidget);
    pContainer = new plQtViewWidgetContainer(this, m_pViewWidget, nullptr);

    QWidget* pCanvasArea = new QWidget(this);
    QGridLayout* pGrid = new QGridLayout(pCanvasArea);
    pGrid->setContentsMargins(0, 0, 0, 0);
    pGrid->setSpacing(0);

    m_pHRuler = new plQtUiRulerWidget(Qt::Horizontal, pCanvasArea);
    m_pHRuler->m_GetTransform = [this](float& fScale, float& fOrigin)
    {
      float fS = 1.0f;
      plVec2 vOffset, vCanvasSize;
      GetCanvasTransform(fS, vOffset, vCanvasSize);
      const float fDpr = static_cast<float>(m_pViewWidget->devicePixelRatioF());
      fScale = fS / fDpr;
      fOrigin = vOffset.x / fDpr;
    };

    m_pVRuler = new plQtUiRulerWidget(Qt::Vertical, pCanvasArea);
    m_pVRuler->m_GetTransform = [this](float& fScale, float& fOrigin)
    {
      float fS = 1.0f;
      plVec2 vOffset, vCanvasSize;
      GetCanvasTransform(fS, vOffset, vCanvasSize);
      const float fDpr = static_cast<float>(m_pViewWidget->devicePixelRatioF());
      fScale = fS / fDpr;
      fOrigin = vOffset.y / fDpr;
    };

    QWidget* pCorner = new QWidget(pCanvasArea);
    pCorner->setFixedSize(20, 20);
    pCorner->setStyleSheet("background-color: #191919;");

    pGrid->addWidget(pCorner, 0, 0);
    pGrid->addWidget(m_pHRuler, 0, 1);
    pGrid->addWidget(m_pVRuler, 1, 0);
    pGrid->addWidget(pContainer, 1, 1);

    plQtDocumentPanel* pCentralPanel = new plQtDocumentPanel(this, pDocument);
    pCentralPanel->setObjectName("UiDocumentAssetCanvasCentral");
    pCentralPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.Canvas")));
    pCentralPanel->setWidget(pCanvasArea, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->setCentralWidget(pCentralPanel);

    m_pViewWidget->installEventFilter(this);
    m_pViewWidget->setMouseTracking(true); // hover updates for interact mode
    m_pViewWidget->setAcceptDrops(true);   // palette widgets drag onto the canvas
  }

  CreateCanvasToolBar();

  // Palette + Hierarchy: two independent docks, stacked on the left
  {
    plQtDocumentPanel* pPalettePanel = new plQtDocumentPanel(this, pDocument);
    pPalettePanel->setObjectName("UiDocumentAssetPaletteDockWidget");
    pPalettePanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.Palette")));
    pPalettePanel->show();
    pPalettePanel->setWidget(CreatePaletteWidget(pPalettePanel), ads::CDockWidget::ForceNoScrollArea);

    ads::CDockAreaWidget* pPaletteArea = m_pDockManager->addDockWidgetTab(ads::LeftDockWidgetArea, pPalettePanel);

    plQtDocumentPanel* pHierarchyPanel = new plQtDocumentPanel(this, pDocument);
    pHierarchyPanel->setObjectName("UiDocumentAssetHierarchyDockWidget");
    pHierarchyPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.Hierarchy")));
    pHierarchyPanel->show();
    pHierarchyPanel->setWidget(CreateHierarchyWidget(pHierarchyPanel), ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidget(ads::BottomDockWidgetArea, pHierarchyPanel, pPaletteArea);
  }

  // Property Grid (with the rect transform editor at the top)
  {
    plQtDocumentPanel* pPropertyPanel = new plQtDocumentPanel(this, pDocument);
    pPropertyPanel->setObjectName("UiDocumentAssetDockWidget");
    pPropertyPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.Properties")));
    pPropertyPanel->show();

    plQtPropertyGridWidget* pPropertyGrid = new plQtPropertyGridWidget(pPropertyPanel, pDocument);

    QWidget* pWidget = new QWidget();
    pWidget->setObjectName("Group");
    pWidget->setLayout(new QVBoxLayout());
    pWidget->setContentsMargins(0, 0, 0, 0);

    pWidget->layout()->setContentsMargins(0, 0, 0, 0);
    pWidget->layout()->addWidget(new plQtAssetStatusIndicator(GetDocument()));
    pWidget->layout()->addWidget(CreateRectTransformWidget(pWidget));
    pWidget->layout()->addWidget(pPropertyGrid);

    pPropertyPanel->setWidget(pWidget, ads::CDockWidget::ForceNoScrollArea);

    ads::CDockAreaWidget* pPropertyArea = m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pPropertyPanel);

    // effective-style inspector: its own tab next to Properties (mockup's Properties | Style)
    plQtDocumentPanel* pStylePanel = new plQtDocumentPanel(this, pDocument);
    pStylePanel->setObjectName("UiDocumentAssetStyleDockWidget");
    pStylePanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.Style")));
    pStylePanel->show();
    pStylePanel->setWidget(CreateEffectiveStyleWidget(pStylePanel), ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidget(ads::CenterDockWidgetArea, pStylePanel, pPropertyArea);
    pPropertyPanel->raise(); // Properties stays the front tab

    pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);
  }

  // Bindings & Mock Data
  {
    plQtDocumentPanel* pBindingsPanel = new plQtDocumentPanel(this, pDocument);
    pBindingsPanel->setObjectName("UiDocumentAssetBindingsDockWidget");
    pBindingsPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.Bindings")));
    pBindingsPanel->show();

    m_pBindingsPanel = new plQtUiBindingsPanel(pBindingsPanel);
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::MockDataChanged, this, [this]() { SendMockBlackboardToEngine(); });
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::AddBindingRequested, this, [this]() { OnAddBindingRequested(); });
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::RemoveBindingRequested, this, [this](plUuid guid, QString sProp) { OnRemoveBindingRequested(guid, sProp); });
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::BindingEntryEdited, this, [this](plUuid guid, QString sProp, QString sEntry) { OnBindingEntryEdited(guid, sProp, sEntry); });
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::BindingRowActivated, this, [this](plUuid guid) { OnBindingRowActivated(guid); });
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::ScenarioSaveRequested, this, [this](QString sName) { OnScenarioSaveRequested(sName); });
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::ScenarioDeleteRequested, this, [this](QString sName) { OnScenarioDeleteRequested(sName); });
    connect(m_pBindingsPanel, &plQtUiBindingsPanel::ScenarioSelected, this, [this](QString sName) { OnScenarioSelected(sName); });

    pBindingsPanel->setWidget(m_pBindingsPanel, ads::CDockWidget::ForceNoScrollArea);

    ads::CDockAreaWidget* pBindingsArea = m_pDockManager->addDockWidgetTab(ads::BottomDockWidgetArea, pBindingsPanel);

    // Animation timeline (tabbed with the bindings dock)
    plQtDocumentPanel* pAnimationsPanel = new plQtDocumentPanel(this, pDocument);
    pAnimationsPanel->setObjectName("UiDocumentAssetAnimationsDockWidget");
    pAnimationsPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.Animations")));
    pAnimationsPanel->show();

    m_pAnimationsPanel = new plQtUiAnimationsPanel(pAnimationsPanel);
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::AddClipRequested, this, [this]() { OnAddClipRequested(); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::RemoveClipRequested, this, [this](plUuid guid) { OnRemoveClipRequested(guid); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::ClipActivated, this, [this](plUuid guid) { OnClipActivated(guid); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::ClipRenamed, this, [this](plUuid guid, QString sName)
      {
        if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid))
        {
          auto pAccessor = GetDocument()->GetObjectAccessor();
          pAccessor->StartTransaction("Rename UI Animation Clip");
          pAccessor->SetValueByName(pObj, "Name", plString(sName.toUtf8().data())).IgnoreResult();
          pAccessor->FinishTransaction();
        }
      });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::ClipDurationChanged, this, [this](plUuid guid, float fDuration)
      {
        if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid))
        {
          auto pAccessor = GetDocument()->GetObjectAccessor();
          pAccessor->StartTransaction("Change UI Animation Clip Duration");
          pAccessor->SetValueByName(pObj, "Duration", fDuration).IgnoreResult();
          pAccessor->FinishTransaction();
        }
      });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::AddTrackRequested, this, [this](plUuid guid) { OnAddTrackRequested(guid); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::RemoveTrackRequested, this, [this](plUuid guid) { OnRemoveTrackRequested(guid); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::AddKeyRequested, this, [this](plUuid guid, float fTime) { OnAddKeyRequested(guid, fTime); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::RemoveKeyRequested, this, [this](plUuid guid) { OnRemoveKeyRequested(guid); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::KeyMoved, this, [this](plUuid guid, float fTime) { OnKeyMoved(guid, fTime); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::KeyEdited, this, [this](plUuid guid, float fTime, float fValue, plColor color, int iEasing) { OnKeyEdited(guid, fTime, fValue, color, iEasing); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::Scrubbed, this, [this](QString sName, float fTime) { OnClipScrubbed(sName, fTime); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::PlayRequested, this, [this](QString sName) { OnClipPlay(sName); });
    connect(m_pAnimationsPanel, &plQtUiAnimationsPanel::StopRequested, this, [this]() { OnClipStop(); });

    pAnimationsPanel->setWidget(m_pAnimationsPanel, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidget(ads::CenterDockWidgetArea, pAnimationsPanel, pBindingsArea);

    // States & transitions (third tab)
    plQtDocumentPanel* pStatesPanel = new plQtDocumentPanel(this, pDocument);
    pStatesPanel->setObjectName("UiDocumentAssetStatesDockWidget");
    pStatesPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.States")));
    pStatesPanel->show();

    m_pStatesPanel = new plQtUiStatesPanel(pStatesPanel);
    connect(m_pStatesPanel, &plQtUiStatesPanel::DisabledBindingChanged, this, [this](plUuid guid, QString sValue) { OnElementStringPropertyChanged(guid, "DisabledBinding", sValue); });
    connect(m_pStatesPanel, &plQtUiStatesPanel::AddStateRequested, this, [this](plUuid guid) { OnAddStateRequested(guid); });
    connect(m_pStatesPanel, &plQtUiStatesPanel::RemoveStateRequested, this, [this](plUuid guid) { OnRemoveStateRequested(guid); });
    connect(m_pStatesPanel, &plQtUiStatesPanel::StateEdited, this, [this](plUuid guid, QString sName, QString sEntry, int iCondition, double fValue, QString sValueText, int iVisibility, QString sPlayClip) { OnStateEdited(guid, sName, sEntry, iCondition, fValue, sValueText, iVisibility, sPlayClip); });
    connect(m_pStatesPanel, &plQtUiStatesPanel::StateStyleChanged, this, [this](plUuid guid, QString sStyleRef) { OnElementStringPropertyChanged(guid, "Style", sStyleRef); });
    connect(m_pStatesPanel, &plQtUiStatesPanel::StateActivated, this, [this](plUuid guid) { OnClipActivated(guid); }); // same "select object in the grid" behavior
    connect(m_pStatesPanel, &plQtUiStatesPanel::TestValueChanged, this, [this](QString sEntry, QString sValue) { OnMockTestValue(sEntry, sValue); });

    pStatesPanel->setWidget(m_pStatesPanel, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidget(ads::CenterDockWidgetArea, pStatesPanel, pBindingsArea);

    // keep the bindings tab in front by default
    pBindingsArea->setCurrentDockWidget(pBindingsPanel);
  }

  // Status Bar: engine indicator | selection | draw stats (mockup's three segments)
  {
    m_pEngineStatusLabel = new QLabel(this);
    statusBar()->addWidget(m_pEngineStatusLabel);
    SetEngineConnectedIndicator(true);

    m_pStatusLabel = new QLabel(this);
    statusBar()->addWidget(m_pStatusLabel, 1);

    m_pDrawStatsLabel = new QLabel(this);
    m_pDrawStatsLabel->setStyleSheet(QStringLiteral("color: #8e8e8e;"));
    statusBar()->addPermanentWidget(m_pDrawStatsLabel);
  }

  m_pAssetDoc = static_cast<plUiDocumentAssetDocument*>(pDocument);

  // live preview: push the widget tree to the engine resource on every edit
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::PropertyEventHandler, this));
  m_pAssetDoc->GetObjectManager()->m_StructureEvents.AddEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::StructureEventHandler, this));
  m_pAssetDoc->GetSelectionManager()->m_Events.AddEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::SelectionEventHandler, this));
  plEditorEngineProcessConnection::s_Events.AddEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::EngineProcessEventHandler, this));

  FinishWindowCreation();

  UpdatePreview();
  RebuildHierarchy();
  UpdateBindingsList();
  UpdateAnimationsList();
  UpdateStatesPanel();
}

plQtUiDocumentAssetDocumentWindow::~plQtUiDocumentAssetDocumentWindow()
{
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::PropertyEventHandler, this));
  m_pAssetDoc->GetObjectManager()->m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::StructureEventHandler, this));
  m_pAssetDoc->GetSelectionManager()->m_Events.RemoveEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::SelectionEventHandler, this));
  plEditorEngineProcessConnection::s_Events.RemoveEventHandler(plMakeDelegate(&plQtUiDocumentAssetDocumentWindow::EngineProcessEventHandler, this));

  RestoreResource();
}

void plQtUiDocumentAssetDocumentWindow::InternalRedraw()
{
  plEditorInputContext::UpdateActiveInputContext();
  SendRedrawMsg();
  plQtEngineDocumentWindow::InternalRedraw();
}

void plQtUiDocumentAssetDocumentWindow::SendRedrawMsg()
{
  // do not try to redraw while the process is crashed, it is obviously futile
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  for (auto pView : m_ViewWidgets)
  {
    pView->SetEnablePicking(false);
    pView->UpdateCameraInterpolation();
    pView->SyncToEngine();
  }
}

void plQtUiDocumentAssetDocumentWindow::UpdatePreview()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plResourceUpdateMsgToEngine msg;
  msg.m_sResourceType = "UI Document";

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plContiguousMemoryStreamStorage streamStorage;
  plMemoryStreamWriter memoryWriter(&streamStorage);

  // Write Path
  plStringBuilder sAbsFilePath = GetDocument()->GetDocumentPath();
  sAbsFilePath.ChangeFileExtension("plBinUiDocument");
  memoryWriter << sAbsFilePath;

  // Write Header
  const plUInt64 uiHash = plAssetCurator::GetSingleton()->GetAssetTransformHash(GetDocument()->GetGuid());
  plAssetFileHeader AssetHeader;
  AssetHeader.SetFileHashAndVersion(uiHash, m_pAssetDoc->GetAssetTypeVersion());
  AssetHeader.Write(memoryWriter).IgnoreResult();

  // Write Asset Data
  if (m_pAssetDoc->WriteAsset(memoryWriter).Succeeded())
  {
    msg.m_Data = plArrayPtr<const plUInt8>(streamStorage.GetData(), streamStorage.GetStorageSize32());

    plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
  }
}

void plQtUiDocumentAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = "UI Document";

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}

void plQtUiDocumentAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  UpdatePreview();
  RebuildHierarchy(); // names / title-relevant properties may have changed
  UpdateSelectionUi();
  UpdateBindingsList();
  UpdateAnimationsList();
  UpdateStatesPanel();
}

void plQtUiDocumentAssetDocumentWindow::StructureEventHandler(const plDocumentObjectStructureEvent& e)
{
  switch (e.m_EventType)
  {
    case plDocumentObjectStructureEvent::Type::AfterObjectAdded:
    case plDocumentObjectStructureEvent::Type::AfterObjectRemoved:
    case plDocumentObjectStructureEvent::Type::AfterObjectMoved2:
      UpdatePreview();
      RebuildHierarchy();
      SendSelectionToEngine(); // indices may have shifted
      UpdateBindingsList();
      UpdateAnimationsList();
      UpdateStatesPanel();
      break;

    default:
      break;
  }
}

void plQtUiDocumentAssetDocumentWindow::SelectionEventHandler(const plSelectionManagerEvent& e)
{
  SendSelectionToEngine();
  SyncHierarchySelection();
  UpdateSelectionUi();
  UpdateStatesPanel();
}

void plQtUiDocumentAssetDocumentWindow::SyncHierarchySelection()
{
  if (m_pHierarchy == nullptr || m_bIgnoreHierarchySelection)
    return;

  m_bIgnoreHierarchySelection = true;

  m_pHierarchy->clearSelection();

  const auto& selection = GetDocument()->GetSelectionManager()->GetSelection();

  std::function<void(QTreeWidgetItem*)> syncItem = [&](QTreeWidgetItem* pItem)
  {
    const plUuid guid = plConversionUtils::ConvertStringToUuid(pItem->data(0, Qt::UserRole).toString().toUtf8().data());

    for (const plDocumentObject* pObj : selection)
    {
      if (pObj->GetGuid() == guid)
      {
        pItem->setSelected(true);
        break;
      }
    }

    for (int i = 0; i < pItem->childCount(); ++i)
    {
      syncItem(pItem->child(i));
    }
  };

  for (int i = 0; i < m_pHierarchy->topLevelItemCount(); ++i)
  {
    syncItem(m_pHierarchy->topLevelItem(i));
  }

  m_bIgnoreHierarchySelection = false;
}

//////////////////////////////////////////////////////////////////////////
// selection → engine gizmos

const plDocumentObject* plQtUiDocumentAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

bool plQtUiDocumentAssetDocumentWindow::ComputeElementPath(const plDocumentObject* pObject, plHybridArray<plUInt32, 8>& out_path) const
{
  out_path.Clear();

  const plDocumentObject* pPropsObj = GetPropertiesObject();
  const plDocumentObject* pCur = pObject;

  while (pCur != nullptr && pCur != pPropsObj)
  {
    if (pCur->GetParentProperty() != "Children")
      return false;

    plInt32 iIndex = pCur->GetPropertyIndex().ConvertTo<plInt32>();
    if (iIndex < 0)
      return false;

    out_path.PushBack(static_cast<plUInt32>(iIndex));
    pCur = pCur->GetParent();
  }

  if (pCur != pPropsObj)
    return false;

  // was built bottom-up
  for (plUInt32 i = 0; i < out_path.GetCount() / 2; ++i)
  {
    plMath::Swap(out_path[i], out_path[out_path.GetCount() - 1 - i]);
  }

  return true;
}

const plDocumentObject* plQtUiDocumentAssetDocumentWindow::GetElementDocObject(plArrayPtr<const plUInt32> path) const
{
  const plDocumentObject* pCur = GetPropertiesObject();

  for (plUInt32 uiIndex : path)
  {
    const plDocumentObject* pNext = nullptr;

    for (const plDocumentObject* pChild : pCur->GetChildren())
    {
      if (pChild->GetParentProperty() == "Children" && pChild->GetPropertyIndex().ConvertTo<plInt32>() == static_cast<plInt32>(uiIndex))
      {
        pNext = pChild;
        break;
      }
    }

    if (pNext == nullptr)
      return nullptr;

    pCur = pNext;
  }

  return pCur;
}

void plQtUiDocumentAssetDocumentWindow::SendSelectionToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plStringBuilder sPaths;
  plStringBuilder sPath;

  for (const plDocumentObject* pObj : GetDocument()->GetSelectionManager()->GetSelection())
  {
    plHybridArray<plUInt32, 8> path;
    if (!ComputeElementPath(pObj, path) || path.IsEmpty())
      continue;

    sPath.Clear();
    for (plUInt32 uiIndex : path)
    {
      if (!sPath.IsEmpty())
        sPath.Append("/");

      sPath.AppendFormat("{}", uiIndex);
    }

    if (!sPaths.IsEmpty())
      sPaths.Append(";");

    sPaths.Append(sPath);
  }

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiSelection";
  msg.m_sValue = sPaths;

  GetEditorEngineConnection()->SendMessage(&msg);
}

//////////////////////////////////////////////////////////////////////////
// canvas mouse editing

void plQtUiDocumentAssetDocumentWindow::GetCanvasTransform(float& out_fScale, plVec2& out_vOffset, plVec2& out_vCanvasSize) const
{
  const float fDpr = static_cast<float>(m_pViewWidget->devicePixelRatioF());
  const plVec2 vViewportSize(m_pViewWidget->width() * fDpr, m_pViewWidget->height() * fDpr);

  // mirrors plUiCanvasComponent::ComputeCanvasTransform for the preview component's settings
  if (m_vDeviceResolution.x > 0 && m_vDeviceResolution.y > 0)
  {
    out_vCanvasSize.Set(static_cast<float>(m_vDeviceResolution.x), static_cast<float>(m_vDeviceResolution.y));

    const float fFitScale = plMath::Min(vViewportSize.x / out_vCanvasSize.x, vViewportSize.y / out_vCanvasSize.y);
    out_fScale = m_fZoom > 0.0f ? m_fZoom : fFitScale;

    out_vOffset = (vViewportSize - out_vCanvasSize * out_fScale) * 0.5f + m_vPan;
    return;
  }

  out_fScale = plMath::Max(vViewportSize.y / s_fReferenceHeight, 0.0001f);
  out_vOffset = plVec2::MakeZero();
  out_vCanvasSize = vViewportSize / out_fScale;
}

float plQtUiDocumentAssetDocumentWindow::GetCanvasScale() const
{
  float fScale = 1.0f;
  plVec2 vOffset, vCanvasSize;
  GetCanvasTransform(fScale, vOffset, vCanvasSize);
  return fScale;
}

plRectFloat plQtUiDocumentAssetDocumentWindow::GetCanvasRect() const
{
  float fScale = 1.0f;
  plVec2 vOffset, vCanvasSize;
  GetCanvasTransform(fScale, vOffset, vCanvasSize);
  return plRectFloat(0, 0, vCanvasSize.x, vCanvasSize.y);
}

plVec2 plQtUiDocumentAssetDocumentWindow::WidgetToCanvasPos(const QPoint& viewPos) const
{
  const float fDpr = static_cast<float>(m_pViewWidget->devicePixelRatioF());

  float fScale = 1.0f;
  plVec2 vOffset, vCanvasSize;
  GetCanvasTransform(fScale, vOffset, vCanvasSize);

  return plVec2((viewPos.x() * fDpr - vOffset.x) / fScale, (viewPos.y() * fDpr - vOffset.y) / fScale);
}

void plQtUiDocumentAssetDocumentWindow::HitTestRecursive(const plUiElement* pElement, const plRectFloat& rect, const plRectFloat& parentRect, const plVec2& vPos, plHybridArray<plUInt32, 8>& ref_path, HitResult& ref_result) const
{
  if (!pElement->m_bVisible)
    return;

  // locked elements (hierarchy lock column) are not clickable, including their subtree
  if (pElement->m_bEditorLocked)
    return;

  if (rect.Contains(vPos))
  {
    // later (topmost drawn) elements win
    ref_result.m_pElement = pElement;
    ref_result.m_Path = ref_path;
    ref_result.m_Rect = rect;
    ref_result.m_ParentRect = parentRect;
  }

  plHybridArray<plRectFloat, 16> childRects;
  plUiLayout::ComputeChildRects(pElement, rect, childRects);

  for (plUInt32 i = 0; i < pElement->m_Children.GetCount(); ++i)
  {
    if (pElement->m_Children[i] == nullptr)
      continue;

    ref_path.PushBack(i);
    HitTestRecursive(pElement->m_Children[i], childRects[i], rect, vPos, ref_path, ref_result);
    ref_path.PopBack();
  }
}

bool plQtUiDocumentAssetDocumentWindow::HitTest(const plVec2& vRefPos, HitResult& out_result) const
{
  const plUiDocumentAssetProperties* pProps = m_pAssetDoc->GetProperties();
  const plRectFloat canvasRect = GetCanvasRect();

  plHybridArray<plUInt32, 8> path;

  for (plUInt32 i = 0; i < pProps->m_Children.GetCount(); ++i)
  {
    if (pProps->m_Children[i] == nullptr)
      continue;

    path.PushBack(i);
    // top level: pseudo-root, children place themselves against the canvas rect
    HitTestRecursive(pProps->m_Children[i], pProps->m_Children[i]->ComputeRect(canvasRect), canvasRect, vRefPos, path, out_result);
    path.PopBack();
  }

  return out_result.m_pElement != nullptr;
}

bool plQtUiDocumentAssetDocumentWindow::GetSelectedElementRect(HitResult& out_result) const
{
  const plDocumentObject* pSelected = GetDocument()->GetSelectionManager()->GetCurrentObject();
  if (pSelected == nullptr)
    return false;

  plHybridArray<plUInt32, 8> path;
  if (!ComputeElementPath(pSelected, path) || path.IsEmpty())
    return false;

  // resolve the native element + rect along the path
  const plUiDocumentAssetProperties* pProps = m_pAssetDoc->GetProperties();
  plRectFloat rect = GetCanvasRect();
  plRectFloat parentRect = rect;
  const plUiElement* pElement = nullptr;
  const plUiElement* pParentElement = nullptr; // nullptr = pseudo-root

  const auto* pChildren = &pProps->m_Children;
  for (plUInt32 uiIndex : path)
  {
    if (uiIndex >= pChildren->GetCount() || (*pChildren)[uiIndex] == nullptr)
      return false;

    parentRect = rect;
    const plUiElement* pChild = (*pChildren)[uiIndex];
    rect = plUiLayout::ComputeChildRect(pParentElement, pChild, parentRect, uiIndex);
    pParentElement = pChild;
    pElement = pChild;
    pChildren = &pElement->m_Children;
  }

  out_result.m_pElement = pElement;
  out_result.m_Path = path;
  out_result.m_Rect = rect;
  out_result.m_ParentRect = parentRect;
  return true;
}

bool plQtUiDocumentAssetDocumentWindow::eventFilter(QObject* pObject, QEvent* pEvent)
{
  if (pObject == m_pViewWidget)
  {
    // rulers track the viewport size
    if (pEvent->type() == QEvent::Resize)
    {
      if (m_pHRuler != nullptr)
        m_pHRuler->update();
      if (m_pVRuler != nullptr)
        m_pVRuler->update();
    }

    // middle-mouse panning (device preset only)
    if (m_vDeviceResolution.x > 0)
    {
      if (pEvent->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(pEvent)->button() == Qt::MiddleButton)
      {
        m_bPanning = true;
        m_PanStartPos = static_cast<QMouseEvent*>(pEvent)->position().toPoint();
        m_vPanStartValue = m_vPan;
        return true;
      }

      if (pEvent->type() == QEvent::MouseMove && m_bPanning)
      {
        const QPoint pos = static_cast<QMouseEvent*>(pEvent)->position().toPoint();
        const float fDpr = static_cast<float>(m_pViewWidget->devicePixelRatioF());
        m_vPan = m_vPanStartValue + plVec2((pos.x() - m_PanStartPos.x()) * fDpr, (pos.y() - m_PanStartPos.y()) * fDpr);
        RefreshCanvasView();
        return true;
      }

      if (pEvent->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(pEvent)->button() == Qt::MiddleButton && m_bPanning)
      {
        m_bPanning = false;
        return true;
      }
    }

    // interact mode: forward mouse state to the preview instead of editing
    // Delete removes the selected elements (works in both edit and interact mode)
    if (pEvent->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(pEvent)->key() == Qt::Key_Delete)
    {
      RemoveSelectedElements();
      return true;
    }

    // mouse-wheel zoom (device-preset canvas), anchored on the cursor
    if (pEvent->type() == QEvent::Wheel && !m_bInteractMode && m_vDeviceResolution.x > 0)
    {
      QWheelEvent* pWheel = static_cast<QWheelEvent*>(pEvent);
      const float fTicks = static_cast<float>(pWheel->angleDelta().y()) / 120.0f;

      float fScale = 1.0f;
      plVec2 vOffset = plVec2::MakeZero();
      plVec2 vCanvasSize = plVec2::MakeZero();
      GetCanvasTransform(fScale, vOffset, vCanvasSize);

      const float fNewZoom = plMath::Clamp(fScale * plMath::Pow(1.15f, fTicks), 0.1f, 8.0f);

      // keep the canvas point under the cursor fixed
      const plVec2 vCursor(static_cast<float>(pWheel->position().x()), static_cast<float>(pWheel->position().y()));
      const plVec2 vCanvasPoint((vCursor.x - vOffset.x) / fScale, (vCursor.y - vOffset.y) / fScale);
      const plVec2 vViewport(static_cast<float>(m_pViewWidget->width()), static_cast<float>(m_pViewWidget->height()));

      const plVec2 vNewOffset = vCursor - vCanvasPoint * fNewZoom;

      m_fZoom = fNewZoom;
      m_vPan = vNewOffset - (vViewport - vCanvasSize * fNewZoom) * 0.5f;

      // reflect the custom zoom in the combo (appended dynamic entry)
      if (m_pZoomCombo != nullptr)
      {
        plQtScopedBlockSignals blockSignals(m_pZoomCombo);

        const QString sText = QString("%1 %").arg(static_cast<int>(plMath::Round(fNewZoom * 100.0f)));

        if (m_pZoomCombo->count() < 5)
        {
          m_pZoomCombo->addItem(sText, fNewZoom);
        }
        else
        {
          m_pZoomCombo->setItemText(4, sText);
          m_pZoomCombo->setItemData(4, fNewZoom);
        }

        m_pZoomCombo->setCurrentIndex(4);
      }

      RefreshCanvasView();
      return true;
    }

    // palette drag & drop: create the widget where it is dropped
    if (pEvent->type() == QEvent::DragEnter || pEvent->type() == QEvent::DragMove)
    {
      QDropEvent* pDrag = static_cast<QDropEvent*>(pEvent);
      if (pDrag->mimeData()->hasFormat(szUiWidgetMimeType))
      {
        pDrag->acceptProposedAction();
        return true;
      }
      return false;
    }

    if (pEvent->type() == QEvent::Drop)
    {
      QDropEvent* pDrop = static_cast<QDropEvent*>(pEvent);
      if (pDrop->mimeData()->hasFormat(szUiWidgetMimeType))
      {
        const QByteArray typeName = pDrop->mimeData()->data(szUiWidgetMimeType);

        if (const plRTTI* pType = plRTTI::FindTypeByName(typeName.constData()))
        {
          AddElementAt(pType, pDrop->position().toPoint());
        }

        pDrop->acceptProposedAction();
        return true;
      }
      return false;
    }

    if (m_bInteractMode)
    {
      // keyboard stands in for the gamepad: arrows/Enter/Escape drive the focus navigation,
      // editing keys and typed characters feed a focused Text Input
      if (pEvent->type() == QEvent::KeyPress && !plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
      {
        QKeyEvent* pKey = static_cast<QKeyEvent*>(pEvent);

        plInt32 iNavAction = -1;
        plInt32 iEditKey = -1;

        switch (pKey->key())
        {
          case Qt::Key_Up:
            iNavAction = plUiNavAction::Up;
            break;
          case Qt::Key_Down:
            iNavAction = plUiNavAction::Down;
            break;
          case Qt::Key_Left:
            iNavAction = plUiNavAction::Left;
            break;
          case Qt::Key_Right:
            iNavAction = plUiNavAction::Right;
            break;
          case Qt::Key_Return:
          case Qt::Key_Enter:
            iNavAction = plUiNavAction::Accept;
            break;
          case Qt::Key_Escape:
            iNavAction = plUiNavAction::Cancel;
            break;
          case Qt::Key_Backspace:
            iEditKey = plUiEditKey::Backspace;
            break;
          case Qt::Key_Home:
            iEditKey = plUiEditKey::Home;
            break;
          case Qt::Key_End:
            iEditKey = plUiEditKey::End;
            break;
          default:
            break;
        }

        if (iNavAction >= 0)
        {
          plDocumentConfigMsgToEngine msg;
          msg.m_sWhatToDo = "UiNavKey";
          msg.m_iValue = iNavAction;
          GetEditorEngineConnection()->SendMessage(&msg);
          return true;
        }

        if (iEditKey >= 0)
        {
          plDocumentConfigMsgToEngine msg;
          msg.m_sWhatToDo = "UiEditKey";
          msg.m_iValue = iEditKey;
          GetEditorEngineConnection()->SendMessage(&msg);
          return true;
        }

        // printable characters (Delete stays the element-delete shortcut, handled above)
        const QString sText = pKey->text();

        if (!sText.isEmpty())
        {
          bool bSent = false;

          for (const uint uiCodepoint : sText.toUcs4())
          {
            if (uiCodepoint >= 32)
            {
              plDocumentConfigMsgToEngine msg;
              msg.m_sWhatToDo = "UiChar";
              msg.m_iValue = static_cast<plInt32>(uiCodepoint);
              GetEditorEngineConnection()->SendMessage(&msg);
              bSent = true;
            }
          }

          if (bSent)
          {
            return true;
          }
        }
      }

      if (pEvent->type() == QEvent::MouseButtonPress || pEvent->type() == QEvent::MouseButtonRelease || pEvent->type() == QEvent::MouseMove)
      {
        QMouseEvent* pMouse = static_cast<QMouseEvent*>(pEvent);
        const bool bDown = pMouse->buttons().testFlag(Qt::LeftButton);
        SendInteractMouseState(pMouse->position().toPoint(), bDown);
        return true;
      }

      if (pEvent->type() == QEvent::Wheel)
      {
        // forward wheel ticks so scroll views scroll in the preview
        QWheelEvent* pWheel = static_cast<QWheelEvent*>(pEvent);
        const float fTicks = static_cast<float>(pWheel->angleDelta().y()) / 120.0f;
        const bool bDown = pWheel->buttons().testFlag(Qt::LeftButton);
        SendInteractMouseState(pWheel->position().toPoint(), bDown, fTicks);
        return true;
      }

      return plQtEngineDocumentWindow::eventFilter(pObject, pEvent);
    }

    if (pEvent->type() == QEvent::MouseButtonPress)
    {
      QMouseEvent* pMouse = static_cast<QMouseEvent*>(pEvent);
      if (pMouse->button() == Qt::LeftButton)
      {
        BeginDrag(pMouse->pos());
        return true;
      }
    }
    else if (pEvent->type() == QEvent::MouseMove)
    {
      if (m_DragMode != DragMode::None)
      {
        UpdateDrag(static_cast<QMouseEvent*>(pEvent)->pos());
        return true;
      }
    }
    else if (pEvent->type() == QEvent::MouseButtonRelease)
    {
      QMouseEvent* pMouse = static_cast<QMouseEvent*>(pEvent);
      if (pMouse->button() == Qt::LeftButton && m_DragMode != DragMode::None)
      {
        EndDrag(true);
        return true;
      }
    }
    else if (pEvent->type() == QEvent::KeyPress)
    {
      QKeyEvent* pKey = static_cast<QKeyEvent*>(pEvent);
      if (pKey->key() == Qt::Key_Escape && m_DragMode != DragMode::None)
      {
        EndDrag(false);
        return true;
      }
    }
  }

  return plQtEngineDocumentWindow::eventFilter(pObject, pEvent);
}

namespace
{
  bool IsContainerParent(const plUiDocumentAssetProperties* pProps, plArrayPtr<const plUInt32> path)
  {
    // walk to the element's parent (path minus last index); the pseudo-root is not a container
    if (path.GetCount() < 2)
      return false;

    const plUiElement* pElement = nullptr;
    const auto* pChildren = &pProps->m_Children;

    for (plUInt32 i = 0; i < path.GetCount() - 1; ++i)
    {
      if (path[i] >= pChildren->GetCount() || (*pChildren)[path[i]] == nullptr)
        return false;

      pElement = (*pChildren)[path[i]];
      pChildren = &pElement->m_Children;
    }

    return plDynamicCast<const plUiStackPanel*>(pElement) != nullptr || plDynamicCast<const plUiGridPanel*>(pElement) != nullptr;
  }
} // namespace

bool plQtUiDocumentAssetDocumentWindow::IsAnchorsMode() const
{
  return m_pAnchorsModeAction != nullptr && m_pAnchorsModeAction->isChecked();
}

void plQtUiDocumentAssetDocumentWindow::BeginDrag(const QPoint& viewPos)
{
  if (IsAnchorsMode())
  {
    BeginAnchorDrag(viewPos);
    return;
  }

  const float fScale = GetCanvasScale();
  const plVec2 vRefPos = WidgetToCanvasPos(viewPos);

  // resize handles of the current selection have priority (unless the selection is locked)
  HitResult selected;
  if (GetSelectedElementRect(selected) && !selected.m_pElement->m_bEditorLocked)
  {
    const float fZone = s_fResizeHandleZonePx / fScale;
    const plRectFloat& r = selected.m_Rect;

    // inside an expanded rect and near an edge?
    if (vRefPos.x >= r.x - fZone && vRefPos.x <= r.x + r.width + fZone && vRefPos.y >= r.y - fZone && vRefPos.y <= r.y + r.height + fZone)
    {
      m_iResizeX = 0;
      m_iResizeY = 0;

      if (plMath::Abs(vRefPos.x - r.x) <= fZone)
        m_iResizeX = -1;
      else if (plMath::Abs(vRefPos.x - (r.x + r.width)) <= fZone)
        m_iResizeX = 1;

      if (plMath::Abs(vRefPos.y - r.y) <= fZone)
        m_iResizeY = -1;
      else if (plMath::Abs(vRefPos.y - (r.y + r.height)) <= fZone)
        m_iResizeY = 1;

      const bool bContainerChild = IsContainerParent(m_pAssetDoc->GetProperties(), selected.m_Path);

      if (bContainerChild)
      {
        // inside a container only the preferred size (Offsets.z/w) is meaningful:
        // keep only right/bottom edge handles
        if (m_iResizeX == -1)
          m_iResizeX = 0;
        if (m_iResizeY == -1)
          m_iResizeY = 0;
      }

      if (m_iResizeX != 0 || m_iResizeY != 0)
      {
        const plDocumentObject* pObj = GetElementDocObject(selected.m_Path);
        if (pObj != nullptr)
        {
          m_DragMode = DragMode::Resize;
          m_DragObjectGuid = pObj->GetGuid();
          m_DragStartPos = viewPos;
          m_bDragContainerChild = bContainerChild;
          m_vDragStartOffsets = selected.m_pElement->m_vOffsets;
          m_vDragAnchorMin = selected.m_pElement->m_vAnchorMin;
          m_vDragAnchorMax = selected.m_pElement->m_vAnchorMax;
          m_DragStartRect = selected.m_Rect;
          CollectSnapRects(selected.m_Path);

          GetDocument()->GetObjectAccessor()->StartTransaction("Resize UI Element");
          m_bDragTransactionOpen = true;
          return;
        }
      }
    }
  }

  // otherwise: select what is under the mouse (and start moving it)
  HitResult hit;
  if (HitTest(vRefPos, hit))
  {
    const plDocumentObject* pObj = GetElementDocObject(hit.m_Path);
    if (pObj != nullptr)
    {
      GetDocument()->GetSelectionManager()->SetSelection(const_cast<plDocumentObject*>(pObj));

      // children of stack/grid containers are placed by their parent: select only, no move
      if (IsContainerParent(m_pAssetDoc->GetProperties(), hit.m_Path))
        return;

      m_DragMode = DragMode::Move;
      m_DragObjectGuid = pObj->GetGuid();
      m_DragStartPos = viewPos;
      m_bDragContainerChild = false;
      m_vDragStartOffsets = hit.m_pElement->m_vOffsets;
      m_vDragAnchorMin = hit.m_pElement->m_vAnchorMin;
      m_vDragAnchorMax = hit.m_pElement->m_vAnchorMax;
      m_DragStartRect = hit.m_Rect;
      CollectSnapRects(hit.m_Path);

      GetDocument()->GetObjectAccessor()->StartTransaction("Move UI Element");
      m_bDragTransactionOpen = true;
    }
  }
  else
  {
    GetDocument()->GetSelectionManager()->Clear();
  }
}

void plQtUiDocumentAssetDocumentWindow::BeginAnchorDrag(const QPoint& viewPos)
{
  const float fScale = GetCanvasScale();
  const plVec2 vRefPos = WidgetToCanvasPos(viewPos);

  HitResult selected;

  if (GetSelectedElementRect(selected) && !selected.m_pElement->m_bEditorLocked)
  {
    if (IsContainerParent(m_pAssetDoc->GetProperties(), selected.m_Path))
    {
      if (m_pStatusLabel != nullptr)
      {
        m_pStatusLabel->setText(plMakeQString(plTranslate("GameUi.Status.AnchorsInContainer")));
      }
    }
    else
    {
      const plRectFloat& parent = selected.m_ParentRect;
      const plUiElement* pElement = selected.m_pElement;

      const float ax[2] = {parent.x + pElement->m_vAnchorMin.x * parent.width, parent.x + pElement->m_vAnchorMax.x * parent.width};
      const float ay[2] = {parent.y + pElement->m_vAnchorMin.y * parent.height, parent.y + pElement->m_vAnchorMax.y * parent.height};

      // nearest of the four diamonds within the grab zone
      const float fZone = 8.0f / plMath::Max(fScale, 0.001f);
      float fBestDistSqr = fZone * fZone;
      plInt32 iBestX = -1, iBestY = -1;

      for (plInt32 ix = 0; ix < 2; ++ix)
      {
        for (plInt32 iy = 0; iy < 2; ++iy)
        {
          const float fDistSqr = plVec2(vRefPos.x - ax[ix], vRefPos.y - ay[iy]).GetLengthSquared();

          if (fDistSqr <= fBestDistSqr)
          {
            fBestDistSqr = fDistSqr;
            iBestX = ix;
            iBestY = iy;
          }
        }
      }

      if (iBestX >= 0)
      {
        const plDocumentObject* pObj = GetElementDocObject(selected.m_Path);

        if (pObj != nullptr)
        {
          // coincident diamonds (point anchor): the first clear drag direction decides
          // whether min or max moves (2 = undecided)
          m_iAnchorDragX = (pElement->m_vAnchorMin.x == pElement->m_vAnchorMax.x) ? 2 : (iBestX == 0 ? -1 : 1);
          m_iAnchorDragY = (pElement->m_vAnchorMin.y == pElement->m_vAnchorMax.y) ? 2 : (iBestY == 0 ? -1 : 1);

          m_DragMode = DragMode::Anchor;
          m_DragObjectGuid = pObj->GetGuid();
          m_DragStartPos = viewPos;
          m_DragStartRect = selected.m_Rect;
          m_DragStartParentRect = parent;
          m_vDragAnchorMin = pElement->m_vAnchorMin;
          m_vDragAnchorMax = pElement->m_vAnchorMax;

          GetDocument()->GetObjectAccessor()->StartTransaction("Edit UI Anchors");
          m_bDragTransactionOpen = true;
          return;
        }
      }
    }
  }

  // no diamond grabbed: plain click-select (never moves in Anchors mode)
  HitResult hit;

  if (HitTest(vRefPos, hit))
  {
    if (const plDocumentObject* pObj = GetElementDocObject(hit.m_Path))
    {
      GetDocument()->GetSelectionManager()->SetSelection(const_cast<plDocumentObject*>(pObj));
    }
  }
  else
  {
    GetDocument()->GetSelectionManager()->Clear();
  }
}

void plQtUiDocumentAssetDocumentWindow::UpdateDrag(const QPoint& viewPos)
{
  if (!m_bDragTransactionOpen)
    return;

  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(m_DragObjectGuid);
  if (pObj == nullptr)
    return;

  const float fDpr = static_cast<float>(m_pViewWidget->devicePixelRatioF());
  const float fScale = GetCanvasScale();

  float fDx = (viewPos.x() - m_DragStartPos.x()) * fDpr / fScale;
  float fDy = (viewPos.y() - m_DragStartPos.y()) * fDpr / fScale;

  const bool bSnap = m_pSnapAction == nullptr || m_pSnapAction->isChecked();

  // ————— Anchors mode: the drag edits anchor fractions, the rect stays put —————
  if (m_DragMode == DragMode::Anchor)
  {
    const plRectFloat& parent = m_DragStartParentRect;
    const float fFracDx = (parent.width > 1e-3f) ? fDx / parent.width : 0.0f;
    const float fFracDy = (parent.height > 1e-3f) ? fDy / parent.height : 0.0f;

    // coincident diamonds: the first clear direction decides which anchor splits off
    const float fDecideZone = 2.0f / plMath::Max(fScale, 0.001f);

    if (m_iAnchorDragX == 2 && plMath::Abs(fDx) > fDecideZone)
      m_iAnchorDragX = (fDx > 0.0f) ? 1 : -1;
    if (m_iAnchorDragY == 2 && plMath::Abs(fDy) > fDecideZone)
      m_iAnchorDragY = (fDy > 0.0f) ? 1 : -1;

    auto snap01 = [bSnap](float f)
    {
      if (bSnap)
      {
        for (float fTarget : {0.0f, 0.5f, 1.0f})
        {
          if (plMath::Abs(f - fTarget) < 0.03f)
            return fTarget;
        }
      }

      return plMath::Clamp(f, 0.0f, 1.0f);
    };

    plVec2 vAnchorMin = m_vDragAnchorMin;
    plVec2 vAnchorMax = m_vDragAnchorMax;

    if (m_iAnchorDragX == -1)
      vAnchorMin.x = plMath::Min(snap01(m_vDragAnchorMin.x + fFracDx), vAnchorMax.x);
    else if (m_iAnchorDragX == 1)
      vAnchorMax.x = plMath::Max(snap01(m_vDragAnchorMax.x + fFracDx), vAnchorMin.x);

    if (m_iAnchorDragY == -1)
      vAnchorMin.y = plMath::Min(snap01(m_vDragAnchorMin.y + fFracDy), vAnchorMax.y);
    else if (m_iAnchorDragY == 1)
      vAnchorMax.y = plMath::Max(snap01(m_vDragAnchorMax.y + fFracDy), vAnchorMin.y);

    // recompute the offsets so the element's rect is unchanged under the new anchors
    // (the generalized ApplyAnchorPreset math)
    const plRectFloat& rect = m_DragStartRect;
    plVec4 vOffsets;

    if (vAnchorMin.x == vAnchorMax.x)
    {
      vOffsets.x = rect.x - (parent.x + vAnchorMin.x * parent.width);
      vOffsets.z = rect.width;
    }
    else
    {
      vOffsets.x = rect.x - (parent.x + vAnchorMin.x * parent.width);
      vOffsets.z = (rect.x + rect.width) - (parent.x + vAnchorMax.x * parent.width);
    }

    if (vAnchorMin.y == vAnchorMax.y)
    {
      vOffsets.y = rect.y - (parent.y + vAnchorMin.y * parent.height);
      vOffsets.w = rect.height;
    }
    else
    {
      vOffsets.y = rect.y - (parent.y + vAnchorMin.y * parent.height);
      vOffsets.w = (rect.y + rect.height) - (parent.y + vAnchorMax.y * parent.height);
    }

    auto pAccessor = GetDocument()->GetObjectAccessor();
    pAccessor->SetValueByName(pObj, "AnchorMin", vAnchorMin).IgnoreResult();
    pAccessor->SetValueByName(pObj, "AnchorMax", vAnchorMax).IgnoreResult();
    pAccessor->SetValueByName(pObj, "Offsets", vOffsets).IgnoreResult();
    return;
  }

  // snap to sibling/canvas edges and centers, showing guide lines in the preview
  plHybridArray<SnapGuide, 4> guides;

  if (bSnap && !m_SnapRects.IsEmpty())
  {
    SnapDragDeltas(fDx, fDy, guides);
  }

  SendGuidesToEngine(guides);

  const bool bPointAnchorX = m_vDragAnchorMin.x == m_vDragAnchorMax.x;
  const bool bPointAnchorY = m_vDragAnchorMin.y == m_vDragAnchorMax.y;

  plVec4 vOffsets = m_vDragStartOffsets;

  if (m_DragMode == DragMode::Move)
  {
    // point anchor: x/y is the position, z/w is the size (keep size)
    // stretch anchor: x/z are left/right insets, both move together
    vOffsets.x += fDx;
    vOffsets.y += fDy;

    if (!bPointAnchorX)
      vOffsets.z += fDx;
    if (!bPointAnchorY)
      vOffsets.w += fDy;
  }
  else if (m_DragMode == DragMode::Resize)
  {
    if (m_bDragContainerChild)
    {
      // inside a container z/w are always the preferred size, regardless of anchors
      if (m_iResizeX == 1)
        vOffsets.z += fDx;
      if (m_iResizeY == 1)
        vOffsets.w += fDy;
    }
    else
    {
      if (m_iResizeX == -1)
      {
        vOffsets.x += fDx;
        if (bPointAnchorX)
          vOffsets.z -= fDx; // keep the right edge in place: shrink the width
      }
      else if (m_iResizeX == 1)
      {
        vOffsets.z += fDx; // point anchor: grow width; stretch: move the right inset
      }

      if (m_iResizeY == -1)
      {
        vOffsets.y += fDy;
        if (bPointAnchorY)
          vOffsets.w -= fDy;
      }
      else if (m_iResizeY == 1)
      {
        vOffsets.w += fDy;
      }
    }
  }

  // snap to whole pixels (toolbar toggle)
  if (bSnap)
  {
    vOffsets.x = plMath::Round(vOffsets.x);
    vOffsets.y = plMath::Round(vOffsets.y);
    vOffsets.z = plMath::Round(vOffsets.z);
    vOffsets.w = plMath::Round(vOffsets.w);
  }

  GetDocument()->GetObjectAccessor()->SetValueByName(pObj, "Offsets", vOffsets).IgnoreResult();

  // spacers size via explicit Width/Height (layout ignores their Offsets.z/w)
  if (pObj->GetType()->IsDerivedFrom<plUiSpacer>() && m_DragMode == DragMode::Resize)
  {
    GetDocument()->GetObjectAccessor()->SetValueByName(pObj, "Width", plMath::Max(vOffsets.z, 0.0f)).IgnoreResult();
    GetDocument()->GetObjectAccessor()->SetValueByName(pObj, "Height", plMath::Max(vOffsets.w, 0.0f)).IgnoreResult();
  }
}

void plQtUiDocumentAssetDocumentWindow::EndDrag(bool bApply)
{
  if (m_bDragTransactionOpen)
  {
    if (bApply)
    {
      GetDocument()->GetObjectAccessor()->FinishTransaction();
    }
    else
    {
      GetDocument()->GetObjectAccessor()->CancelTransaction();
    }

    m_bDragTransactionOpen = false;
  }

  m_DragMode = DragMode::None;
  m_iResizeX = 0;
  m_iResizeY = 0;
  m_iAnchorDragX = 0;
  m_iAnchorDragY = 0;
  m_SnapRects.Clear();
  SendGuidesToEngine({}); // clear the preview guide lines
}

void plQtUiDocumentAssetDocumentWindow::CollectSnapRects(const plHybridArray<plUInt32, 8>& excludePath)
{
  m_SnapRects.Clear();

  // the canvas itself is a snap target (edges + center)
  const plRectFloat canvasRect = GetCanvasRect();
  m_SnapRects.PushBack(canvasRect);

  const plUiDocumentAssetProperties* pProps = m_pAssetDoc->GetProperties();

  plHybridArray<plUInt32, 8> path;

  std::function<void(const plUiElement*, const plRectFloat&)> collect = [&](const plUiElement* pElement, const plRectFloat& rect)
  {
    if (!pElement->m_bVisible)
      return;

    // the dragged subtree moves with the cursor: its rects are not valid targets
    if (path == excludePath)
      return;

    m_SnapRects.PushBack(rect);

    plHybridArray<plRectFloat, 16> childRects;
    plUiLayout::ComputeChildRects(pElement, rect, childRects);

    for (plUInt32 i = 0; i < pElement->m_Children.GetCount(); ++i)
    {
      if (pElement->m_Children[i] == nullptr)
        continue;

      path.PushBack(i);
      collect(pElement->m_Children[i], childRects[i]);
      path.PopBack();
    }
  };

  for (plUInt32 i = 0; i < pProps->m_Children.GetCount(); ++i)
  {
    if (pProps->m_Children[i] == nullptr)
      continue;

    path.PushBack(i);
    collect(pProps->m_Children[i], pProps->m_Children[i]->ComputeRect(canvasRect));
    path.PopBack();
  }
}

void plQtUiDocumentAssetDocumentWindow::SnapDragDeltas(float& ref_fDx, float& ref_fDy, plHybridArray<SnapGuide, 4>& out_guides) const
{
  const float fScale = GetCanvasScale();
  const float fThreshold = 6.0f / plMath::Max(fScale, 0.001f);

  // the coordinates of the dragged rect that want to align, per axis
  plHybridArray<float, 3> myXs, myYs;

  if (m_DragMode == DragMode::Move)
  {
    const plRectFloat rect(m_DragStartRect.x + ref_fDx, m_DragStartRect.y + ref_fDy, m_DragStartRect.width, m_DragStartRect.height);
    myXs.PushBack(rect.x);
    myXs.PushBack(rect.x + rect.width * 0.5f);
    myXs.PushBack(rect.x + rect.width);
    myYs.PushBack(rect.y);
    myYs.PushBack(rect.y + rect.height * 0.5f);
    myYs.PushBack(rect.y + rect.height);
  }
  else if (m_DragMode == DragMode::Resize)
  {
    // only the dragged edge snaps
    if (m_iResizeX == -1)
      myXs.PushBack(m_DragStartRect.x + ref_fDx);
    else if (m_iResizeX == 1)
      myXs.PushBack(m_DragStartRect.x + m_DragStartRect.width + ref_fDx);

    if (m_iResizeY == -1)
      myYs.PushBack(m_DragStartRect.y + ref_fDy);
    else if (m_iResizeY == 1)
      myYs.PushBack(m_DragStartRect.y + m_DragStartRect.height + ref_fDy);
  }

  float fBestDx = 0.0f, fBestDy = 0.0f;
  const plRectFloat* pBestXRect = nullptr;
  const plRectFloat* pBestYRect = nullptr;
  float fBestXPos = 0.0f, fBestYPos = 0.0f;
  float fBestDxAbs = fThreshold, fBestDyAbs = fThreshold;

  for (const plRectFloat& candidate : m_SnapRects)
  {
    const float candXs[3] = {candidate.x, candidate.x + candidate.width * 0.5f, candidate.x + candidate.width};
    const float candYs[3] = {candidate.y, candidate.y + candidate.height * 0.5f, candidate.y + candidate.height};

    for (float fMine : myXs)
    {
      for (float fCand : candXs)
      {
        const float fDiff = fCand - fMine;
        if (plMath::Abs(fDiff) < fBestDxAbs)
        {
          fBestDxAbs = plMath::Abs(fDiff);
          fBestDx = fDiff;
          fBestXPos = fCand;
          pBestXRect = &candidate;
        }
      }
    }

    for (float fMine : myYs)
    {
      for (float fCand : candYs)
      {
        const float fDiff = fCand - fMine;
        if (plMath::Abs(fDiff) < fBestDyAbs)
        {
          fBestDyAbs = plMath::Abs(fDiff);
          fBestDy = fDiff;
          fBestYPos = fCand;
          pBestYRect = &candidate;
        }
      }
    }
  }

  const plRectFloat draggedRect(m_DragStartRect.x + ref_fDx, m_DragStartRect.y + ref_fDy, m_DragStartRect.width, m_DragStartRect.height);

  if (pBestXRect != nullptr)
  {
    ref_fDx += fBestDx;

    SnapGuide& guide = out_guides.ExpandAndGetRef();
    guide.m_bVertical = true;
    guide.m_fPos = fBestXPos;
    guide.m_fMin = plMath::Min(draggedRect.y, pBestXRect->y) - 8.0f;
    guide.m_fMax = plMath::Max(draggedRect.y + draggedRect.height, pBestXRect->y + pBestXRect->height) + 8.0f;
  }

  if (pBestYRect != nullptr)
  {
    ref_fDy += fBestDy;

    SnapGuide& guide = out_guides.ExpandAndGetRef();
    guide.m_bVertical = false;
    guide.m_fPos = fBestYPos;
    guide.m_fMin = plMath::Min(draggedRect.x, pBestYRect->x) - 8.0f;
    guide.m_fMax = plMath::Max(draggedRect.x + draggedRect.width, pBestYRect->x + pBestYRect->width) + 8.0f;
  }
}

void plQtUiDocumentAssetDocumentWindow::SendGuidesToEngine(plArrayPtr<const SnapGuide> guides)
{
  plStringBuilder sValue;

  for (const SnapGuide& guide : guides)
  {
    if (!sValue.IsEmpty())
    {
      sValue.Append(";");
    }

    sValue.AppendFormat("{} {} {} {}", guide.m_bVertical ? "V" : "H", guide.m_fPos, guide.m_fMin, guide.m_fMax);
  }

  if (m_sLastSentGuides == sValue)
    return;

  m_sLastSentGuides = sValue;

  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiGuides";
  msg.m_sValue = sValue.GetData();
  GetEditorEngineConnection()->SendMessage(&msg);
}

//////////////////////////////////////////////////////////////////////////
// palette + hierarchy panel

namespace
{
  /// palette button that starts a drag carrying the element type name
  class plQtUiPaletteButton : public QToolButton
  {
  public:
    plQtUiPaletteButton(QWidget* pParent, const plRTTI* pType)
      : QToolButton(pParent)
      , m_pType(pType)
    {
    }

  protected:
    virtual void mousePressEvent(QMouseEvent* pEvent) override
    {
      m_PressPos = pEvent->pos();
      QToolButton::mousePressEvent(pEvent);
    }

    virtual void mouseMoveEvent(QMouseEvent* pEvent) override
    {
      if (pEvent->buttons().testFlag(Qt::LeftButton) && (pEvent->pos() - m_PressPos).manhattanLength() >= QApplication::startDragDistance())
      {
        plStringBuilder sTypeName(m_pType->GetTypeName());

        QMimeData* pMime = new QMimeData();
        pMime->setData(szUiWidgetMimeType, QByteArray(sTypeName.GetData()));

        QDrag* pDrag = new QDrag(this);
        pDrag->setMimeData(pMime);
        pDrag->setPixmap(icon().pixmap(28, 28));

        setDown(false);
        pDrag->exec(Qt::CopyAction);
        return;
      }

      QToolButton::mouseMoveEvent(pEvent);
    }

  private:
    const plRTTI* m_pType;
    QPoint m_PressPos;
  };
} // namespace

QWidget* plQtUiDocumentAssetDocumentWindow::CreatePaletteWidget(QWidget* pParent)
{
  QWidget* pPanel = new QWidget(pParent);
  QVBoxLayout* pLayout = new QVBoxLayout(pPanel);
  pLayout->setContentsMargins(4, 4, 4, 4);
  pLayout->setSpacing(4);

  // search box filtering the widget buttons (mockup: "Search widgets...")
  QLineEdit* pSearch = new QLineEdit(pPanel);
  pSearch->setPlaceholderText(plMakeQString(plTranslate("GameUi.Palette.Search")));
  pSearch->setClearButtonEnabled(true);
  pLayout->addWidget(pSearch);

  struct PaletteGroup
  {
    const char* m_szTitleKey;
    QList<const plRTTI*> m_Types;
  };

  PaletteGroup groups[] = {
    {"GameUi.Palette.GroupContainers", {plGetStaticRTTI<plUiPanel>(), plGetStaticRTTI<plUiStackPanel>(), plGetStaticRTTI<plUiGridPanel>(), plGetStaticRTTI<plUiScrollView>()}},
    {"GameUi.Palette.GroupVisual", {plGetStaticRTTI<plUiImage>(), plGetStaticRTTI<plUiText>(), plGetStaticRTTI<plUiPath>()}},
    {"GameUi.Palette.GroupControls", {plGetStaticRTTI<plUiButton>(), plGetStaticRTTI<plUiToggle>(), plGetStaticRTTI<plUiSlider>(), plGetStaticRTTI<plUiTextInput>(), plGetStaticRTTI<plUiProgressBar>(), plGetStaticRTTI<plUiItemList>()}},
    {"GameUi.Palette.GroupAdvanced", {plGetStaticRTTI<plUiSubDocument>(), plGetStaticRTTI<plUiSpacer>()}},
  };

  // per-group data kept alive in the filter lambda: header, grid, and (button, label) pairs
  struct GroupWidgets
  {
    QLabel* m_pHeader = nullptr;
    QGridLayout* m_pGrid = nullptr;
    QList<QPair<QToolButton*, QString>> m_Buttons;
  };

  auto pGroupWidgets = std::make_shared<QList<GroupWidgets>>();

  for (const PaletteGroup& group : groups)
  {
    GroupWidgets widgets;

    widgets.m_pHeader = new QLabel(plMakeQString(plTranslate(group.m_szTitleKey)), pPanel);
    widgets.m_pHeader->setStyleSheet(plUiEditorTheme::szSectionHeader);
    pLayout->addWidget(widgets.m_pHeader);

    widgets.m_pGrid = new QGridLayout();
    widgets.m_pGrid->setSpacing(3);

    for (const plRTTI* pEntryType : group.m_Types)
    {
      const QString sLabel = plMakeQString(plTranslate(pEntryType->GetTypeName()));

      QToolButton* pButton = new plQtUiPaletteButton(pPanel, pEntryType);
      pButton->setText(sLabel);
      pButton->setIcon(QIcon(MakePaletteWidgetPixmap(pEntryType)));
      pButton->setIconSize(QSize(28, 28));
      pButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      pButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      pButton->setToolTip(QString("%1: %2").arg(sLabel, plMakeQString(plTranslate("GameUi.Palette.AddTooltip"))));
      pButton->setAcceptDrops(false);

      connect(pButton, &QToolButton::clicked, this, [this, pEntryType]() { AddElement(pEntryType); });

      widgets.m_Buttons.append({pButton, sLabel});
    }

    pGroupWidgets->append(widgets);
    pLayout->addLayout(widgets.m_pGrid);
  }

  // (re)flows every group's grid to the buttons matching the filter; hides empty groups
  auto applyFilter = [pGroupWidgets](const QString& sFilter)
  {
    for (GroupWidgets& widgets : *pGroupWidgets)
    {
      int iVisible = 0;

      for (auto& entry : widgets.m_Buttons)
      {
        widgets.m_pGrid->removeWidget(entry.first);

        const bool bMatch = sFilter.isEmpty() || entry.second.contains(sFilter, Qt::CaseInsensitive);
        entry.first->setVisible(bMatch);

        if (bMatch)
        {
          widgets.m_pGrid->addWidget(entry.first, iVisible / 2, iVisible % 2);
          ++iVisible;
        }
      }

      widgets.m_pHeader->setVisible(iVisible > 0);
    }
  };

  applyFilter(QString());

  connect(pSearch, &QLineEdit::textChanged, this, [applyFilter](const QString& sText) { applyFilter(sText.trimmed()); });

  pLayout->addStretch();

  // overflow SCROLLS instead of squeezing the buttons
  QScrollArea* pScroll = new QScrollArea(pParent);
  pScroll->setWidget(pPanel);
  pScroll->setWidgetResizable(true);
  pScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  pScroll->setFrameShape(QFrame::NoFrame);

  return pScroll;
}

QWidget* plQtUiDocumentAssetDocumentWindow::CreateHierarchyWidget(QWidget* pParent)
{
  m_pHierarchy = new plQtUiHierarchyTreeWidget(pParent);
  m_pHierarchy->m_OnMoveItems = [this](const QList<QTreeWidgetItem*>& items, QTreeWidgetItem* pNewParentItem, int iIndex)
  { OnHierarchyItemsMoved(items, pNewParentItem, iIndex); };
  m_pHierarchy->m_OnDeleteKey = [this]() { RemoveSelectedElements(); };
  m_pHierarchy->setColumnCount(3);
  m_pHierarchy->setHeaderHidden(true);
  m_pHierarchy->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_pHierarchy->setContextMenuPolicy(Qt::CustomContextMenu);
  m_pHierarchy->setColumnWidth(1, 24);
  m_pHierarchy->setColumnWidth(2, 24);
  m_pHierarchy->header()->setStretchLastSection(false);
  m_pHierarchy->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_pHierarchy->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_pHierarchy->header()->setSectionResizeMode(2, QHeaderView::Fixed);

  connect(m_pHierarchy, &QTreeWidget::itemSelectionChanged, this, [this]() { OnHierarchySelectionChanged(); });
  connect(m_pHierarchy, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* pItem, int iColumn) { OnHierarchyItemChanged(pItem, iColumn); });
  connect(m_pHierarchy, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) { OnHierarchyContextMenu(pos); });

  return m_pHierarchy;
}

void plQtUiDocumentAssetDocumentWindow::RebuildHierarchy()
{
  if (m_pHierarchy == nullptr)
    return;

  m_bIgnoreHierarchySelection = true;

  m_pHierarchy->clear();

  plStringBuilder tmp;

  std::function<void(const plDocumentObject*, QTreeWidgetItem*)> addChildren = [&](const plDocumentObject* pParentObj, QTreeWidgetItem* pParentItem)
  {
    // gather in property-index order
    plHybridArray<const plDocumentObject*, 16> ordered;

    for (const plDocumentObject* pChild : pParentObj->GetChildren())
    {
      if (pChild->GetParentProperty() == "Children")
      {
        ordered.PushBack(pChild);
      }
    }

    ordered.Sort([](const plDocumentObject* a, const plDocumentObject* b)
      { return a->GetPropertyIndex().ConvertTo<plInt32>() < b->GetPropertyIndex().ConvertTo<plInt32>(); });

    for (const plDocumentObject* pChild : ordered)
    {
      plStringBuilder sLabel = plTranslate(pChild->GetType()->GetTypeName());

      const plVariant name = pChild->GetTypeAccessor().GetValue("Name");
      if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
      {
        sLabel.AppendFormat(": {}", name.Get<plString>());
      }

      // binding / localization badges (GetValue returns an invalid variant for absent properties)
      bool bBound = false;
      for (const char* szBindingProp : {"VisibleBinding", "FillAmountBinding", "TextBinding", "ClickEntryBinding"})
      {
        const plVariant binding = pChild->GetTypeAccessor().GetValue(szBindingProp);
        bBound |= binding.IsA<plString>() && !binding.Get<plString>().IsEmpty();
      }

      const plVariant localized = pChild->GetTypeAccessor().GetValue("Localized");

      const plVariant style = pChild->GetTypeAccessor().GetValue("Style");
      const bool bStyled = style.IsA<plString>() && !style.Get<plString>().IsEmpty();

      if (bBound)
        sLabel.Append(" ⚡"); // lightning bolt
      if (localized.IsA<bool>() && localized.Get<bool>())
        sLabel.Append(" \U0001F310"); // globe
      if (bStyled)
        sLabel.Append(" \xF0\x9F\x8E\xA8"); // artist palette: has a UI Style

      QTreeWidgetItem* pItem = new QTreeWidgetItem();
      pItem->setText(0, sLabel.GetData());

      if (bBound)
      {
        pItem->setToolTip(0, plMakeQString(plTranslate("GameUi.Hierarchy.BoundTooltip")));
      }
      pItem->setIcon(0, MakeElementTypeIcon(GetElementTypeColor(pChild->GetType())));
      pItem->setData(0, Qt::UserRole, QString(plConversionUtils::ToString(pChild->GetGuid(), tmp).GetData()));

      // eye/lock badge columns (scenegraph iconography); toggled via itemClicked
      const plVariant visible = pChild->GetTypeAccessor().GetValue("Visible");
      const bool bVisible = !visible.IsA<bool>() || visible.Get<bool>();
      pItem->setIcon(1, MakeEyeIcon(bVisible));
      pItem->setToolTip(1, plMakeQString(plTranslate("GameUi.Hierarchy.VisibleTooltip")));

      // lock column: a locked element (and subtree) is not clickable in the canvas
      const plVariant locked = pChild->GetTypeAccessor().GetValue("EditorLocked");
      const bool bLocked = locked.IsA<bool>() && locked.Get<bool>();
      pItem->setIcon(2, MakeHierarchyLockIcon(bLocked));
      pItem->setToolTip(2, plMakeQString(plTranslate("GameUi.Hierarchy.LockTooltip")));

      if (pParentItem != nullptr)
      {
        pParentItem->addChild(pItem);
      }
      else
      {
        m_pHierarchy->addTopLevelItem(pItem);
      }

      addChildren(pChild, pItem);
    }
  };

  addChildren(GetPropertiesObject(), nullptr);

  m_pHierarchy->expandAll();

  m_bIgnoreHierarchySelection = false;

  SyncHierarchySelection();
}

void plQtUiDocumentAssetDocumentWindow::OnHierarchySelectionChanged()
{
  if (m_bIgnoreHierarchySelection)
    return;

  m_bIgnoreHierarchySelection = true;

  plDeque<const plDocumentObject*> newSelection;

  for (QTreeWidgetItem* pItem : m_pHierarchy->selectedItems())
  {
    const plUuid guid = plConversionUtils::ConvertStringToUuid(pItem->data(0, Qt::UserRole).toString().toUtf8().data());

    if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid))
    {
      newSelection.PushBack(pObj);
    }
  }

  GetDocument()->GetSelectionManager()->SetSelection(newSelection);

  m_bIgnoreHierarchySelection = false;

  SendSelectionToEngine();
}

void plQtUiDocumentAssetDocumentWindow::OnHierarchyItemChanged(QTreeWidgetItem* pItem, int iColumn)
{
  // clicking an eye/lock badge toggles the corresponding property (icons refresh on rebuild)
  if (m_bIgnoreHierarchySelection || (iColumn != 1 && iColumn != 2))
    return;

  const plUuid guid = plConversionUtils::ConvertStringToUuid(pItem->data(0, Qt::UserRole).toString().toUtf8().data());
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  if (iColumn == 1)
  {
    const plVariant visible = pObj->GetTypeAccessor().GetValue("Visible");
    const bool bNewVisible = !(!visible.IsA<bool>() || visible.Get<bool>());

    pAccessor->StartTransaction(bNewVisible ? "Show UI Element" : "Hide UI Element");
    if (pAccessor->SetValueByName(pObj, "Visible", bNewVisible).Failed())
    {
      pAccessor->CancelTransaction();
      return;
    }
    pAccessor->FinishTransaction();
    return;
  }

  // lock column: gates canvas hit-testing (see HitTestRecursive), runtime ignores it
  const plVariant locked = pObj->GetTypeAccessor().GetValue("EditorLocked");
  const bool bNewLocked = !(locked.IsA<bool>() && locked.Get<bool>());

  pAccessor->StartTransaction(bNewLocked ? "Lock UI Element" : "Unlock UI Element");
  if (pAccessor->SetValueByName(pObj, "EditorLocked", bNewLocked).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnHierarchyContextMenu(const QPoint& pos)
{
  QMenu menu(m_pHierarchy);

  QAction* pAddPanel = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddPanel")));
  QAction* pAddStackPanel = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddStackPanel")));
  QAction* pAddGridPanel = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddGridPanel")));
  QAction* pAddImage = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddImage")));
  QAction* pAddText = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddText")));
  QAction* pAddButton = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddButton")));
  QAction* pAddProgressBar = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddProgressBar")));
  QAction* pAddScrollView = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddScrollView")));
  QAction* pAddToggle = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddToggle")));
  QAction* pAddSlider = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddSlider")));
  QAction* pAddTextInput = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddTextInput")));
  QAction* pAddItemList = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddItemList")));
  QAction* pAddSubDocument = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddSubDocument")));
  QAction* pAddSpacer = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.AddSpacer")));
  menu.addSeparator();
  QAction* pRemove = menu.addAction(plMakeQString(plTranslate("GameUi.Menu.Delete")));
  pRemove->setEnabled(!m_pHierarchy->selectedItems().isEmpty());

  QAction* pChosen = menu.exec(m_pHierarchy->mapToGlobal(pos));

  if (pChosen == pAddPanel)
  {
    AddElement(plGetStaticRTTI<plUiPanel>());
  }
  else if (pChosen == pAddStackPanel)
  {
    AddElement(plGetStaticRTTI<plUiStackPanel>());
  }
  else if (pChosen == pAddGridPanel)
  {
    AddElement(plGetStaticRTTI<plUiGridPanel>());
  }
  else if (pChosen == pAddImage)
  {
    AddElement(plGetStaticRTTI<plUiImage>());
  }
  else if (pChosen == pAddText)
  {
    AddElement(plGetStaticRTTI<plUiText>());
  }
  else if (pChosen == pAddButton)
  {
    AddElement(plGetStaticRTTI<plUiButton>());
  }
  else if (pChosen == pAddProgressBar)
  {
    AddElement(plGetStaticRTTI<plUiProgressBar>());
  }
  else if (pChosen == pAddScrollView)
  {
    AddElement(plGetStaticRTTI<plUiScrollView>());
  }
  else if (pChosen == pAddToggle)
  {
    AddElement(plGetStaticRTTI<plUiToggle>());
  }
  else if (pChosen == pAddSlider)
  {
    AddElement(plGetStaticRTTI<plUiSlider>());
  }
  else if (pChosen == pAddTextInput)
  {
    AddElement(plGetStaticRTTI<plUiTextInput>());
  }
  else if (pChosen == pAddItemList)
  {
    AddElement(plGetStaticRTTI<plUiItemList>());
  }
  else if (pChosen == pAddSubDocument)
  {
    AddElement(plGetStaticRTTI<plUiSubDocument>());
  }
  else if (pChosen == pAddSpacer)
  {
    AddElement(plGetStaticRTTI<plUiSpacer>());
  }
  else if (pChosen == pRemove)
  {
    RemoveSelectedElements();
  }
}

void plQtUiDocumentAssetDocumentWindow::AddElementAt(const plRTTI* pType, const QPoint& viewPos)
{
  const plVec2 vRefPos = WidgetToCanvasPos(viewPos);

  // drop parent: the topmost element under the cursor (any element can hold children)
  const plDocumentObject* pParent = GetPropertiesObject();
  plRectFloat parentRect = GetCanvasRect();
  bool bContainerParent = false;

  HitResult hit;
  if (HitTest(vRefPos, hit))
  {
    if (const plDocumentObject* pObj = GetElementDocObject(hit.m_Path))
    {
      pParent = pObj;
      parentRect = hit.m_Rect;
      bContainerParent = pObj->GetType()->IsDerivedFrom<plUiStackPanel>() || pObj->GetType()->IsDerivedFrom<plUiGridPanel>();
    }
  }

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI Element");

  plUuid newGuid = plUuid::MakeUuid();
  if (pAccessor->AddObjectByName(pParent, "Children", pAccessor->GetCountByName(pParent, "Children"), pType, newGuid).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }

  // free placement: center the default 200x50 box on the drop point (containers place their
  // children themselves, so their offsets stay untouched)
  if (!bContainerParent)
  {
    if (const plDocumentObject* pNewObj = GetDocument()->GetObjectManager()->GetObject(newGuid))
    {
      constexpr float fWidth = 200.0f;
      constexpr float fHeight = 50.0f;

      // default anchors are the parent center (0.5, 0.5)
      const float fAnchorX = parentRect.x + parentRect.width * 0.5f;
      const float fAnchorY = parentRect.y + parentRect.height * 0.5f;

      plVec4 vOffsets;
      vOffsets.x = plMath::Round(vRefPos.x - fWidth * 0.5f - fAnchorX);
      vOffsets.y = plMath::Round(vRefPos.y - fHeight * 0.5f - fAnchorY);
      vOffsets.z = fWidth;
      vOffsets.w = fHeight;

      pAccessor->SetValueByName(pNewObj, "Offsets", vOffsets).IgnoreResult();

      if (pType->IsDerivedFrom<plUiSpacer>())
      {
        pAccessor->SetValueByName(pNewObj, "Width", fWidth).IgnoreResult();
        pAccessor->SetValueByName(pNewObj, "Height", fHeight).IgnoreResult();
      }
    }
  }

  pAccessor->FinishTransaction();

  if (const plDocumentObject* pNewObj = GetDocument()->GetObjectManager()->GetObject(newGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pNewObj);
  }
}

void plQtUiDocumentAssetDocumentWindow::AddElement(const plRTTI* pType)
{
  // add as child of the single selected element, or at the document root
  const plDocumentObject* pParent = GetPropertiesObject();

  const auto& selection = GetDocument()->GetSelectionManager()->GetSelection();
  if (selection.GetCount() == 1 && selection[0] != GetPropertiesObject())
  {
    plHybridArray<plUInt32, 8> path;
    if (ComputeElementPath(selection[0], path))
    {
      pParent = selection[0];
    }
  }

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI Element");

  plUuid newGuid = plUuid::MakeUuid();
  if (pAccessor->AddObjectByName(pParent, "Children", pAccessor->GetCountByName(pParent, "Children"), pType, newGuid).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }

  pAccessor->FinishTransaction();

  if (const plDocumentObject* pNewObj = GetDocument()->GetObjectManager()->GetObject(newGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pNewObj);
  }
}

void plQtUiDocumentAssetDocumentWindow::OnHierarchyItemsMoved(const QList<QTreeWidgetItem*>& items, QTreeWidgetItem* pNewParentItem, int iIndex)
{
  const plDocumentObject* pNewParent = GetPropertiesObject();

  if (pNewParentItem != nullptr)
  {
    const plUuid parentGuid = plConversionUtils::ConvertStringToUuid(pNewParentItem->data(0, Qt::UserRole).toString().toUtf8().data());
    pNewParent = GetDocument()->GetObjectManager()->GetObject(parentGuid);

    if (pNewParent == nullptr)
      return;
  }

  // gather the dragged document objects
  plHybridArray<const plDocumentObject*, 8> objects;

  for (QTreeWidgetItem* pItem : items)
  {
    const plUuid guid = plConversionUtils::ConvertStringToUuid(pItem->data(0, Qt::UserRole).toString().toUtf8().data());

    if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid))
    {
      objects.PushBack(pObj);
    }
  }

  if (objects.IsEmpty())
    return;

  // an element must never become a child of itself or one of its own descendants
  for (const plDocumentObject* pObj : objects)
  {
    const plDocumentObject* pCheck = pNewParent;
    while (pCheck != nullptr)
    {
      if (pCheck == pObj)
        return;

      pCheck = pCheck->GetParent();
    }
  }

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Move UI Elements");

  bool bOk = true;
  plVariant index = iIndex < 0 ? plVariant(pAccessor->GetCountByName(pNewParent, "Children")) : plVariant(iIndex);

  for (const plDocumentObject* pObj : objects)
  {
    bOk &= pAccessor->MoveObjectByName(pObj, pNewParent, "Children", index).Succeeded();
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

//////////////////////////////////////////////////////////////////////////
// canvas toolbar / interact mode

void plQtUiDocumentAssetDocumentWindow::CreateCanvasToolBar()
{
  QToolBar* pToolBar = new QToolBar("Canvas", this);
  pToolBar->setObjectName("UiDocumentAssetCanvasToolBar");

  // device preset
  {
    m_pDevicePresetCombo = new QComboBox(pToolBar);
    m_pDevicePresetCombo->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.DevicePresetTooltip")));
    m_pDevicePresetCombo->addItem(plMakeQString(plTranslate("GameUi.Toolbar.FillViewport")), QSize(0, 0));
    m_pDevicePresetCombo->addItem("1920 x 1080", QSize(1920, 1080));
    m_pDevicePresetCombo->addItem("2560 x 1440", QSize(2560, 1440));
    m_pDevicePresetCombo->addItem("3840 x 2160", QSize(3840, 2160));
    m_pDevicePresetCombo->addItem("1280 x 800 (Deck)", QSize(1280, 800));
    m_pDevicePresetCombo->addItem("1080 x 2340 (Phone)", QSize(1080, 2340));

    connect(m_pDevicePresetCombo, &QComboBox::currentIndexChanged, this, [this](int)
      {
        const QSize size = m_pDevicePresetCombo->currentData().toSize();
        m_vDeviceResolution.Set(static_cast<plUInt32>(size.width()), static_cast<plUInt32>(size.height()));
        m_vPan.SetZero();
        m_pZoomCombo->setEnabled(m_vDeviceResolution.x > 0);
        RefreshCanvasView();
      });

    pToolBar->addWidget(m_pDevicePresetCombo);
  }

  // zoom
  {
    m_pZoomCombo = new QComboBox(pToolBar);
    m_pZoomCombo->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.ZoomTooltip")));
    m_pZoomCombo->addItem(plMakeQString(plTranslate("GameUi.Toolbar.ZoomFit")), 0.0f);
    m_pZoomCombo->addItem("50 %", 0.5f);
    m_pZoomCombo->addItem("100 %", 1.0f);
    m_pZoomCombo->addItem("200 %", 2.0f);
    m_pZoomCombo->setEnabled(false);

    connect(m_pZoomCombo, &QComboBox::currentIndexChanged, this, [this](int)
      {
        m_fZoom = m_pZoomCombo->currentData().toFloat();
        m_vPan.SetZero();
        RefreshCanvasView();
      });

    pToolBar->addWidget(m_pZoomCombo);
  }

  // localization preview: language combo + highlight-untranslated (mockup's globe combo)
  {
    m_pLanguageCombo = new QComboBox(pToolBar);
    m_pLanguageCombo->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.LanguageTooltip")));
    PopulateLanguageCombo();

    connect(m_pLanguageCombo, &QComboBox::currentIndexChanged, this, [this](int) { SendLanguageToEngine(); });

    pToolBar->addWidget(m_pLanguageCombo);

    m_pHighlightUntranslatedAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.HighlightUntranslated")));
    m_pHighlightUntranslatedAction->setCheckable(true);
    m_pHighlightUntranslatedAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.HighlightUntranslatedTooltip")));
    connect(m_pHighlightUntranslatedAction, &QAction::toggled, this, [this](bool) { SendLanguageToEngine(); });
  }

  pToolBar->addSeparator();

  // canvas tool mode: direct manipulation vs anchor editing (mockup's Select | Anchors)
  {
    QActionGroup* pModeGroup = new QActionGroup(this);

    m_pSelectModeAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.SelectMode")));
    m_pSelectModeAction->setCheckable(true);
    m_pSelectModeAction->setChecked(true);
    m_pSelectModeAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.SelectModeTooltip")));
    pModeGroup->addAction(m_pSelectModeAction);

    m_pAnchorsModeAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.AnchorsMode")));
    m_pAnchorsModeAction->setCheckable(true);
    m_pAnchorsModeAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.AnchorsModeTooltip")));
    pModeGroup->addAction(m_pAnchorsModeAction);
  }

  pToolBar->addSeparator();

  m_pInteractAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.Interact")));
  m_pInteractAction->setCheckable(true);
  m_pInteractAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.InteractTooltip")));
  connect(m_pInteractAction, &QAction::toggled, this, [this](bool bChecked) { SetInteractMode(bChecked); });

  m_pSnapAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.Snap")));
  m_pSnapAction->setCheckable(true);
  m_pSnapAction->setChecked(true);
  m_pSnapAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.SnapTooltip")));

  m_pSafeAreaAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.SafeArea")));
  m_pSafeAreaAction->setCheckable(true);
  m_pSafeAreaAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.SafeAreaTooltip")));
  connect(m_pSafeAreaAction, &QAction::toggled, this, [this](bool) { RefreshCanvasView(); });

  m_pNavGraphAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.NavGraph")));
  m_pNavGraphAction->setCheckable(true);
  m_pNavGraphAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.NavGraphTooltip")));
  connect(m_pNavGraphAction, &QAction::toggled, this, [this](bool) { RefreshCanvasView(); });

  m_pCheckerboardAction = pToolBar->addAction(plMakeQString(plTranslate("GameUi.Toolbar.Checker")));
  m_pCheckerboardAction->setCheckable(true);
  m_pCheckerboardAction->setToolTip(plMakeQString(plTranslate("GameUi.Toolbar.CheckerTooltip")));
  connect(m_pCheckerboardAction, &QAction::toggled, this, [this](bool) { RefreshCanvasView(); });

  addToolBar(pToolBar);
}

void plQtUiDocumentAssetDocumentWindow::PopulateLanguageCombo()
{
  if (m_pLanguageCombo == nullptr)
    return;

  plQtScopedBlockSignals blockSignals(m_pLanguageCombo);

  m_pLanguageCombo->clear();
  m_pLanguageCombo->addItem(plMakeQString(plTranslate("GameUi.Toolbar.LanguageNone")), QString());

  // languages = subfolders of the project's game and editor localization directories
  const QString sProjectDir = QString::fromUtf8(plToolsProject::GetSingleton()->GetProjectDirectory().GetData());

  QStringList languages;

  for (const QString& sRoot : {sProjectDir + "/Localization", sProjectDir + "/Editor/Localization"})
  {
    const QDir dir(sRoot);

    for (const QString& sSub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
      if (!languages.contains(sSub, Qt::CaseInsensitive))
      {
        languages.append(sSub);
      }
    }
  }

  for (const QString& sLanguage : languages)
  {
    m_pLanguageCombo->addItem(QString::fromUtf8("\xF0\x9F\x8C\x90 ") + sLanguage, sLanguage);
  }
}

void plQtUiDocumentAssetDocumentWindow::SendLanguageToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  const QString sLanguage = (m_pLanguageCombo != nullptr) ? m_pLanguageCombo->currentData().toString() : QString();

  plStringBuilder sFolders;

  if (!sLanguage.isEmpty())
  {
    const QString sProjectDir = QString::fromUtf8(plToolsProject::GetSingleton()->GetProjectDirectory().GetData());

    for (const QString& sRoot : {sProjectDir + "/Localization/" + sLanguage, sProjectDir + "/Editor/Localization/" + sLanguage})
    {
      if (QDir(sRoot).exists())
      {
        if (!sFolders.IsEmpty())
        {
          sFolders.Append(";");
        }

        sFolders.Append(sRoot.toUtf8().data());
      }
    }
  }

  {
    plDocumentConfigMsgToEngine msg;
    msg.m_sWhatToDo = "UiLanguage";
    msg.m_sValue = sFolders.GetData();
    GetEditorEngineConnection()->SendMessage(&msg);
  }

  {
    plDocumentConfigMsgToEngine msg;
    msg.m_sWhatToDo = "UiHighlightUntranslated";
    msg.m_iValue = (m_pHighlightUntranslatedAction != nullptr && m_pHighlightUntranslatedAction->isChecked()) ? 1 : 0;
    GetEditorEngineConnection()->SendMessage(&msg);
  }

  // resource-version bump: binding rediscovery re-runs plTranslate with the new chain
  UpdatePreview();
}

void plQtUiDocumentAssetDocumentWindow::SendCanvasViewToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiCanvasView";

  plStringBuilder sValue;
  sValue.SetFormat("{};{};{};{};{};{};{};{}", m_vDeviceResolution.x, m_vDeviceResolution.y, m_fZoom, m_vPan.x, m_vPan.y,
    (m_pSafeAreaAction != nullptr && m_pSafeAreaAction->isChecked()) ? 1 : 0,
    (m_pCheckerboardAction != nullptr && m_pCheckerboardAction->isChecked()) ? 1 : 0,
    (m_pNavGraphAction != nullptr && m_pNavGraphAction->isChecked()) ? 1 : 0);
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plQtUiDocumentAssetDocumentWindow::RefreshCanvasView()
{
  SendCanvasViewToEngine();
  UpdateSelectionUi();

  if (m_pHRuler != nullptr)
    m_pHRuler->update();
  if (m_pVRuler != nullptr)
    m_pVRuler->update();
}

void plQtUiDocumentAssetDocumentWindow::SetInteractMode(bool bEnabled)
{
  if (m_bInteractMode == bEnabled)
    return;

  m_bInteractMode = bEnabled;
  m_bLastInteractButtonDown = false;

  if (bEnabled)
  {
    EndDrag(false); // never keep an editing transaction open while interacting
  }

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiInteract";
  msg.m_iValue = bEnabled ? 1 : 0;

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plQtUiDocumentAssetDocumentWindow::SendInteractMouseState(const QPoint& viewPos, bool bButtonDown, float fWheelTicks /*= 0.0f*/)
{
  const float fWidth = static_cast<float>(m_pViewWidget->width());
  const float fHeight = static_cast<float>(m_pViewWidget->height());

  if (fWidth <= 0.0f || fHeight <= 0.0f)
    return;

  const float fX = plMath::Clamp(viewPos.x() / fWidth, 0.0f, 1.0f);
  const float fY = plMath::Clamp(viewPos.y() / fHeight, 0.0f, 1.0f);

  m_bLastInteractButtonDown = bButtonDown;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiMouse";

  plStringBuilder sValue;
  sValue.SetFormat("{};{};{};{}", fX, fY, bButtonDown ? 1 : 0, fWheelTicks);
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

//////////////////////////////////////////////////////////////////////////
// bindings & mock data

namespace
{
  /// Small modal dialog to create a binding: element (seeded from the selection),
  /// property (filtered by the element's type), blackboard entry name.
  class plQtUiAddBindingDlg : public QDialog
  {
  public:
    plQtUiAddBindingDlg(QWidget* pParent, const plDynamicArray<plQtUiDocumentAssetDocumentWindow::ElementListEntry>& elements, const plUuid& preselectGuid, const QStringList& templateEntries)
      : QDialog(pParent)
      , m_Elements(elements)
    {
      setWindowTitle(plMakeQString(plTranslate("GameUi.Bindings.DlgTitle")));

      QGridLayout* pGrid = new QGridLayout(this);

      pGrid->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Bindings.DlgElement")), this), 0, 0);
      m_pElementCombo = new QComboBox(this);
      for (const auto& element : m_Elements)
      {
        m_pElementCombo->addItem(element.m_sLabel);
      }
      pGrid->addWidget(m_pElementCombo, 0, 1);

      pGrid->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Bindings.DlgProperty")), this), 1, 0);
      m_pPropertyCombo = new QComboBox(this);
      pGrid->addWidget(m_pPropertyCombo, 1, 1);

      pGrid->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Bindings.DlgEntry")), this), 2, 0);
      m_pEntryCombo = new QComboBox(this);
      m_pEntryCombo->setEditable(true); // template entries offered, custom names allowed
      m_pEntryCombo->addItems(templateEntries);
      m_pEntryCombo->setCurrentText("");
      pGrid->addWidget(m_pEntryCombo, 2, 1);

      QDialogButtonBox* pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
      connect(pButtons, &QDialogButtonBox::accepted, this, [this]()
        {
          if (!m_pEntryCombo->currentText().isEmpty() && m_pPropertyCombo->count() > 0)
            accept();
        });
      connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
      pGrid->addWidget(pButtons, 3, 0, 1, 2);

      connect(m_pElementCombo, &QComboBox::currentIndexChanged, this, [this](int) { RepopulateProperties(); });

      // preselect the given element
      for (plUInt32 i = 0; i < m_Elements.GetCount(); ++i)
      {
        if (m_Elements[i].m_Guid == preselectGuid)
        {
          m_pElementCombo->setCurrentIndex(static_cast<int>(i));
          break;
        }
      }

      RepopulateProperties();

      m_pEntryCombo->setFocus();
      setMinimumWidth(340);
    }

    plUuid GetElementGuid() const
    {
      const int iIndex = m_pElementCombo->currentIndex();
      return (iIndex >= 0 && iIndex < static_cast<int>(m_Elements.GetCount())) ? m_Elements[iIndex].m_Guid : plUuid();
    }

    QString GetBindingProperty() const { return m_pPropertyCombo->currentData().toString(); }
    QString GetEntry() const { return m_pEntryCombo->currentText(); }

  private:
    void RepopulateProperties()
    {
      m_pPropertyCombo->clear();

      const int iIndex = m_pElementCombo->currentIndex();
      if (iIndex < 0 || iIndex >= static_cast<int>(m_Elements.GetCount()))
        return;

      plDynamicArray<plString> bindingProps;
      plQtUiDocumentAssetDocumentWindow::GetBindablePropertiesForType(m_Elements[iIndex].m_pObject->GetType(), bindingProps);

      for (const plString& sProp : bindingProps)
      {
        m_pPropertyCombo->addItem(plMakeQString(plTranslate(plQtUiDocumentAssetDocumentWindow::GetBindingPropertyDisplayName(sProp))), QString(sProp.GetData()));
      }
    }

    const plDynamicArray<plQtUiDocumentAssetDocumentWindow::ElementListEntry>& m_Elements;
    QComboBox* m_pElementCombo = nullptr;
    QComboBox* m_pPropertyCombo = nullptr;
    QComboBox* m_pEntryCombo = nullptr;
  };

  /// Small modal dialog to create an animation clip: name + duration. Triggers (event /
  /// entry binding) and loop mode are edited in the property grid afterwards.
  class plQtUiAddClipDlg : public QDialog
  {
  public:
    explicit plQtUiAddClipDlg(QWidget* pParent)
      : QDialog(pParent)
    {
      setWindowTitle(plMakeQString(plTranslate("GameUi.Animations.DlgClipTitle")));

      QGridLayout* pGrid = new QGridLayout(this);

      pGrid->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Animations.DlgName")), this), 0, 0);
      m_pNameEdit = new QLineEdit(this);
      pGrid->addWidget(m_pNameEdit, 0, 1);

      pGrid->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Animations.DlgDuration")), this), 1, 0);
      m_pDurationSpin = new QDoubleSpinBox(this);
      m_pDurationSpin->setRange(0.01, 3600.0);
      m_pDurationSpin->setDecimals(2);
      m_pDurationSpin->setSingleStep(0.1);
      m_pDurationSpin->setValue(0.8);
      m_pDurationSpin->setSuffix(" s");
      pGrid->addWidget(m_pDurationSpin, 1, 1);

      QDialogButtonBox* pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
      connect(pButtons, &QDialogButtonBox::accepted, this, [this]()
        {
          if (!m_pNameEdit->text().isEmpty())
            accept();
        });
      connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
      pGrid->addWidget(pButtons, 2, 0, 1, 2);

      m_pNameEdit->setFocus();
      setMinimumWidth(320);
    }

    QString GetName() const { return m_pNameEdit->text(); }
    double GetDuration() const { return m_pDurationSpin->value(); }

  private:
    QLineEdit* m_pNameEdit = nullptr;
    QDoubleSpinBox* m_pDurationSpin = nullptr;
  };

  /// Small modal dialog to add a track to a clip: animated element (by NAME — only named
  /// elements are offered) + target channel.
  class plQtUiAddTrackDlg : public QDialog
  {
  public:
    plQtUiAddTrackDlg(QWidget* pParent, const QStringList& elementNames, const QString& sPreselect)
      : QDialog(pParent)
    {
      setWindowTitle(plMakeQString(plTranslate("GameUi.Animations.DlgTrackTitle")));

      QGridLayout* pGrid = new QGridLayout(this);

      pGrid->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Bindings.DlgElement")), this), 0, 0);
      m_pElementCombo = new QComboBox(this);
      m_pElementCombo->setEditable(true); // free names allowed (element may be named later)
      m_pElementCombo->addItems(elementNames);
      m_pElementCombo->setCurrentText(sPreselect);
      pGrid->addWidget(m_pElementCombo, 0, 1);

      pGrid->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Animations.DlgTarget")), this), 1, 0);
      m_pTargetCombo = new QComboBox(this);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::OffsetX")), plUiAnimTarget::OffsetX);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::OffsetY")), plUiAnimTarget::OffsetY);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::Width")), plUiAnimTarget::Width);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::Height")), plUiAnimTarget::Height);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::Opacity")), plUiAnimTarget::Opacity);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::Scale")), plUiAnimTarget::Scale);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::Rotation")), plUiAnimTarget::Rotation);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::Tint")), plUiAnimTarget::Tint);
      m_pTargetCombo->addItem(plMakeQString(plTranslate("plUiAnimTarget::Fill")), plUiAnimTarget::Fill);
      pGrid->addWidget(m_pTargetCombo, 1, 1);

      QDialogButtonBox* pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
      connect(pButtons, &QDialogButtonBox::accepted, this, [this]()
        {
          if (!m_pElementCombo->currentText().isEmpty())
            accept();
        });
      connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
      pGrid->addWidget(pButtons, 2, 0, 1, 2);

      setMinimumWidth(320);
    }

    QString GetElementName() const { return m_pElementCombo->currentText(); }
    int GetTarget() const { return m_pTargetCombo->currentData().toInt(); }

  private:
    QComboBox* m_pElementCombo = nullptr;
    QComboBox* m_pTargetCombo = nullptr;
  };

  /// default key value for a track target (the identity of that channel)
  float GetTargetDefaultValue(plInt32 iTarget)
  {
    switch (iTarget)
    {
      case plUiAnimTarget::Opacity:
      case plUiAnimTarget::Scale:
      case plUiAnimTarget::Fill:
        return 1.0f;
      default:
        return 0.0f; // offsets, size deltas, rotation
    }
  }
} // namespace

void plQtUiDocumentAssetDocumentWindow::CollectElementList(plDynamicArray<ElementListEntry>& out_elements) const
{
  out_elements.Clear();

  std::function<void(const plDocumentObject*)> collect = [&](const plDocumentObject* pParentObj)
  {
    // gather in property-index order (same pattern as RebuildHierarchy)
    plHybridArray<const plDocumentObject*, 16> ordered;

    for (const plDocumentObject* pChild : pParentObj->GetChildren())
    {
      if (pChild->GetParentProperty() == "Children")
      {
        ordered.PushBack(pChild);
      }
    }

    ordered.Sort([](const plDocumentObject* a, const plDocumentObject* b)
      { return a->GetPropertyIndex().ConvertTo<plInt32>() < b->GetPropertyIndex().ConvertTo<plInt32>(); });

    for (const plDocumentObject* pChild : ordered)
    {
      plStringBuilder sLabel;

      const plVariant name = pChild->GetTypeAccessor().GetValue("Name");
      if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
      {
        sLabel.SetFormat("{} ({})", name.Get<plString>(), plTranslate(pChild->GetType()->GetTypeName()));
      }
      else
      {
        sLabel = plTranslate(pChild->GetType()->GetTypeName());
      }

      ElementListEntry& entry = out_elements.ExpandAndGetRef();
      entry.m_Guid = pChild->GetGuid();
      entry.m_sLabel = sLabel.GetData();
      entry.m_pObject = pChild;

      collect(pChild);
    }
  };

  collect(GetPropertiesObject());
}

void plQtUiDocumentAssetDocumentWindow::GetBindablePropertiesForType(const plRTTI* pType, plDynamicArray<plString>& out_bindingProps)
{
  out_bindingProps.Clear();

  out_bindingProps.PushBack("VisibleBinding"); // every element

  if (pType->IsDerivedFrom<plUiProgressBar>())
  {
    out_bindingProps.PushBack("FillAmountBinding");
  }

  if (pType->IsDerivedFrom<plUiText>() || pType->IsDerivedFrom<plUiTextInput>())
  {
    out_bindingProps.PushBack("TextBinding");
  }

  if (pType->IsDerivedFrom<plUiSlider>())
  {
    out_bindingProps.PushBack("ValueBinding"); // two-way: display + drag write
  }

  if (pType->IsDerivedFrom<plUiItemList>())
  {
    out_bindingProps.PushBack("ItemsBinding");    // array of item dictionaries (or a count)
    out_bindingProps.PushBack("SelectedBinding"); // clicked row index is written here
  }

  if (pType->IsDerivedFrom<plUiButton>())
  {
    out_bindingProps.PushBack("ClickEntryBinding"); // event -> entry write
    out_bindingProps.PushBack("DisabledBinding");
  }
}

const char* plQtUiDocumentAssetDocumentWindow::GetBindingPropertyDisplayName(plStringView sBindingProperty)
{
  if (sBindingProperty == "FillAmountBinding")
    return "FillAmount";
  if (sBindingProperty == "TextBinding")
    return "Text";
  if (sBindingProperty == "ClickEntryBinding")
    return "GameUi.Bindings.ClickProperty";
  if (sBindingProperty == "ValueBinding")
    return "Value";
  if (sBindingProperty == "ItemsBinding")
    return "ItemsBinding";
  if (sBindingProperty == "SelectedBinding")
    return "SelectedBinding";
  if (sBindingProperty == "DisabledBinding")
    return "GameUi.Bindings.DisabledProperty";

  return "Visible";
}

void plQtUiDocumentAssetDocumentWindow::LoadTemplateEntries()
{
  const plString& sTemplate = m_pAssetDoc->GetProperties()->m_sBlackboardTemplate;

  if (m_sLoadedTemplate == sTemplate)
    return;

  m_sLoadedTemplate = sTemplate;
  m_TemplateEntries.Clear();

  if (sTemplate.IsEmpty())
    return;

  // resolve the asset reference to its transformed output file
  plAssetCurator::plLockedSubAsset pSubAsset;

  if (plConversionUtils::IsStringUuid(sTemplate))
  {
    pSubAsset = plAssetCurator::GetSingleton()->GetSubAsset(plConversionUtils::ConvertStringToUuid(sTemplate));
  }
  else
  {
    pSubAsset = plAssetCurator::GetSingleton()->FindSubAsset(sTemplate);
  }

  if (!pSubAsset.isValid())
    return;

  const plString sOutputFile = pSubAsset->m_pAssetInfo->GetManager()->GetAbsoluteOutputFileName(pSubAsset->m_pAssetInfo->m_pDocumentTypeDescriptor, pSubAsset->m_pAssetInfo->m_Path, "");

  plFileReader file;
  if (file.Open(sOutputFile).Failed())
    return; // not transformed yet; entries stay empty until the template asset is saved

  plAssetFileHeader assetHeader;
  assetHeader.Read(file).IgnoreResult();

  plBlackboardTemplateResourceDescriptor desc;
  if (desc.Deserialize(file).Failed())
    return;

  for (const plBlackboardEntry& entry : desc.m_Entries)
  {
    plQtUiBindingsPanel::MockEntry& mockEntry = m_TemplateEntries.ExpandAndGetRef();
    mockEntry.m_sName = entry.m_sName.GetString();
    mockEntry.m_sValue = entry.m_InitialValue.CanConvertTo<plString>() ? entry.m_InitialValue.ConvertTo<plString>() : plString();
  }
}

void plQtUiDocumentAssetDocumentWindow::OnAddBindingRequested()
{
  plDynamicArray<ElementListEntry> elements;
  CollectElementList(elements);

  if (elements.IsEmpty())
    return;

  // seed from the current selection if it is an element
  plUuid preselectGuid;
  if (const plDocumentObject* pSelected = GetDocument()->GetSelectionManager()->GetCurrentObject())
  {
    plHybridArray<plUInt32, 8> path;
    if (ComputeElementPath(pSelected, path) && !path.IsEmpty())
    {
      preselectGuid = pSelected->GetGuid();
    }
  }

  QStringList templateEntries;
  for (const auto& entry : m_TemplateEntries)
  {
    templateEntries.append(entry.m_sName.GetData());
  }

  plQtUiAddBindingDlg dlg(this, elements, preselectGuid, templateEntries);

  if (dlg.exec() != QDialog::Accepted)
    return;

  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(dlg.GetElementGuid());
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI Binding");
  if (pAccessor->SetValueByName(pObj, dlg.GetBindingProperty().toUtf8().data(), plString(dlg.GetEntry().toUtf8().data())).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnRemoveBindingRequested(const plUuid& elementGuid, const QString& sBindingProperty)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Remove UI Binding");
  if (pAccessor->SetValueByName(pObj, sBindingProperty.toUtf8().data(), plString()).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnBindingEntryEdited(const plUuid& elementGuid, const QString& sBindingProperty, const QString& sNewEntry)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Change UI Binding");
  if (pAccessor->SetValueByName(pObj, sBindingProperty.toUtf8().data(), plString(sNewEntry.toUtf8().data())).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnBindingRowActivated(const plUuid& elementGuid)
{
  if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObj);
  }
}

void plQtUiDocumentAssetDocumentWindow::UpdateBindingsList()
{
  if (m_pBindingsPanel == nullptr)
    return;

  LoadTemplateEntries();
  m_pBindingsPanel->SetTemplateEntries(m_TemplateEntries);

  // seed helper: template initial value wins over the property-derived fallback
  auto seedFor = [this](const plString& sEntry, const plString& sFallback) -> plString
  {
    for (const auto& templateEntry : m_TemplateEntries)
    {
      if (templateEntry.m_sName == sEntry)
        return templateEntry.m_sValue;
    }
    return sFallback;
  };

  plDynamicArray<plQtUiBindingsPanel::BindingInfo> bindings;

  plDynamicArray<ElementListEntry> elements;
  CollectElementList(elements);

  plStringBuilder tmp;

  for (const ElementListEntry& element : elements)
  {
    const plDocumentObject* pObj = element.m_pObject;
    const auto& accessor = pObj->GetTypeAccessor();

    plStringBuilder sElementName;
    const plVariant name = accessor.GetValue("Name");
    if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
    {
      sElementName = name.Get<plString>();
    }
    else
    {
      sElementName = plTranslate(pObj->GetType()->GetTypeName());
    }

    auto addBinding = [&](const char* szBindingProp, const plString& sEntry, const plString& sSeed)
    {
      plQtUiBindingsPanel::BindingInfo& info = bindings.ExpandAndGetRef();
      info.m_ElementGuid = pObj->GetGuid();
      info.m_sElement = sElementName;
      info.m_sProperty = plTranslate(GetBindingPropertyDisplayName(szBindingProp));
      info.m_sBindingProperty = szBindingProp;
      info.m_sEntry = sEntry;
      info.m_sSeedValue = sSeed;
    };

    // Visible binding (all elements): seed "true"
    {
      const plVariant binding = accessor.GetValue("VisibleBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        addBinding("VisibleBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), "true"));
      }
    }

    // FillAmount binding (progress bars): seed = authored fill mapped into the binding range
    {
      const plVariant binding = accessor.GetValue("FillAmountBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        const float fFill = accessor.GetValue("FillAmount").ConvertTo<float>();
        const float fMin = accessor.GetValue("BindingMin").ConvertTo<float>();
        const float fMax = accessor.GetValue("BindingMax").ConvertTo<float>();

        tmp.SetFormat("{}", fMin + fFill * (fMax - fMin));
        addBinding("FillAmountBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), tmp.GetData()));
      }
    }

    // Value binding (sliders): seed = authored value
    {
      const plVariant binding = accessor.GetValue("ValueBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        tmp.SetFormat("{}", accessor.GetValue("Value").ConvertTo<float>());
        addBinding("ValueBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), tmp.GetData()));
      }
    }

    // Items binding (item lists): seed = a placeholder COUNT so the preview shows rows
    {
      const plVariant binding = accessor.GetValue("ItemsBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        addBinding("ItemsBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), "4"));
      }
    }

    // Selected binding (item lists): the clicked row index
    {
      const plVariant binding = accessor.GetValue("SelectedBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        addBinding("SelectedBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), "0"));
      }
    }

    // Text binding (texts): seed = authored text
    {
      const plVariant binding = accessor.GetValue("TextBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        const plVariant text = accessor.GetValue("Text");
        addBinding("TextBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), text.IsA<plString>() ? text.Get<plString>() : plString()));
      }
    }

    // Click entry binding (buttons): writes the entry on click
    {
      const plVariant binding = accessor.GetValue("ClickEntryBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        addBinding("ClickEntryBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), "false"));
      }
    }

    // state machine: one row per state that watches an entry (editable on the STATE object)
    for (const plDocumentObject* pStateObj : pObj->GetChildren())
    {
      if (pStateObj->GetParentProperty() != "States")
        continue;

      const plVariant entry = pStateObj->GetTypeAccessor().GetValue("Entry");
      if (!entry.IsA<plString>() || entry.Get<plString>().IsEmpty())
        continue;

      plStringBuilder sProperty(plTranslate("GameUi.Bindings.StateProperty"));

      const plVariant stateName = pStateObj->GetTypeAccessor().GetValue("Name");
      if (stateName.IsA<plString>() && !stateName.Get<plString>().IsEmpty())
      {
        sProperty.AppendFormat(": {}", stateName.Get<plString>());
      }

      plQtUiBindingsPanel::BindingInfo& info = bindings.ExpandAndGetRef();
      info.m_ElementGuid = pStateObj->GetGuid(); // row activation selects the state object
      info.m_sElement = sElementName;
      info.m_sProperty = sProperty;
      info.m_sBindingProperty = "Entry"; // editable: writes the state object
      info.m_sEntry = entry.Get<plString>();
      info.m_sSeedValue = seedFor(entry.Get<plString>(), "0");
    }

    // disabled binding (buttons)
    {
      const plVariant binding = accessor.GetValue("DisabledBinding");
      if (binding.IsA<plString>() && !binding.Get<plString>().IsEmpty())
      {
        addBinding("DisabledBinding", binding.Get<plString>(), seedFor(binding.Get<plString>(), "false"));
      }
    }
  }

  // animation clips driven by blackboard entries (document level)
  for (const plDocumentObject* pClipObj : GetPropertiesObject()->GetChildren())
  {
    if (pClipObj->GetParentProperty() != "Animations")
      continue;

    const plVariant entryBinding = pClipObj->GetTypeAccessor().GetValue("EntryBinding");
    if (!entryBinding.IsA<plString>() || entryBinding.Get<plString>().IsEmpty())
      continue;

    plStringBuilder sProperty(plTranslate("GameUi.Bindings.AnimProperty"));

    const plVariant clipName = pClipObj->GetTypeAccessor().GetValue("Name");
    if (clipName.IsA<plString>() && !clipName.Get<plString>().IsEmpty())
    {
      sProperty.AppendFormat(": {}", clipName.Get<plString>());
    }

    const plVariant mode = pClipObj->GetTypeAccessor().GetValue("Mode");
    const bool bValueMode = mode.CanConvertTo<plInt32>() && mode.ConvertTo<plInt32>() == plUiAnimationMode::Value;

    plQtUiBindingsPanel::BindingInfo& info = bindings.ExpandAndGetRef();
    info.m_ElementGuid = pClipObj->GetGuid(); // row activation selects the clip object
    info.m_sElement = plTranslate("GameUi.Bindings.DocumentRow");
    info.m_sProperty = sProperty;
    info.m_sBindingProperty = "EntryBinding"; // editable: writes the clip object
    info.m_sEntry = entryBinding.Get<plString>();
    info.m_sSeedValue = seedFor(entryBinding.Get<plString>(), bValueMode ? "0.5" : "false");
  }

  m_pBindingsPanel->SetBindings(bindings);

  UpdateScenariosList();
}

//////////////////////////////////////////////////////////////////////////
// animations panel

void plQtUiDocumentAssetDocumentWindow::UpdateAnimationsList()
{
  if (m_pAnimationsPanel == nullptr)
    return;

  // element names, to flag tracks whose element does not exist (yet)
  plDynamicArray<plString> elementNames;
  {
    plDynamicArray<ElementListEntry> elements;
    CollectElementList(elements);

    for (const ElementListEntry& element : elements)
    {
      const plVariant name = element.m_pObject->GetTypeAccessor().GetValue("Name");
      if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
      {
        elementNames.PushBack(name.Get<plString>());
      }
    }
  }

  auto childrenOf = [](const plDocumentObject* pParent, const char* szProperty, plHybridArray<const plDocumentObject*, 16>& out_ordered)
  {
    out_ordered.Clear();

    for (const plDocumentObject* pChild : pParent->GetChildren())
    {
      if (pChild->GetParentProperty() == szProperty)
      {
        out_ordered.PushBack(pChild);
      }
    }

    out_ordered.Sort([](const plDocumentObject* a, const plDocumentObject* b)
      { return a->GetPropertyIndex().ConvertTo<plInt32>() < b->GetPropertyIndex().ConvertTo<plInt32>(); });
  };

  plDynamicArray<plQtUiAnimationsPanel::ClipInfo> clips;

  plHybridArray<const plDocumentObject*, 16> clipObjects, trackObjects, keyObjects;
  childrenOf(GetPropertiesObject(), "Animations", clipObjects);

  for (const plDocumentObject* pClipObj : clipObjects)
  {
    const auto& clip = pClipObj->GetTypeAccessor();

    plQtUiAnimationsPanel::ClipInfo& info = clips.ExpandAndGetRef();
    info.m_Guid = pClipObj->GetGuid();

    const plVariant name = clip.GetValue("Name");
    if (name.IsA<plString>())
      info.m_sName = name.Get<plString>();

    info.m_fDuration = clip.GetValue("Duration").ConvertTo<float>();

    const plVariant playOn = clip.GetValue("PlayOnEvent");
    if (playOn.IsA<plString>())
      info.m_sPlayOnEvent = playOn.Get<plString>();

    const plVariant entry = clip.GetValue("EntryBinding");
    if (entry.IsA<plString>())
      info.m_sEntry = entry.Get<plString>();

    const plVariant mode = clip.GetValue("Mode");
    info.m_bValueMode = mode.CanConvertTo<plInt32>() && mode.ConvertTo<plInt32>() == plUiAnimationMode::Value;

    childrenOf(pClipObj, "Tracks", trackObjects);

    for (const plDocumentObject* pTrackObj : trackObjects)
    {
      const auto& track = pTrackObj->GetTypeAccessor();

      plQtUiAnimationsPanel::TrackInfo& trackInfo = info.m_Tracks.ExpandAndGetRef();
      trackInfo.m_Guid = pTrackObj->GetGuid();

      const plVariant element = track.GetValue("Element");
      if (element.IsA<plString>())
        trackInfo.m_sElement = element.Get<plString>();

      trackInfo.m_iTarget = track.GetValue("Target").ConvertTo<plInt32>();
      trackInfo.m_bElementFound = elementNames.Contains(trackInfo.m_sElement);

      childrenOf(pTrackObj, "Keys", keyObjects);

      for (const plDocumentObject* pKeyObj : keyObjects)
      {
        const auto& key = pKeyObj->GetTypeAccessor();

        plQtUiAnimationsPanel::KeyInfo& keyInfo = trackInfo.m_Keys.ExpandAndGetRef();
        keyInfo.m_Guid = pKeyObj->GetGuid();
        keyInfo.m_fTime = key.GetValue("Time").ConvertTo<float>();
        keyInfo.m_fValue = key.GetValue("Value").ConvertTo<float>();
        keyInfo.m_iEasing = key.GetValue("Easing").ConvertTo<plInt32>();

        const plVariant color = key.GetValue("Color");
        if (color.IsA<plColor>())
          keyInfo.m_Color = color.Get<plColor>();
      }

      trackInfo.m_Keys.Sort([](const plQtUiAnimationsPanel::KeyInfo& a, const plQtUiAnimationsPanel::KeyInfo& b)
        { return a.m_fTime < b.m_fTime; });
    }
  }

  m_pAnimationsPanel->SetClips(clips);
}

void plQtUiDocumentAssetDocumentWindow::OnAddClipRequested()
{
  plQtUiAddClipDlg dlg(this);

  if (dlg.exec() != QDialog::Accepted)
    return;

  const plDocumentObject* pProperties = GetPropertiesObject();
  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI Animation Clip");

  plUuid clipGuid = plUuid::MakeUuid();
  if (pAccessor->AddObjectByName(pProperties, "Animations", pAccessor->GetCountByName(pProperties, "Animations"), plGetStaticRTTI<plUiAnimationClip>(), clipGuid).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }

  if (const plDocumentObject* pClip = GetDocument()->GetObjectManager()->GetObject(clipGuid))
  {
    pAccessor->SetValueByName(pClip, "Name", plString(dlg.GetName().toUtf8().data())).IgnoreResult();
    pAccessor->SetValueByName(pClip, "Duration", static_cast<float>(dlg.GetDuration())).IgnoreResult();
  }

  pAccessor->FinishTransaction();

  // show the clip in the property grid so triggers/loop mode can be set right away
  if (const plDocumentObject* pClip = GetDocument()->GetObjectManager()->GetObject(clipGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pClip);
  }
}

void plQtUiDocumentAssetDocumentWindow::OnRemoveClipRequested(const plUuid& clipGuid)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(clipGuid);
  if (pObj == nullptr || pObj->GetParentProperty() != "Animations")
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Remove UI Animation Clip");
  if (pAccessor->RemoveObject(pObj).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnClipActivated(const plUuid& clipGuid)
{
  if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(clipGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObj);
  }
}

void plQtUiDocumentAssetDocumentWindow::OnAddTrackRequested(const plUuid& clipGuid)
{
  const plDocumentObject* pClip = GetDocument()->GetObjectManager()->GetObject(clipGuid);
  if (pClip == nullptr)
    return;

  // offer the NAMED elements (tracks reference elements by name)
  QStringList elementNames;
  QString sPreselect;
  {
    plDynamicArray<ElementListEntry> elements;
    CollectElementList(elements);

    const plDocumentObject* pSelected = GetDocument()->GetSelectionManager()->GetCurrentObject();

    for (const ElementListEntry& element : elements)
    {
      const plVariant name = element.m_pObject->GetTypeAccessor().GetValue("Name");
      if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
      {
        const QString sName = QString::fromUtf8(name.Get<plString>().GetData());
        elementNames.append(sName);

        if (element.m_pObject == pSelected)
        {
          sPreselect = sName;
        }
      }
    }
  }

  plQtUiAddTrackDlg dlg(this, elementNames, sPreselect);

  if (dlg.exec() != QDialog::Accepted)
    return;

  const float fDuration = pClip->GetTypeAccessor().GetValue("Duration").ConvertTo<float>();
  const float fDefaultValue = GetTargetDefaultValue(dlg.GetTarget());

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI Animation Track");

  plUuid trackGuid = plUuid::MakeUuid();
  if (pAccessor->AddObjectByName(pClip, "Tracks", pAccessor->GetCountByName(pClip, "Tracks"), plGetStaticRTTI<plUiAnimTrack>(), trackGuid).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }

  if (const plDocumentObject* pTrack = GetDocument()->GetObjectManager()->GetObject(trackGuid))
  {
    pAccessor->SetValueByName(pTrack, "Element", plString(dlg.GetElementName().toUtf8().data())).IgnoreResult();
    pAccessor->SetValueByName(pTrack, "Target", dlg.GetTarget()).IgnoreResult();

    // start with a key at both ends, so the track is immediately editable
    const float keyTimes[2] = {0.0f, plMath::Max(fDuration, 0.01f)};

    for (plUInt32 i = 0; i < 2; ++i)
    {
      plUuid keyGuid = plUuid::MakeUuid();
      if (pAccessor->AddObjectByName(pTrack, "Keys", i, plGetStaticRTTI<plUiAnimKey>(), keyGuid).Succeeded())
      {
        if (const plDocumentObject* pKey = GetDocument()->GetObjectManager()->GetObject(keyGuid))
        {
          pAccessor->SetValueByName(pKey, "Time", keyTimes[i]).IgnoreResult();
          pAccessor->SetValueByName(pKey, "Value", fDefaultValue).IgnoreResult();
        }
      }
    }
  }

  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnRemoveTrackRequested(const plUuid& trackGuid)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(trackGuid);
  if (pObj == nullptr || pObj->GetParentProperty() != "Tracks")
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Remove UI Animation Track");
  if (pAccessor->RemoveObject(pObj).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnAddKeyRequested(const plUuid& trackGuid, float fTime, bool bSelectKey)
{
  const plDocumentObject* pTrack = GetDocument()->GetObjectManager()->GetObject(trackGuid);
  if (pTrack == nullptr)
    return;

  const float fDefaultValue = GetTargetDefaultValue(pTrack->GetTypeAccessor().GetValue("Target").ConvertTo<plInt32>());

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI Animation Key");

  plUuid keyGuid = plUuid::MakeUuid();
  if (pAccessor->AddObjectByName(pTrack, "Keys", pAccessor->GetCountByName(pTrack, "Keys"), plGetStaticRTTI<plUiAnimKey>(), keyGuid).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }

  if (const plDocumentObject* pKey = GetDocument()->GetObjectManager()->GetObject(keyGuid))
  {
    pAccessor->SetValueByName(pKey, "Time", fTime).IgnoreResult();
    pAccessor->SetValueByName(pKey, "Value", fDefaultValue).IgnoreResult();
  }

  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnRemoveKeyRequested(const plUuid& keyGuid)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(keyGuid);
  if (pObj == nullptr || pObj->GetParentProperty() != "Keys")
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Remove UI Animation Key");
  if (pAccessor->RemoveObject(pObj).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnKeyMoved(const plUuid& keyGuid, float fNewTime)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(keyGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Move UI Animation Key");
  if (pAccessor->SetValueByName(pObj, "Time", fNewTime).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnKeyEdited(const plUuid& keyGuid, float fTime, float fValue, const plColor& color, int iEasing)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(keyGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Edit UI Animation Key");
  pAccessor->SetValueByName(pObj, "Time", fTime).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Value", fValue).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Color", color).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Easing", iEasing).IgnoreResult();
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnClipScrubbed(const QString& sClipName, float fTime)
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiClipPreview";

  // time first — the clip name may contain any characters
  plStringBuilder sValue;
  sValue.SetFormat("{};{}", fTime, sClipName.toUtf8().data());
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plQtUiDocumentAssetDocumentWindow::OnClipPlay(const QString& sClipName)
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiClipPlay";
  msg.m_sValue = sClipName.toUtf8().data();

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plQtUiDocumentAssetDocumentWindow::OnClipStop()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiClipStop";

  GetEditorEngineConnection()->SendMessage(&msg);
}

//////////////////////////////////////////////////////////////////////////
// states & transitions panel

namespace
{
  /// display name of an asset reference (guid or path), for read-only labels
  plString GetAssetRefDisplayName(const plString& sRef)
  {
    if (sRef.IsEmpty())
      return plString();

    plAssetCurator::plLockedSubAsset pSubAsset;

    if (plConversionUtils::IsStringUuid(sRef))
    {
      pSubAsset = plAssetCurator::GetSingleton()->GetSubAsset(plConversionUtils::ConvertStringToUuid(sRef));
    }
    else
    {
      pSubAsset = plAssetCurator::GetSingleton()->FindSubAsset(sRef);
    }

    if (pSubAsset.isValid())
    {
      return pSubAsset->GetName();
    }

    // unknown asset: show the tail of the reference
    plStringBuilder tmp(sRef);
    return plString(tmp.GetFileName());
  }
} // namespace

void plQtUiDocumentAssetDocumentWindow::UpdateStatesPanel()
{
  if (m_pStatesPanel == nullptr)
    return;

  LoadTemplateEntries();

  // offer the template entries in the binding combos
  {
    plDynamicArray<plString> entryNames;
    for (const auto& entry : m_TemplateEntries)
    {
      entryNames.PushBack(entry.m_sName);
    }

    m_pStatesPanel->SetTemplateEntries(entryNames);
  }

  // offer the document's clips in the Clip column
  {
    plDynamicArray<plString> clipNames;

    for (const plDocumentObject* pChild : GetPropertiesObject()->GetChildren())
    {
      if (pChild->GetParentProperty() != "Animations")
        continue;

      const plVariant name = pChild->GetTypeAccessor().GetValue("Name");
      if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
      {
        clipNames.PushBack(name.Get<plString>());
      }
    }

    m_pStatesPanel->SetClipNames(clipNames);
  }

  // the selected element (if any)
  const plDocumentObject* pSelected = GetDocument()->GetSelectionManager()->GetCurrentObject();

  plHybridArray<plUInt32, 8> path;
  if (pSelected == nullptr || !ComputeElementPath(pSelected, path) || path.IsEmpty())
  {
    m_pStatesPanel->SetElement(nullptr);
    return;
  }

  const auto& accessor = pSelected->GetTypeAccessor();

  plQtUiStatesPanel::ElementStateInfo info;
  info.m_ElementGuid = pSelected->GetGuid();
  info.m_bIsButton = pSelected->GetType()->IsDerivedFrom<plUiButton>();

  {
    const plVariant name = accessor.GetValue("Name");
    if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
    {
      info.m_sElementLabel = name.Get<plString>();
    }
    else
    {
      info.m_sElementLabel = plTranslate(pSelected->GetType()->GetTypeName());
    }
  }

  auto getString = [&](const char* szProperty) -> plString
  {
    const plVariant value = accessor.GetValue(szProperty);
    return value.IsA<plString>() ? value.Get<plString>() : plString();
  };

  info.m_sStyleName = GetAssetRefDisplayName(getString("Style"));
  info.m_sDisabledBinding = getString("DisabledBinding"); // invalid variant on non-buttons -> empty

  // the element's state machine (doc objects under the "States" property, in order)
  {
    plHybridArray<const plDocumentObject*, 16> stateObjects;

    for (const plDocumentObject* pChild : pSelected->GetChildren())
    {
      if (pChild->GetParentProperty() == "States")
      {
        stateObjects.PushBack(pChild);
      }
    }

    stateObjects.Sort([](const plDocumentObject* a, const plDocumentObject* b)
      { return a->GetPropertyIndex().ConvertTo<plInt32>() < b->GetPropertyIndex().ConvertTo<plInt32>(); });

    for (const plDocumentObject* pStateObj : stateObjects)
    {
      const auto& state = pStateObj->GetTypeAccessor();

      plQtUiStatesPanel::StateRow& row = info.m_States.ExpandAndGetRef();
      row.m_Guid = pStateObj->GetGuid();

      const plVariant name = state.GetValue("Name");
      if (name.IsA<plString>())
        row.m_sName = name.Get<plString>();

      const plVariant entry = state.GetValue("Entry");
      if (entry.IsA<plString>())
        row.m_sEntry = entry.Get<plString>();

      row.m_iCondition = state.GetValue("Condition").ConvertTo<plInt32>();
      row.m_fValue = state.GetValue("Value").ConvertTo<double>();

      const plVariant valueText = state.GetValue("ValueText");
      if (valueText.IsA<plString>())
        row.m_sValueText = valueText.Get<plString>();

      row.m_iVisibility = state.GetValue("Visibility").ConvertTo<plInt32>();

      const plVariant style = state.GetValue("Style");
      if (style.IsA<plString>())
      {
        row.m_sStyleRef = style.Get<plString>();
        row.m_sStyleName = GetAssetRefDisplayName(row.m_sStyleRef);
      }

      const plVariant playClip = state.GetValue("PlayClip");
      if (playClip.IsA<plString>())
        row.m_sPlayClip = playClip.Get<plString>();
    }
  }

  m_pStatesPanel->SetElement(&info);
}

void plQtUiDocumentAssetDocumentWindow::OnAddStateRequested(const plUuid& elementGuid)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI State");

  plUuid stateGuid = plUuid::MakeUuid();
  if (pAccessor->AddObjectByName(pObj, "States", pAccessor->GetCountByName(pObj, "States"), plGetStaticRTTI<plUiElementState>(), stateGuid).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }

  if (const plDocumentObject* pState = GetDocument()->GetObjectManager()->GetObject(stateGuid))
  {
    plStringBuilder sName;
    sName.SetFormat("State{}", pAccessor->GetCountByName(pObj, "States"));
    pAccessor->SetValueByName(pState, "Name", plString(sName)).IgnoreResult();
  }

  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnRemoveStateRequested(const plUuid& stateGuid)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(stateGuid);
  if (pObj == nullptr || pObj->GetParentProperty() != "States")
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Remove UI State");
  if (pAccessor->RemoveObject(pObj).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnStateEdited(const plUuid& stateGuid, const QString& sName, const QString& sEntry, int iCondition, double fValue, const QString& sValueText, int iVisibility, const QString& sPlayClip)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(stateGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Edit UI State");
  pAccessor->SetValueByName(pObj, "Name", plString(sName.trimmed().toUtf8().data())).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Entry", plString(sEntry.toUtf8().data())).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Condition", iCondition).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Value", fValue).IgnoreResult();
  pAccessor->SetValueByName(pObj, "ValueText", plString(sValueText.toUtf8().data())).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Visibility", iVisibility).IgnoreResult();
  pAccessor->SetValueByName(pObj, "PlayClip", plString(sPlayClip.toUtf8().data())).IgnoreResult();
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnElementStringPropertyChanged(const plUuid& elementGuid, const char* szProperty, const QString& sValue)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Edit UI Element State");
  if (pAccessor->SetValueByName(pObj, szProperty, plString(sValue.toUtf8().data())).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::OnMockTestValue(const QString& sEntry, const QString& sValue)
{
  if (m_pBindingsPanel == nullptr)
    return;

  m_pBindingsPanel->SetMockValueSilently(sEntry.toUtf8().data(), sValue.toUtf8().data());
  SendMockBlackboardToEngine();
}

void plQtUiDocumentAssetDocumentWindow::ProcessMessageEventHandler(const plEditorEngineDocumentMsg* pMsg)
{
  if (auto pConfigMsg = plDynamicCast<const plSimpleDocumentConfigMsgToEditor*>(pMsg))
  {
    if (pConfigMsg->m_sWhatToDo == "UiMockChanged")
    {
      // an interact-mode click wrote the preview blackboard: reflect it in the mock table
      if (m_pBindingsPanel != nullptr)
      {
        plStringBuilder sValue;
        if (pConfigMsg->m_PayloadValue.CanConvertTo<plString>())
        {
          sValue = pConfigMsg->m_PayloadValue.ConvertTo<plString>();
        }

        m_pBindingsPanel->SetMockValueSilently(pConfigMsg->m_sPayload, sValue);
      }

      return;
    }

    if (pConfigMsg->m_sWhatToDo == "UiDrawStats")
    {
      // m_sPayload = "batches;quads"
      if (m_pDrawStatsLabel != nullptr)
      {
        plHybridArray<plStringView, 2> parts;
        pConfigMsg->m_sPayload.GetView().Split(false, parts, ";");

        if (parts.GetCount() == 2)
        {
          plStringBuilder sBatches(parts[0]), sQuads(parts[1]);
          m_pDrawStatsLabel->setText(QString("draw: %1 batches \xC2\xB7 %2 quads").arg(sBatches.GetData(), sQuads.GetData()));
        }
      }

      return;
    }
  }

  plQtEngineDocumentWindow::ProcessMessageEventHandler(pMsg);
}

void plQtUiDocumentAssetDocumentWindow::SendMockBlackboardToEngine()
{
  if (m_pBindingsPanel == nullptr || plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDynamicArray<plQtUiBindingsPanel::MockEntry> entries;
  m_pBindingsPanel->GetMockEntries(entries);

  // newline-separated name/value pairs (values may legitimately contain semicolons)
  plStringBuilder sValue;
  for (const auto& entry : entries)
  {
    if (!sValue.IsEmpty())
      sValue.Append("\n");

    sValue.Append(entry.m_sName);
    sValue.Append("\n");
    sValue.Append(entry.m_sValue);
  }

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiMockBlackboard";
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plQtUiDocumentAssetDocumentWindow::UpdateScenariosList()
{
  if (m_pBindingsPanel == nullptr)
    return;

  QStringList names;

  for (const plUiMockScenario* pScenario : m_pAssetDoc->GetProperties()->m_MockScenarios)
  {
    if (pScenario != nullptr && !pScenario->m_sName.IsEmpty())
    {
      names.append(QString::fromUtf8(pScenario->m_sName.GetData()));
    }
  }

  m_pBindingsPanel->SetScenarios(names);
}

const plDocumentObject* plQtUiDocumentAssetDocumentWindow::FindScenarioObject(const QString& sName) const
{
  const plString sWanted = sName.toUtf8().data();

  for (const plDocumentObject* pChild : m_pAssetDoc->GetPropertyObject()->GetChildren())
  {
    if (pChild->GetParentProperty() != "MockScenarios")
      continue;

    const plVariant name = pChild->GetTypeAccessor().GetValue("Name");

    if (name.IsA<plString>() && name.Get<plString>() == sWanted)
    {
      return pChild;
    }
  }

  return nullptr;
}

void plQtUiDocumentAssetDocumentWindow::OnScenarioSaveRequested(const QString& sName)
{
  if (m_pBindingsPanel == nullptr)
    return;

  // snapshot the current mock table in the "UiMockBlackboard" newline format
  plDynamicArray<plQtUiBindingsPanel::MockEntry> entries;
  m_pBindingsPanel->GetMockEntries(entries);

  plStringBuilder sData;
  for (const auto& entry : entries)
  {
    if (!sData.IsEmpty())
      sData.Append("\n");

    sData.Append(entry.m_sName);
    sData.Append("\n");
    sData.Append(entry.m_sValue);
  }

  auto pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Save Mock Scenario");

  bool bOk = true;

  if (const plDocumentObject* pExisting = FindScenarioObject(sName))
  {
    bOk = pAccessor->SetValueByName(pExisting, "Data", plString(sData)).Succeeded();
  }
  else
  {
    const plDocumentObject* pPropsObj = m_pAssetDoc->GetPropertyObject();
    plUuid newGuid = plUuid::MakeUuid();

    bOk = pAccessor->AddObjectByName(pPropsObj, "MockScenarios", pAccessor->GetCountByName(pPropsObj, "MockScenarios"), plGetStaticRTTI<plUiMockScenario>(), newGuid).Succeeded();

    if (bOk)
    {
      const plDocumentObject* pNewObj = GetDocument()->GetObjectManager()->GetObject(newGuid);
      bOk = pNewObj != nullptr;
      bOk = bOk && pAccessor->SetValueByName(pNewObj, "Name", plString(sName.toUtf8().data())).Succeeded();
      bOk = bOk && pAccessor->SetValueByName(pNewObj, "Data", plString(sData)).Succeeded();
    }
  }

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }

  UpdateScenariosList();
}

void plQtUiDocumentAssetDocumentWindow::OnScenarioDeleteRequested(const QString& sName)
{
  const plDocumentObject* pScenario = FindScenarioObject(sName);
  if (pScenario == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Delete Mock Scenario");

  if (pAccessor->RemoveObject(pScenario).Succeeded())
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }

  UpdateScenariosList();
}

void plQtUiDocumentAssetDocumentWindow::OnScenarioSelected(const QString& sName)
{
  if (m_pBindingsPanel == nullptr)
    return;

  const plDocumentObject* pScenario = FindScenarioObject(sName);
  if (pScenario == nullptr)
    return;

  const plVariant data = pScenario->GetTypeAccessor().GetValue("Data");
  if (!data.IsA<plString>())
    return;

  // newline-separated name/value pairs (trailing empty value may be dropped by the split)
  plHybridArray<plStringView, 32> parts;
  data.Get<plString>().GetView().Split(true, parts, "\n");

  plDynamicArray<plQtUiBindingsPanel::MockEntry> entries;

  for (plUInt32 i = 0; i + 1 < parts.GetCount(); i += 2)
  {
    plQtUiBindingsPanel::MockEntry& entry = entries.ExpandAndGetRef();
    entry.m_sName = parts[i];
    entry.m_sValue = parts[i + 1];
  }

  if (parts.GetCount() % 2 == 1 && !parts.PeekBack().IsEmpty())
  {
    plQtUiBindingsPanel::MockEntry& entry = entries.ExpandAndGetRef();
    entry.m_sName = parts.PeekBack();
    entry.m_sValue = plString();
  }

  // SetMockEntries emits MockDataChanged when values changed -> live preview push
  m_pBindingsPanel->SetMockEntries(entries);
}

void plQtUiDocumentAssetDocumentWindow::SetEngineConnectedIndicator(bool bConnected)
{
  if (m_pEngineStatusLabel == nullptr)
    return;

  if (bConnected)
  {
    m_pEngineStatusLabel->setStyleSheet(QStringLiteral("color: #12b886;"));
    m_pEngineStatusLabel->setText(QString::fromUtf8("\xE2\x97\x8F ") + plMakeQString(plTranslate("GameUi.Status.EngineConnected")));
  }
  else
  {
    m_pEngineStatusLabel->setStyleSheet(QStringLiteral("color: #fa5252;"));
    m_pEngineStatusLabel->setText(QString::fromUtf8("\xE2\x97\x8F ") + plMakeQString(plTranslate("GameUi.Status.EngineRestarting")));
  }
}

void plQtUiDocumentAssetDocumentWindow::EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e)
{
  if (e.m_Type == plEditorEngineProcessConnection::Event::Type::ProcessCrashed)
  {
    QMetaObject::invokeMethod(this, [this]() { SetEngineConnectedIndicator(false); }, Qt::QueuedConnection);
  }

  if (e.m_Type == plEditorEngineProcessConnection::Event::Type::ProcessRestarted)
  {
    // the engine process lost all session state: re-push everything the preview needs
    QMetaObject::invokeMethod(this, [this]()
      {
        SetEngineConnectedIndicator(true);
        UpdatePreview();
        SendCanvasViewToEngine();
        SendMockBlackboardToEngine();
        SendSelectionToEngine();
        SendLanguageToEngine();
      },
      Qt::QueuedConnection);
  }
}

//////////////////////////////////////////////////////////////////////////
// rect transform panel + status bar

QWidget* plQtUiDocumentAssetDocumentWindow::CreateRectTransformWidget(QWidget* pParent)
{
  m_pRectTransformWidget = new QWidget(pParent);

  QVBoxLayout* pOuterLayout = new QVBoxLayout(m_pRectTransformWidget);
  pOuterLayout->setContentsMargins(4, 4, 4, 4);
  pOuterLayout->setSpacing(4);

  QHBoxLayout* pLayout = new QHBoxLayout();
  pLayout->setContentsMargins(0, 0, 0, 0);
  pLayout->setSpacing(8);
  pOuterLayout->addLayout(pLayout);

  // 4x4 anchor preset grid: left/center/right/stretch × top/middle/bottom/stretch
  {
    QGridLayout* pGrid = new QGridLayout();
    pGrid->setSpacing(1);

    for (plInt8 iRow = 0; iRow < 4; ++iRow)
    {
      for (plInt8 iCol = 0; iCol < 4; ++iCol)
      {
        QToolButton* pButton = new QToolButton(m_pRectTransformWidget);
        pButton->setIcon(QIcon(MakeAnchorPresetPixmap(iCol, iRow)));
        pButton->setIconSize(QSize(20, 20));
        pButton->setAutoRaise(true);
        pButton->setToolTip(plMakeQString(plTranslate("GameUi.AnchorPreset.Tooltip")));

        connect(pButton, &QToolButton::clicked, this, [this, iCol, iRow]() { ApplyAnchorPreset(iCol, iRow); });

        pGrid->addWidget(pButton, iRow, iCol);
      }
    }

    pLayout->addLayout(pGrid);
  }

  // offset fields (labels adapt to point vs stretch anchors per axis)
  {
    QGridLayout* pFields = new QGridLayout();
    pFields->setSpacing(3);

    const char* defaultLabels[4] = {"X", "Y", "W", "H"};

    // display order: X Y / W H (mapping to offsets x y / z w)
    const int displayRow[4] = {0, 1, 0, 1};
    const int displayCol[4] = {0, 0, 1, 1};

    for (plUInt32 i = 0; i < 4; ++i)
    {
      m_pOffsetLabel[i] = new QLabel(defaultLabels[i], m_pRectTransformWidget);
      m_pOffsetLabel[i]->setMinimumWidth(14);

      m_pOffsetSpin[i] = new QDoubleSpinBox(m_pRectTransformWidget);
      m_pOffsetSpin[i]->setRange(-100000.0, 100000.0);
      m_pOffsetSpin[i]->setDecimals(1);
      m_pOffsetSpin[i]->setKeyboardTracking(false);
      m_pOffsetSpin[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);

      connect(m_pOffsetSpin[i], &QDoubleSpinBox::editingFinished, this, [this]() { OnOffsetEditingFinished(); });

      QHBoxLayout* pPair = new QHBoxLayout();
      pPair->setSpacing(2);
      pPair->addWidget(m_pOffsetLabel[i]);
      pPair->addWidget(m_pOffsetSpin[i], 1);

      pFields->addLayout(pPair, displayRow[i], displayCol[i]);
    }

    pLayout->addLayout(pFields, 1);
  }

  // anchor summary ("Anchor: Left-Mid" / custom fractions), mockup's readout line
  m_pAnchorSummaryLabel = new QLabel(m_pRectTransformWidget);
  m_pAnchorSummaryLabel->setStyleSheet(QStringLiteral("color: #8e8e8e; font-size: 10px;"));
  pOuterLayout->addWidget(m_pAnchorSummaryLabel);

  m_pRectTransformWidget->setEnabled(false);

  return m_pRectTransformWidget;
}

void plQtUiDocumentAssetDocumentWindow::UpdateAnchorSummary(const plUiElement* pElement)
{
  if (m_pAnchorSummaryLabel == nullptr)
    return;

  if (pElement == nullptr)
  {
    m_pAnchorSummaryLabel->setText(QString());
    return;
  }

  const plVec2& vMin = pElement->m_vAnchorMin;
  const plVec2& vMax = pElement->m_vAnchorMax;

  auto axisName = [](float fMin, float fMax, const char* szLow, const char* szMid, const char* szHigh) -> const char*
  {
    if (fMin == 0.0f && fMax == 1.0f)
      return "Stretch";
    if (fMin != fMax)
      return nullptr;
    if (fMin == 0.0f)
      return szLow;
    if (fMin == 0.5f)
      return szMid;
    if (fMin == 1.0f)
      return szHigh;
    return nullptr;
  };

  const char* szX = axisName(vMin.x, vMax.x, "Left", "Center", "Right");
  const char* szY = axisName(vMin.y, vMax.y, "Top", "Middle", "Bottom");

  plStringBuilder sText;

  if (szX != nullptr && szY != nullptr)
  {
    sText.SetFormat("Anchor: {}-{}", szX, szY);
  }
  else
  {
    sText.SetFormat("Anchor: custom ({}, {}) - ({}, {})", plArgF(vMin.x, 2), plArgF(vMin.y, 2), plArgF(vMax.x, 2), plArgF(vMax.y, 2));
  }

  m_pAnchorSummaryLabel->setText(sText.GetData());
}

QWidget* plQtUiDocumentAssetDocumentWindow::CreateEffectiveStyleWidget(QWidget* pParent)
{
  m_pEffectiveStyleWidget = new QWidget(pParent);

  QVBoxLayout* pLayout = new QVBoxLayout(m_pEffectiveStyleWidget);
  pLayout->setContentsMargins(4, 2, 4, 2);
  pLayout->setSpacing(2);

  m_pEffectiveStyleTitle = new QLabel(m_pEffectiveStyleWidget);
  m_pEffectiveStyleTitle->setStyleSheet(plUiEditorTheme::szSectionHeader);
  pLayout->addWidget(m_pEffectiveStyleTitle);

  m_pEffectiveStyleTable = new QTableWidget(m_pEffectiveStyleWidget);
  m_pEffectiveStyleTable->setStyleSheet(plUiEditorTheme::szTableStyle);
  m_pEffectiveStyleTable->setColumnCount(3);
  m_pEffectiveStyleTable->setHorizontalHeaderLabels({plMakeQString(plTranslate("GameUi.EffectiveStyle.ColProperty")),
    plMakeQString(plTranslate("GameUi.EffectiveStyle.ColValue")), plMakeQString(plTranslate("GameUi.EffectiveStyle.ColSource"))});
  m_pEffectiveStyleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_pEffectiveStyleTable->verticalHeader()->setVisible(false);
  m_pEffectiveStyleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pEffectiveStyleTable->setSelectionMode(QAbstractItemView::NoSelection);
  m_pEffectiveStyleTable->setFocusPolicy(Qt::NoFocus);
  pLayout->addWidget(m_pEffectiveStyleTable, 1);

  return m_pEffectiveStyleWidget;
}

bool plQtUiDocumentAssetDocumentWindow::LoadCompiledStyle(const plString& sStyleRef, plUiStyle& out_style) const
{
  if (sStyleRef.IsEmpty())
    return false;

  // curator -> transformed output file -> asset header -> descriptor (LoadTemplateEntries pattern)
  plAssetCurator::plLockedSubAsset pSubAsset;

  if (plConversionUtils::IsStringUuid(sStyleRef))
  {
    pSubAsset = plAssetCurator::GetSingleton()->GetSubAsset(plConversionUtils::ConvertStringToUuid(sStyleRef));
  }
  else
  {
    pSubAsset = plAssetCurator::GetSingleton()->FindSubAsset(sStyleRef);
  }

  if (!pSubAsset.isValid())
    return false;

  const plString sOutputFile = pSubAsset->m_pAssetInfo->GetManager()->GetAbsoluteOutputFileName(pSubAsset->m_pAssetInfo->m_pDocumentTypeDescriptor, pSubAsset->m_pAssetInfo->m_Path, "");

  plFileReader file;
  if (file.Open(sOutputFile).Failed())
    return false; // not transformed yet

  plAssetFileHeader assetHeader;
  if (assetHeader.Read(file).Failed())
    return false;

  plUiStyleResourceDescriptor desc;
  if (desc.Load(file).Failed() || desc.m_pStyle == nullptr)
    return false;

  out_style = *desc.m_pStyle;
  return true;
}

void plQtUiDocumentAssetDocumentWindow::UpdateEffectiveStyle()
{
  if (m_pEffectiveStyleWidget == nullptr)
    return;

  HitResult selected;
  const plDocumentObject* pObj = nullptr;

  if (GetSelectedElementRect(selected))
  {
    pObj = GetElementDocObject(selected.m_Path);
  }

  // only VISUAL elements have a look to inspect
  const plUiVisualElement* pVisual = (selected.m_pElement != nullptr) ? plDynamicCast<const plUiVisualElement*>(selected.m_pElement) : nullptr;
  const plUiText* pText = (selected.m_pElement != nullptr) ? plDynamicCast<const plUiText*>(selected.m_pElement) : nullptr;

  if (pObj == nullptr || (pVisual == nullptr && pText == nullptr))
  {
    m_pEffectiveStyleTitle->setText(plMakeQString(plTranslate("GameUi.EffectiveStyle.NoSelection")));
    m_pEffectiveStyleTable->setRowCount(0);
    return;
  }

  plStringBuilder sName = selected.m_pElement->m_sName;
  if (sName.IsEmpty())
    sName = plTranslate(selected.m_pElement->GetDynamicRTTI()->GetTypeName());

  plStringBuilder sTitle;
  sTitle.SetFormat("{} \xC2\xB7 {}", plTranslate("GameUi.EffectiveStyle.Title"), sName);
  m_pEffectiveStyleTitle->setText(sTitle.GetData());

  // ————— resolve the cascade: element locals, replaced wholesale by the style asset —————

  const plVariant styleRef = pObj->GetTypeAccessor().GetValue("Style");
  const plString sStyleRef = styleRef.IsA<plString>() ? styleRef.Get<plString>() : plString();

  plUiStyle style;
  const bool bStyled = !sStyleRef.IsEmpty();
  const bool bStyleLoaded = bStyled && LoadCompiledStyle(sStyleRef, style);
  const plString sStyleName = bStyled ? GetAssetRefDisplayName(sStyleRef) : plString();

  struct Row
  {
    QString m_sProperty;
    QString m_sValue;
    QColor m_Swatch;
    bool m_bSwatch = false;
    QString m_sSource;
    int m_iKind = 0; // 0 element, 1 style, 2 state
  };

  QList<Row> rows;

  const QString sElementSource = plMakeQString(plTranslate("GameUi.EffectiveStyle.SourceElement"));
  const QString sStyleSource = QString("%1: %2").arg(plMakeQString(plTranslate("GameUi.EffectiveStyle.SourceStyle")), QString::fromUtf8(sStyleName.GetData()));

  auto addColor = [&](const char* szName, const plColorGammaUB& color, bool bFromStyle)
  {
    Row& row = rows.emplaceBack();
    row.m_sProperty = plMakeQString(plTranslate(szName));
    row.m_sValue = QString("#%1%2%3 · %4%%")
                     .arg(color.r, 2, 16, QChar('0'))
                     .arg(color.g, 2, 16, QChar('0'))
                     .arg(color.b, 2, 16, QChar('0'))
                     .arg(static_cast<int>(color.a / 2.55f));
    row.m_Swatch = QColor(color.r, color.g, color.b);
    row.m_bSwatch = true;
    row.m_sSource = bFromStyle ? sStyleSource : sElementSource;
    row.m_iKind = bFromStyle ? 1 : 0;
  };

  auto addValue = [&](const char* szName, const QString& sValue, bool bFromStyle)
  {
    Row& row = rows.emplaceBack();
    row.m_sProperty = plMakeQString(plTranslate(szName));
    row.m_sValue = sValue;
    row.m_sSource = bFromStyle ? sStyleSource : sElementSource;
    row.m_iKind = bFromStyle ? 1 : 0;
  };

  if (bStyled && !bStyleLoaded)
  {
    Row& row = rows.emplaceBack();
    row.m_sProperty = plMakeQString(plTranslate("GameUi.EffectiveStyle.NotTransformed"));
    row.m_sSource = sStyleSource;
    row.m_iKind = 1;
  }
  else if (pVisual != nullptr)
  {
    // "style replaces visuals": with a loaded style EVERY value comes from it
    const bool bS = bStyleLoaded;

    addColor("Color", bS ? style.m_Color : pVisual->m_Color, bS);
    addValue("CornerRadius", QString("%1 / %2 / %3 / %4 px").arg(bS ? style.m_vCornerRadius.x : pVisual->m_vCornerRadius.x).arg(bS ? style.m_vCornerRadius.y : pVisual->m_vCornerRadius.y).arg(bS ? style.m_vCornerRadius.z : pVisual->m_vCornerRadius.z).arg(bS ? style.m_vCornerRadius.w : pVisual->m_vCornerRadius.w), bS);

    const float fBorder = bS ? style.m_fBorderWidth : pVisual->m_fBorderWidth;
    if (fBorder > 0.0f)
    {
      addValue("BorderWidth", QString("%1 px").arg(fBorder), bS);
      addColor("BorderColor", bS ? style.m_BorderColor : pVisual->m_BorderColor, bS);
    }

    const float fShadow = bS ? style.m_fShadowSize : pVisual->m_fShadowSize;
    if (fShadow > 0.0f)
    {
      addValue("ShadowSize", QString("%1 px").arg(fShadow), bS);
      addColor("ShadowColor", bS ? style.m_ShadowColor : pVisual->m_ShadowColor, bS);
    }

    if (const plUiButton* pButton = plDynamicCast<const plUiButton*>(pVisual))
    {
      addColor("HoverColor", bS ? style.m_HoverColor : pButton->m_HoverColor, bS);
      addColor("PressedColor", bS ? style.m_PressedColor : pButton->m_PressedColor, bS);

      if (bS)
      {
        addValue("TransitionDuration", QString("%1 s").arg(style.m_fTransitionDuration), bS);
      }
    }

    if (const plUiSlider* pSlider = plDynamicCast<const plUiSlider*>(pVisual))
    {
      addColor("FillColor", bS ? style.m_FillColor : pSlider->m_FillColor, bS);
      addColor("HandleColor", bS ? style.m_HandleColor : pSlider->m_HandleColor, bS);
    }
    else if (const plUiProgressBar* pProgressBar = plDynamicCast<const plUiProgressBar*>(pVisual))
    {
      addColor("FillColor", bS ? style.m_FillColor : pProgressBar->m_FillColor, bS);
    }

    if (const plUiTextInput* pTextInput = plDynamicCast<const plUiTextInput*>(pVisual))
    {
      addColor("TextColor", bS ? style.m_TextColor : pTextInput->m_TextColor, bS);
      addValue("FontSize", QString("%1 px").arg(bS ? style.m_uiFontSize : pTextInput->m_uiFontSize), bS);
    }
  }
  else if (pText != nullptr)
  {
    const bool bS = bStyleLoaded;
    addColor("TextColor", bS ? style.m_TextColor : pText->m_Color, bS);
    addValue("FontSize", QString("%1 px").arg(bS ? style.m_uiFontSize : pText->m_uiFontSize), bS);
  }

  // ————— state overrides (styles cross-faded on top while their condition holds) —————

  for (const plDocumentObject* pStateObj : pObj->GetChildren())
  {
    if (pStateObj->GetParentProperty() != "States")
      continue;

    const plVariant stateStyle = pStateObj->GetTypeAccessor().GetValue("Style");
    if (!stateStyle.IsA<plString>() || stateStyle.Get<plString>().IsEmpty())
      continue;

    const plVariant stateName = pStateObj->GetTypeAccessor().GetValue("Name");
    const plVariant entry = pStateObj->GetTypeAccessor().GetValue("Entry");

    Row& row = rows.emplaceBack();
    row.m_sProperty = QString("%1: %2").arg(plMakeQString(plTranslate("GameUi.EffectiveStyle.SourceState")), stateName.IsA<plString>() ? QString::fromUtf8(stateName.Get<plString>().GetData()) : QString());
    row.m_sValue = QString::fromUtf8(GetAssetRefDisplayName(stateStyle.Get<plString>()).GetData());
    row.m_sSource = entry.IsA<plString>() ? QString::fromUtf8(entry.Get<plString>().GetData()) : QString();
    row.m_iKind = 2;
  }

  // ————— fill the table —————

  m_pEffectiveStyleTable->setRowCount(rows.size());

  const QColor styleColor(0xb9, 0x96, 0xcd);  // lavender: style asset
  const QColor stateColor(0x84, 0x5e, 0xf7);  // violet: state override
  const QColor elementColor(0x8e, 0x8e, 0x8e); // dim: element-local

  for (int i = 0; i < rows.size(); ++i)
  {
    const Row& row = rows[i];

    QTableWidgetItem* pProperty = new QTableWidgetItem(row.m_sProperty);
    QTableWidgetItem* pValue = new QTableWidgetItem(row.m_sValue);
    QTableWidgetItem* pSource = new QTableWidgetItem(row.m_sSource);

    if (row.m_bSwatch)
    {
      QPixmap swatch(12, 12);
      swatch.fill(row.m_Swatch);
      pValue->setIcon(QIcon(swatch));
    }

    pSource->setForeground(QBrush(row.m_iKind == 1 ? styleColor : (row.m_iKind == 2 ? stateColor : elementColor)));

    m_pEffectiveStyleTable->setItem(i, 0, pProperty);
    m_pEffectiveStyleTable->setItem(i, 1, pValue);
    m_pEffectiveStyleTable->setItem(i, 2, pSource);
  }
}

void plQtUiDocumentAssetDocumentWindow::ApplyAnchorPreset(plInt8 iCol, plInt8 iRow)
{
  HitResult selected;
  if (!GetSelectedElementRect(selected))
    return;

  const plDocumentObject* pObj = GetElementDocObject(selected.m_Path);
  if (pObj == nullptr)
    return;

  const plRectFloat& rect = selected.m_Rect;
  const plRectFloat& parent = selected.m_ParentRect;

  plVec2 vAnchorMin, vAnchorMax;
  plVec4 vOffsets;

  // X axis
  if (iCol == 3)
  {
    vAnchorMin.x = 0.0f;
    vAnchorMax.x = 1.0f;
    vOffsets.x = rect.x - parent.x;
    vOffsets.z = (rect.x + rect.width) - (parent.x + parent.width);
  }
  else
  {
    const float fAnchor = iCol * 0.5f;
    vAnchorMin.x = fAnchor;
    vAnchorMax.x = fAnchor;
    vOffsets.x = rect.x - (parent.x + fAnchor * parent.width);
    vOffsets.z = rect.width;
  }

  // Y axis
  if (iRow == 3)
  {
    vAnchorMin.y = 0.0f;
    vAnchorMax.y = 1.0f;
    vOffsets.y = rect.y - parent.y;
    vOffsets.w = (rect.y + rect.height) - (parent.y + parent.height);
  }
  else
  {
    const float fAnchor = iRow * 0.5f;
    vAnchorMin.y = fAnchor;
    vAnchorMax.y = fAnchor;
    vOffsets.y = rect.y - (parent.y + fAnchor * parent.height);
    vOffsets.w = rect.height;
  }

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Set UI Anchors");

  bool bOk = true;
  bOk &= pAccessor->SetValueByName(pObj, "AnchorMin", vAnchorMin).Succeeded();
  bOk &= pAccessor->SetValueByName(pObj, "AnchorMax", vAnchorMax).Succeeded();
  bOk &= pAccessor->SetValueByName(pObj, "Offsets", vOffsets).Succeeded();

  if (bOk)
  {
    pAccessor->FinishTransaction();
  }
  else
  {
    pAccessor->CancelTransaction();
  }
}

void plQtUiDocumentAssetDocumentWindow::OnOffsetEditingFinished()
{
  if (m_bIgnoreRectPanelChanges)
    return;

  HitResult selected;
  if (!GetSelectedElementRect(selected))
    return;

  const plDocumentObject* pObj = GetElementDocObject(selected.m_Path);
  if (pObj == nullptr)
    return;

  const plVec4 vOffsets(
    static_cast<float>(m_pOffsetSpin[0]->value()),
    static_cast<float>(m_pOffsetSpin[1]->value()),
    static_cast<float>(m_pOffsetSpin[2]->value()),
    static_cast<float>(m_pOffsetSpin[3]->value()));

  if (vOffsets == selected.m_pElement->m_vOffsets)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Edit UI Rect");
  if (pAccessor->SetValueByName(pObj, "Offsets", vOffsets).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plQtUiDocumentAssetDocumentWindow::UpdateSelectionUi()
{
  HitResult selected;
  const bool bHasElement = GetSelectedElementRect(selected);

  if (m_pRectTransformWidget != nullptr)
  {
    m_pRectTransformWidget->setEnabled(bHasElement);

    if (bHasElement)
    {
      m_bIgnoreRectPanelChanges = true;

      const plUiElement* pElement = selected.m_pElement;

      const bool bPointX = pElement->m_vAnchorMin.x == pElement->m_vAnchorMax.x;
      const bool bPointY = pElement->m_vAnchorMin.y == pElement->m_vAnchorMax.y;

      m_pOffsetLabel[0]->setText(bPointX ? "X" : "L");
      m_pOffsetLabel[2]->setText(bPointX ? "W" : "R");
      m_pOffsetLabel[1]->setText(bPointY ? "Y" : "T");
      m_pOffsetLabel[3]->setText(bPointY ? "H" : "B");

      m_pOffsetSpin[0]->setValue(pElement->m_vOffsets.x);
      m_pOffsetSpin[1]->setValue(pElement->m_vOffsets.y);
      m_pOffsetSpin[2]->setValue(pElement->m_vOffsets.z);
      m_pOffsetSpin[3]->setValue(pElement->m_vOffsets.w);

      m_bIgnoreRectPanelChanges = false;
    }

    UpdateAnchorSummary(bHasElement ? selected.m_pElement : nullptr);
  }

  UpdateEffectiveStyle();

  if (m_pStatusLabel != nullptr)
  {
    if (bHasElement)
    {
      plStringBuilder sName = selected.m_pElement->m_sName;
      if (sName.IsEmpty())
        sName = "<unnamed>";

      plStringBuilder sStatus;
      sStatus.SetFormat("{} ({}) — rect {}, {} · {} × {} px", sName, plTranslate(selected.m_pElement->GetDynamicRTTI()->GetTypeName()),
        plArgF(selected.m_Rect.x, 0), plArgF(selected.m_Rect.y, 0), plArgF(selected.m_Rect.width, 0), plArgF(selected.m_Rect.height, 0));

      m_pStatusLabel->setText(sStatus.GetData());
    }
    else
    {
      m_pStatusLabel->setText("");
    }
  }
}

void plQtUiDocumentAssetDocumentWindow::RemoveSelectedElements()
{
  plHybridArray<plUuid, 8> toRemove;

  for (const plDocumentObject* pObj : GetDocument()->GetSelectionManager()->GetSelection())
  {
    plHybridArray<plUInt32, 8> path;
    if (ComputeElementPath(pObj, path) && !path.IsEmpty())
    {
      toRemove.PushBack(pObj->GetGuid());
    }
  }

  if (toRemove.IsEmpty())
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Delete UI Element");

  for (const plUuid& guid : toRemove)
  {
    if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid))
    {
      pAccessor->RemoveObject(pObj).IgnoreResult();
    }
  }

  pAccessor->FinishTransaction();
}
