#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/DocumentWindow/EngineViewWidget.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <EditorPluginGameUi/UiStyleAsset/UiStyleAssetWindow.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

plUiStyleAssetDocumentWindow::plUiStyleAssetDocumentWindow(plAssetDocument* pDocument)
  : plEngineDocumentWindow(pDocument)
{
  m_pAssetDoc = static_cast<plUiStyleAssetDocument*>(pDocument);

  SetMenuBarActions("UiStyleAssetMenuBar");
  SetToolBarActions("UiStyleAssetToolBar");

  // the live engine-rendered showcase
  {
    m_ViewConfig.m_Camera.LookAt(plVec3(-1.6f, 0, 0), plVec3(0, 0, 0), plVec3(0, 0, 1));
    m_ViewConfig.ApplyPerspectiveSetting(90);

    m_pViewWidget = new plEngineViewWidget(this, &m_ViewConfig);
    AddViewWidget(m_pViewWidget);
    m_pViewWidget->m_InputEvents.AddEventHandler(plMakeDelegate(&plUiStyleAssetDocumentWindow::OnViewInput, this));
  }

  // the shell's properties panel follows the selection
  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);

  // live preview: push the style to the engine resource on every edit
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plUiStyleAssetDocumentWindow::PropertyEventHandler, this));
  plEditorEngineProcessConnection::s_Events.AddEventHandler(plMakeDelegate(&plUiStyleAssetDocumentWindow::EngineProcessEventHandler, this));

  UpdatePreview();
}

plUiStyleAssetDocumentWindow::~plUiStyleAssetDocumentWindow()
{
  m_pViewWidget->m_InputEvents.RemoveEventHandler(plMakeDelegate(&plUiStyleAssetDocumentWindow::OnViewInput, this));
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plUiStyleAssetDocumentWindow::PropertyEventHandler, this));
  plEditorEngineProcessConnection::s_Events.RemoveEventHandler(plMakeDelegate(&plUiStyleAssetDocumentWindow::EngineProcessEventHandler, this));

  RestoreResource();
}

void plUiStyleAssetDocumentWindow::InternalRedraw()
{
  plEditorInputContext::UpdateActiveInputContext();
  SendRedrawMsg();
  plEngineDocumentWindow::InternalRedraw();
}

void plUiStyleAssetDocumentWindow::SendRedrawMsg()
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

void plUiStyleAssetDocumentWindow::UpdatePreview()
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

void plUiStyleAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = "UI Style";

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}

void plUiStyleAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  UpdatePreview();
}

void plUiStyleAssetDocumentWindow::EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e)
{
  if (e.m_Type == plEditorEngineProcessConnection::Event::Type::ProcessRestarted)
  {
    UpdatePreview();
  }
}

void plUiStyleAssetDocumentWindow::SendMouseState(const plVec2& vViewPos, bool bButtonDown, float fWheelTicks /*= 0.0f*/)
{
  const plVec2 vSize = m_pViewWidget->GetViewportSize();

  if (vSize.x <= 0.0f || vSize.y <= 0.0f)
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiMouse";

  plStringBuilder sValue;
  sValue.SetFormat("{};{};{};{}", plMath::Clamp(vViewPos.x / vSize.x, 0.0f, 1.0f), plMath::Clamp(vViewPos.y / vSize.y, 0.0f, 1.0f), bButtonDown ? 1 : 0, fWheelTicks);
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plUiStyleAssetDocumentWindow::OnViewInput(const plEditorInputEvent& e)
{
  // the showcase is always interactive: forward pointer input (no camera controls here)
  const bool bLeftHeld = e.IsHeld(plEditorInputEvent::LeftButton);

  switch (e.m_Type)
  {
    case plEditorInputEvent::Type::MousePress:
    case plEditorInputEvent::Type::MouseRelease:
    case plEditorInputEvent::Type::MouseMove:
      SendMouseState(e.m_vPosition, bLeftHeld);
      break;

    case plEditorInputEvent::Type::Wheel:
      SendMouseState(e.m_vPosition, bLeftHeld, e.m_fWheel);
      break;

    default:
      break;
  }
}
