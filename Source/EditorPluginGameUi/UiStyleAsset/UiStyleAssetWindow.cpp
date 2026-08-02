#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Assets/AssetStatusIndicator.moc.h>
#include <EditorFramework/DocumentWindow/EngineViewWidget.moc.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <EditorPluginGameUi/UiStyleAsset/UiStyleAssetWindow.moc.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <GuiFoundation/ActionViews/MenuBarActionMapView.moc.h>
#include <GuiFoundation/ActionViews/ToolBarActionMapView.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

#include <QLayout>
#include <QMouseEvent>

plQtUiStyleAssetDocumentWindow::plQtUiStyleAssetDocumentWindow(plAssetDocument* pDocument)
  : plQtEngineDocumentWindow(pDocument)
{
  m_pAssetDoc = static_cast<plUiStyleAssetDocument*>(pDocument);

  // Menu Bar
  {
    plQtMenuBarActionMapView* pMenuBar = static_cast<plQtMenuBarActionMapView*>(menuBar());
    plActionContext context;
    context.m_sMapping = "UiStyleAssetMenuBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pMenuBar->SetActionContext(context);
  }

  // Tool Bar
  {
    plQtToolBarActionMapView* pToolBar = new plQtToolBarActionMapView("Toolbar", this);
    plActionContext context;
    context.m_sMapping = "UiStyleAssetToolBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pToolBar->SetActionContext(context);
    pToolBar->setObjectName("UiStyleAssetWindowToolBar");
    addToolBar(pToolBar);
  }

  // Central: live engine-rendered showcase
  {
    m_ViewConfig.m_Camera.LookAt(plVec3(-1.6f, 0, 0), plVec3(0, 0, 0), plVec3(0, 0, 1));
    m_ViewConfig.ApplyPerspectiveSetting(90);

    m_pViewWidget = new plQtEngineViewWidget(nullptr, this, &m_ViewConfig);
    AddViewWidget(m_pViewWidget);
    plQtViewWidgetContainer* pContainer = new plQtViewWidgetContainer(this, m_pViewWidget, nullptr);

    plQtDocumentPanel* pCentralPanel = new plQtDocumentPanel(this, pDocument);
    pCentralPanel->setObjectName("UiStyleAssetViewCentral");
    pCentralPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.StylePreview")));
    pCentralPanel->setWidget(pContainer, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->setCentralWidget(pCentralPanel);

    m_pViewWidget->installEventFilter(this);
    m_pViewWidget->setMouseTracking(true); // hover states in the preview
  }

  // Property Grid
  {
    plQtDocumentPanel* pPropertyPanel = new plQtDocumentPanel(this, pDocument);
    pPropertyPanel->setObjectName("UiStyleAssetDockWidget");
    pPropertyPanel->setWindowTitle(plMakeQString(plTranslate("GameUi.Panel.StyleProperties")));
    pPropertyPanel->show();

    plQtPropertyGridWidget* pPropertyGrid = new plQtPropertyGridWidget(pPropertyPanel, pDocument);

    QWidget* pWidget = new QWidget();
    pWidget->setObjectName("Group");
    pWidget->setLayout(new QVBoxLayout());
    pWidget->setContentsMargins(0, 0, 0, 0);

    pWidget->layout()->setContentsMargins(0, 0, 0, 0);
    pWidget->layout()->addWidget(new plQtAssetStatusIndicator((plAssetDocument*)GetDocument()));
    pWidget->layout()->addWidget(pPropertyGrid);

    pPropertyPanel->setWidget(pWidget, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pPropertyPanel);

    pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);
  }

  // live preview: push the style to the engine resource on every edit
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plQtUiStyleAssetDocumentWindow::PropertyEventHandler, this));
  plEditorEngineProcessConnection::s_Events.AddEventHandler(plMakeDelegate(&plQtUiStyleAssetDocumentWindow::EngineProcessEventHandler, this));

  FinishWindowCreation();

  UpdatePreview();
}

plQtUiStyleAssetDocumentWindow::~plQtUiStyleAssetDocumentWindow()
{
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plQtUiStyleAssetDocumentWindow::PropertyEventHandler, this));
  plEditorEngineProcessConnection::s_Events.RemoveEventHandler(plMakeDelegate(&plQtUiStyleAssetDocumentWindow::EngineProcessEventHandler, this));

  RestoreResource();
}

void plQtUiStyleAssetDocumentWindow::InternalRedraw()
{
  plEditorInputContext::UpdateActiveInputContext();
  SendRedrawMsg();
  plQtEngineDocumentWindow::InternalRedraw();
}

void plQtUiStyleAssetDocumentWindow::SendRedrawMsg()
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

void plQtUiStyleAssetDocumentWindow::UpdatePreview()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plResourceUpdateMsgToEngine msg;
  msg.m_sResourceType = "UI Style";

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plContiguousMemoryStreamStorage streamStorage;
  plMemoryStreamWriter memoryWriter(&streamStorage);

  // Write Path
  plStringBuilder sAbsFilePath = GetDocument()->GetDocumentPath();
  sAbsFilePath.ChangeFileExtension("plBinUiStyle");
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

void plQtUiStyleAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = "UI Style";

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}

void plQtUiStyleAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  UpdatePreview();
}

void plQtUiStyleAssetDocumentWindow::EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e)
{
  if (e.m_Type == plEditorEngineProcessConnection::Event::Type::ProcessRestarted)
  {
    UpdatePreview();
  }
}

void plQtUiStyleAssetDocumentWindow::SendMouseState(const QPoint& viewPos, bool bButtonDown, float fWheelTicks /*= 0.0f*/)
{
  const float fWidth = static_cast<float>(m_pViewWidget->width());
  const float fHeight = static_cast<float>(m_pViewWidget->height());

  if (fWidth <= 0.0f || fHeight <= 0.0f)
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiMouse";

  plStringBuilder sValue;
  sValue.SetFormat("{};{};{};{}", plMath::Clamp(viewPos.x() / fWidth, 0.0f, 1.0f), plMath::Clamp(viewPos.y() / fHeight, 0.0f, 1.0f), bButtonDown ? 1 : 0, fWheelTicks);
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

bool plQtUiStyleAssetDocumentWindow::eventFilter(QObject* pObject, QEvent* pEvent)
{
  if (pObject == m_pViewWidget)
  {
    // the showcase is always interactive: forward pointer input (no camera controls here)
    if (pEvent->type() == QEvent::MouseButtonPress || pEvent->type() == QEvent::MouseButtonRelease || pEvent->type() == QEvent::MouseMove)
    {
      QMouseEvent* pMouse = static_cast<QMouseEvent*>(pEvent);
      SendMouseState(pMouse->position().toPoint(), pMouse->buttons().testFlag(Qt::LeftButton));
      return true;
    }

    if (pEvent->type() == QEvent::Wheel)
    {
      QWheelEvent* pWheel = static_cast<QWheelEvent*>(pEvent);
      SendMouseState(pWheel->position().toPoint(), pWheel->buttons().testFlag(Qt::LeftButton), static_cast<float>(pWheel->angleDelta().y()) / 120.0f);
      return true;
    }
  }

  return plQtEngineDocumentWindow::eventFilter(pObject, pEvent);
}
