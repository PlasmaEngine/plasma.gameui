#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/DocumentWindow/EngineViewWidget.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiAddBindingDialog.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiAnimationEngineDlgs.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiBindingsView.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetWindow.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiHierarchyView.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiPaletteView.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiStatesView.h>
#include <EditorUi/Widgets.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameEngine/Utils/BlackboardTemplateResource.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>
#include <GameUiPlugin/Elements/UiLayout.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <GuiFoundation/EngineUi/EngineUiRenderer.h>
#include <ToolsFoundation/Project/ToolsProject.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  // must match the preview component's default canvas scaling (ScaleWithHeight, 1080)
  constexpr float s_fReferenceHeight = 1080.0f;

  /// The canvas view. The engine shell hands drags over the view to HandleDrag: a widget type dragged from the palette
  /// is created where it is dropped.
  class plUiCanvasViewWidget : public plEngineViewWidget
  {
  public:
    using plEngineViewWidget::plEngineViewWidget;

    std::function<void(const plRTTI*, const plVec2I32&)> m_OnPaletteDrop;

    bool HandleDrag(plEditorUiDropEvent::Type type, const plVec2& vPosition, const plEditorUiDragPayload* pPayload, bool bShift, bool bCtrl) override
    {
      if (pPayload == nullptr || !pPayload->HasFormat(plUiPaletteView::s_szMimeType))
        return plEngineViewWidget::HandleDrag(type, vPosition, pPayload, bShift, bCtrl);

      if (type == plEditorUiDropEvent::Type::Drop && m_OnPaletteDrop)
      {
        if (const plRTTI* pType = plRTTI::FindTypeByName(pPayload->GetText(plUiPaletteView::s_szMimeType)))
          m_OnPaletteDrop(pType, plVec2I32(plMath::RoundToInt(vPosition.x), plMath::RoundToInt(vPosition.y)));
      }

      return type != plEditorUiDropEvent::Type::Leave;
    }
  };

  constexpr float s_fResizeHandleZonePx = 8.0f;

  /// The canvas toolbar's device presets; the first (0 x 0) fills the viewport.
  struct DevicePreset
  {
    const char* m_szLabel;
    plUInt32 m_uiWidth;
    plUInt32 m_uiHeight;
  };

  const DevicePreset s_DevicePresets[] = {
    {"GameUi.Toolbar.FillViewport", 0, 0},
    {"1920 x 1080", 1920, 1080},
    {"2560 x 1440", 2560, 1440},
    {"3840 x 2160", 3840, 2160},
    {"1280 x 800 (Deck)", 1280, 800},
    {"1080 x 2340 (Phone)", 1080, 2340},
  };

  /// The canvas toolbar's zoom presets; 0 fits the device into the view.
  const float s_ZoomPresets[] = {0.0f, 0.5f, 1.0f, 2.0f};

} // namespace

plUiDocumentAssetDocumentWindow::plUiDocumentAssetDocumentWindow(plAssetDocument* pDocument)
  : plEngineDocumentWindow(pDocument)
{
  m_pAssetDoc = static_cast<plUiDocumentAssetDocument*>(pDocument);

  SetMenuBarActions("UiDocumentAssetMenuBar");
  SetToolBarActions("UiDocumentAssetToolBar");

  CreateCanvasView();
  CreateCanvasChrome();

  CollectLanguages();
  UpdateLayoutContext();
  CreateCanvasToolBar();

  // the panels; plUiDocumentShellPanels arranges them around the view and the properties
  CreatePalettePanel();
  CreateHierarchyPanel();
  CreateRectTransformPanel();
  m_pEffectiveStyleView = AddPanel<plUiEffectiveStyleView>({"Style", plTranslate("GameUi.Panel.Style")});
  CreateTextFitPanels();
  CreateBindingsPanel();
  CreateStatesPanel();
  CreateAnimationsPanel();

  // the shell's properties panel follows the selection
  pDocument->GetSelectionManager()->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);

  SetEngineConnectedIndicator(true);

  // live preview: push the widget tree to the engine resource on every edit
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::PropertyEventHandler, this));
  m_pAssetDoc->GetObjectManager()->m_StructureEvents.AddEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::StructureEventHandler, this));
  m_pAssetDoc->GetSelectionManager()->m_Events.AddEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::SelectionEventHandler, this));
  plEditorEngineProcessConnection::s_Events.AddEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::EngineProcessEventHandler, this));

  UpdatePreview();
  RebuildHierarchy();
  UpdateBindingsList();
  UpdateAnimationsList();
  UpdateStatesPanel();
}

plUiDocumentAssetDocumentWindow::~plUiDocumentAssetDocumentWindow()
{
  m_pAssetDoc->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::PropertyEventHandler, this));
  m_pAssetDoc->GetObjectManager()->m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::StructureEventHandler, this));
  m_pAssetDoc->GetSelectionManager()->m_Events.RemoveEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::SelectionEventHandler, this));
  if (m_pViewWidget != nullptr)
    m_pViewWidget->m_InputEvents.RemoveEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::OnViewInput, this));
  plEditorEngineProcessConnection::s_Events.RemoveEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::EngineProcessEventHandler, this));

  // the panels' contents use the window's members (the bindings and states views, the canvas transform)
  DestroyPanels();

  RestoreResource();
}

void plUiDocumentAssetDocumentWindow::CreateCanvasView()
{
  SetTargetFramerate(25);

  m_ViewConfig.m_Camera.LookAt(plVec3(-1.6f, 0, 0), plVec3(0, 0, 0), plVec3(0, 0, 1));
  m_ViewConfig.ApplyPerspectiveSetting(90);

  auto* pCanvasView = new plUiCanvasViewWidget(this, &m_ViewConfig);
  pCanvasView->m_OnPaletteDrop = [this](const plRTTI* pType, const plVec2I32& viewPos)
  { AddElementAt(pType, viewPos); };

  m_pViewWidget = pCanvasView;
  AddViewWidget(m_pViewWidget);
  m_pViewWidget->m_InputEvents.AddEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentWindow::OnViewInput, this));
}

void plUiDocumentAssetDocumentWindow::CreateCanvasChrome()
{
  // the contents' surfaces need the UI renderer
  plEngineUiRenderer::EnsureRenderer();

  // the rulers follow the canvas transform, in logical units of the view; the horizontal ruler draws the corner square
  // over the vertical one
  m_pHRulerView = PL_DEFAULT_NEW(plUiRulerView, true, plUiRulerView::Thickness);
  m_pHRulerView->m_GetTransform = [this](float& fScale, float& fOrigin)
  {
    float fS = 1.0f;
    plVec2 vOffset, vCanvasSize;
    GetCanvasTransform(fS, vOffset, vCanvasSize);
    const float fDpr = m_pViewWidget->GetViewportPixelRatio();
    fScale = fS / fDpr;
    fOrigin = vOffset.x / fDpr;
  };

  m_pVRulerView = PL_DEFAULT_NEW(plUiRulerView, false);
  m_pVRulerView->m_GetTransform = [this](float& fScale, float& fOrigin)
  {
    float fS = 1.0f;
    plVec2 vOffset, vCanvasSize;
    GetCanvasTransform(fS, vOffset, vCanvasSize);
    const float fDpr = m_pViewWidget->GetViewportPixelRatio();
    fScale = fS / fDpr;
    fOrigin = vOffset.y / fDpr;
  };

  m_pCanvasStatusRow = PL_DEFAULT_NEW(plUiCanvasStatusRow);
}

void plUiDocumentAssetDocumentWindow::InternalRedraw()
{
  plEditorInputContext::UpdateActiveInputContext();
  SendRedrawMsg();
  plEngineDocumentWindow::InternalRedraw();
}

void plUiDocumentAssetDocumentWindow::SendRedrawMsg()
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

void plUiDocumentAssetDocumentWindow::UpdatePreview()
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

void plUiDocumentAssetDocumentWindow::RestoreResource()
{
  plRestoreResourceMsgToEngine msg;
  msg.m_sResourceType = "UI Document";

  plStringBuilder tmp;
  msg.m_sResourceID = plConversionUtils::ToString(GetDocument()->GetGuid(), tmp);

  plEditorEngineProcessConnection::GetSingleton()->SendMessage(&msg);
}

void plUiDocumentAssetDocumentWindow::PropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  UpdatePreview();
  RebuildHierarchy(); // names / title-relevant properties may have changed
  UpdateSelectionUi();
  UpdateBindingsList();
  UpdateAnimationsList();
  UpdateStatesPanel();
}

void plUiDocumentAssetDocumentWindow::StructureEventHandler(const plDocumentObjectStructureEvent& e)
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

void plUiDocumentAssetDocumentWindow::SelectionEventHandler(const plSelectionManagerEvent& e)
{
  SendSelectionToEngine();
  SyncHierarchySelection();
  UpdateSelectionUi();
  UpdateStatesPanel();
}

void plUiDocumentAssetDocumentWindow::SyncHierarchySelection()
{
  if (m_pHierarchyView == nullptr || m_bIgnoreHierarchySelection)
    return;

  plHybridArray<plUuid, 16> selection;
  for (const plDocumentObject* pObj : GetDocument()->GetSelectionManager()->GetSelection())
    selection.PushBack(pObj->GetGuid());

  m_pHierarchyView->SetSelection(selection);
}

//////////////////////////////////////////////////////////////////////////
// selection → engine gizmos

const plDocumentObject* plUiDocumentAssetDocumentWindow::GetPropertiesObject() const
{
  return GetDocument()->GetObjectManager()->GetRootObject()->GetChildren()[0];
}

bool plUiDocumentAssetDocumentWindow::ComputeElementPath(const plDocumentObject* pObject, plHybridArray<plUInt32, 8>& out_path) const
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

const plDocumentObject* plUiDocumentAssetDocumentWindow::GetElementDocObject(plArrayPtr<const plUInt32> path) const
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

void plUiDocumentAssetDocumentWindow::SendSelectionToEngine()
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

void plUiDocumentAssetDocumentWindow::GetCanvasTransform(float& out_fScale, plVec2& out_vOffset, plVec2& out_vCanvasSize) const
{
  const float fDpr = m_pViewWidget->GetViewportPixelRatio();
  const plVec2 vViewportSize = m_pViewWidget->GetViewportSize() * fDpr;

  // mirrors plUiCanvasComponent::ComputeCanvasTransform for the preview component's settings
  if (m_vDeviceResolution.x > 0 && m_vDeviceResolution.y > 0)
  {
    out_vCanvasSize.Set(static_cast<float>(m_vDeviceResolution.x), static_cast<float>(m_vDeviceResolution.y));

    const float fFitScale = plMath::Min(vViewportSize.x / out_vCanvasSize.x, vViewportSize.y / out_vCanvasSize.y);
    out_fScale = m_fZoom > 0.0f ? m_fZoom : fFitScale;

    out_vOffset = (vViewportSize - out_vCanvasSize * out_fScale) * 0.5f + m_vPan;
    return;
  }

  // laid out at the viewport's size; drawn zoomed and panned when the preview asks for it
  const float fLayoutScale = plMath::Max(vViewportSize.y / s_fReferenceHeight, 0.0001f);
  out_vCanvasSize = vViewportSize / fLayoutScale;
  out_fScale = m_fZoom > 0.0f ? m_fZoom : fLayoutScale;
  out_vOffset = (vViewportSize - out_vCanvasSize * out_fScale) * 0.5f + m_vPan;
}

float plUiDocumentAssetDocumentWindow::GetCanvasScale() const
{
  float fScale = 1.0f;
  plVec2 vOffset, vCanvasSize;
  GetCanvasTransform(fScale, vOffset, vCanvasSize);
  return fScale;
}

plRectFloat plUiDocumentAssetDocumentWindow::GetCanvasRect() const
{
  float fScale = 1.0f;
  plVec2 vOffset, vCanvasSize;
  GetCanvasTransform(fScale, vOffset, vCanvasSize);
  return plRectFloat(0, 0, vCanvasSize.x, vCanvasSize.y);
}

plVec2 plUiDocumentAssetDocumentWindow::WidgetToCanvasPos(const plVec2I32& viewPos) const
{
  const float fDpr = m_pViewWidget->GetViewportPixelRatio();

  float fScale = 1.0f;
  plVec2 vOffset, vCanvasSize;
  GetCanvasTransform(fScale, vOffset, vCanvasSize);

  return plVec2((viewPos.x * fDpr - vOffset.x) / fScale, (viewPos.y * fDpr - vOffset.y) / fScale);
}

void plUiDocumentAssetDocumentWindow::HitTestRecursive(const plUiElement* pElement, const plRectFloat& rect, const plRectFloat& parentRect, const plVec2& vPos, plHybridArray<plUInt32, 8>& ref_path, HitResult& ref_result) const
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
  plUiLayout::ComputeChildRects(pElement, rect, childRects, &m_LayoutContext);

  for (plUInt32 i = 0; i < pElement->m_Children.GetCount(); ++i)
  {
    if (pElement->m_Children[i] == nullptr)
      continue;

    ref_path.PushBack(i);
    HitTestRecursive(pElement->m_Children[i], childRects[i], rect, vPos, ref_path, ref_result);
    ref_path.PopBack();
  }
}

bool plUiDocumentAssetDocumentWindow::HitTest(const plVec2& vRefPos, HitResult& out_result) const
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
    HitTestRecursive(pProps->m_Children[i], pProps->m_Children[i]->ComputeRect(canvasRect, &m_LayoutContext), canvasRect, vRefPos, path, out_result);
    path.PopBack();
  }

  return out_result.m_pElement != nullptr;
}

bool plUiDocumentAssetDocumentWindow::GetSelectedElementRect(HitResult& out_result) const
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
    rect = plUiLayout::ComputeChildRect(pParentElement, pChild, parentRect, uiIndex, &m_LayoutContext);
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

void plUiDocumentAssetDocumentWindow::OnViewInput(const plEditorInputEvent& e)
{
  using InputType = plEditorInputEvent::Type;

  CanvasInput input;
  input.m_Position = e.GetPosition();
  input.m_bLeftHeld = e.IsHeld(plEditorInputEvent::LeftButton);

  switch (e.m_Type)
  {
    case InputType::MousePress:
      input.m_Type = CanvasInput::Type::Press;
      input.m_uiButton = e.m_uiButton;
      break;

    case InputType::MouseRelease:
      input.m_Type = CanvasInput::Type::Release;
      input.m_uiButton = e.m_uiButton;
      break;

    case InputType::MouseMove:
      input.m_Type = CanvasInput::Type::Move;
      break;

    case InputType::Wheel:
      input.m_Type = CanvasInput::Type::Wheel;
      input.m_fWheelTicks = e.m_fWheel;
      break;

    case InputType::KeyPress:
      // typed text arrives as Character events
      input.m_Type = CanvasInput::Type::Key;
      input.m_Key = e.m_Key;
      break;

    case InputType::Character:
      // typed text for a focused Text Input of the interact preview
      input.m_Type = CanvasInput::Type::Key;
      input.m_uiCharacter = e.m_uiCharacter;
      break;

    default:
      return;
  }

  HandleCanvasInput(input);
}

bool plUiDocumentAssetDocumentWindow::HandleCanvasInput(const CanvasInput& input)
{
  using Type = CanvasInput::Type;
  using Key = plEditorPlatformKey;

  // right- or middle-mouse panning
  if (input.m_Type == Type::Press && (input.m_uiButton == plEditorInputEvent::RightButton || input.m_uiButton == plEditorInputEvent::MiddleButton) && !m_bPanning)
  {
    m_bPanning = true;
    m_uiPanButton = input.m_uiButton;
    m_PanStartPos = input.m_Position;
    m_vPanStartValue = m_vPan;
    return true;
  }

  if (input.m_Type == Type::Move && m_bPanning)
  {
    const plVec2I32 pos = input.m_Position;
    const float fDpr = m_pViewWidget->GetViewportPixelRatio();
    m_vPan = m_vPanStartValue + plVec2((pos.x - m_PanStartPos.x) * fDpr, (pos.y - m_PanStartPos.y) * fDpr);
    RefreshCanvasView();
    return true;
  }

  if (input.m_Type == Type::Release && input.m_uiButton == m_uiPanButton && m_bPanning)
  {
    m_bPanning = false;
    m_uiPanButton = plEditorInputEvent::NoButton;
    return true;
  }

  // Delete removes the selected elements (works in both edit and interact mode)
  if (input.m_Type == Type::Key && input.m_Key == Key::Delete)
  {
    RemoveSelectedElements();
    return true;
  }

  // mouse-wheel zoom (device-preset canvas), anchored on the cursor
  if (input.m_Type == Type::Wheel && !m_bInteractMode)
  {
    const float fTicks = input.m_fWheelTicks;

    float fScale = 1.0f;
    plVec2 vOffset = plVec2::MakeZero();
    plVec2 vCanvasSize = plVec2::MakeZero();
    GetCanvasTransform(fScale, vOffset, vCanvasSize);

    const float fNewZoom = plMath::Clamp(fScale * plMath::Pow(1.15f, fTicks), 0.1f, 8.0f);

    // keep the canvas point under the cursor fixed
    const plVec2 vCursor(static_cast<float>(input.m_Position.x), static_cast<float>(input.m_Position.y));
    const plVec2 vCanvasPoint((vCursor.x - vOffset.x) / fScale, (vCursor.y - vOffset.y) / fScale);
    const plVec2 vViewport = m_pViewWidget->GetViewportSize();

    const plVec2 vNewOffset = vCursor - vCanvasPoint * fNewZoom;

    m_fZoom = fNewZoom;
    m_vPan = vNewOffset - (vViewport - vCanvasSize * fNewZoom) * 0.5f;

    // the toolbar shows the custom zoom as an extra entry
    SyncCanvasToolBar();
    RefreshCanvasView();
    return true;
  }

  // interact mode: forward input to the preview instead of editing
  if (m_bInteractMode)
  {
    // keyboard stands in for the gamepad: arrows/Enter/Escape drive the focus navigation,
    // editing keys and typed characters feed a focused Text Input
    if (input.m_Type == Type::Key && !plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    {
      plInt32 iNavAction = -1;
      plInt32 iEditKey = -1;

      switch (input.m_Key)
      {
        case Key::Up:
          iNavAction = plUiNavAction::Up;
          break;
        case Key::Down:
          iNavAction = plUiNavAction::Down;
          break;
        case Key::Left:
          iNavAction = plUiNavAction::Left;
          break;
        case Key::Right:
          iNavAction = plUiNavAction::Right;
          break;
        case Key::Enter:
          iNavAction = plUiNavAction::Accept;
          break;
        case Key::Escape:
          iNavAction = plUiNavAction::Cancel;
          break;
        case Key::Backspace:
          iEditKey = plUiEditKey::Backspace;
          break;
        case Key::Home:
          iEditKey = plUiEditKey::Home;
          break;
        case Key::End:
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
      if (input.m_uiCharacter >= 32)
      {
        plDocumentConfigMsgToEngine msg;
        msg.m_sWhatToDo = "UiChar";
        msg.m_iValue = static_cast<plInt32>(input.m_uiCharacter);
        GetEditorEngineConnection()->SendMessage(&msg);
        return true;
      }
    }

    if (input.m_Type == Type::Press || input.m_Type == Type::Release || input.m_Type == Type::Move)
    {
      SendInteractMouseState(input.m_Position, input.m_bLeftHeld);
      return true;
    }

    if (input.m_Type == Type::Wheel)
    {
      // forward wheel ticks so scroll views scroll in the preview
      SendInteractMouseState(input.m_Position, input.m_bLeftHeld, input.m_fWheelTicks);
      return true;
    }

    return false;
  }

  // editing: select, move and resize elements, or drag anchors
  if (input.m_Type == Type::Press && input.m_uiButton == plEditorInputEvent::LeftButton)
  {
    BeginDrag(input.m_Position);
    return true;
  }

  if (input.m_Type == Type::Move && m_DragMode != DragMode::None)
  {
    UpdateDrag(input.m_Position);
    return true;
  }

  if (input.m_Type == Type::Release && input.m_uiButton == plEditorInputEvent::LeftButton && m_DragMode != DragMode::None)
  {
    EndDrag(true);
    return true;
  }

  if (input.m_Type == Type::Key && input.m_Key == Key::Escape && m_DragMode != DragMode::None)
  {
    EndDrag(false);
    return true;
  }

  return false;
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

bool plUiDocumentAssetDocumentWindow::IsAnchorsMode() const
{
  return m_bAnchorsMode;
}

void plUiDocumentAssetDocumentWindow::BeginDrag(const plVec2I32& viewPos)
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

void plUiDocumentAssetDocumentWindow::BeginAnchorDrag(const plVec2I32& viewPos)
{
  const float fScale = GetCanvasScale();
  const plVec2 vRefPos = WidgetToCanvasPos(viewPos);

  HitResult selected;

  if (GetSelectedElementRect(selected) && !selected.m_pElement->m_bEditorLocked)
  {
    if (IsContainerParent(m_pAssetDoc->GetProperties(), selected.m_Path))
    {
      SetCanvasStatusMessage(plTranslate("GameUi.Status.AnchorsInContainer"));
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

void plUiDocumentAssetDocumentWindow::UpdateDrag(const plVec2I32& viewPos)
{
  if (!m_bDragTransactionOpen)
    return;

  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(m_DragObjectGuid);
  if (pObj == nullptr)
    return;

  const float fDpr = m_pViewWidget->GetViewportPixelRatio();
  const float fScale = GetCanvasScale();

  float fDx = (viewPos.x - m_DragStartPos.x) * fDpr / fScale;
  float fDy = (viewPos.y - m_DragStartPos.y) * fDpr / fScale;

  const bool bSnap = m_bSnap;

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

void plUiDocumentAssetDocumentWindow::EndDrag(bool bApply)
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

void plUiDocumentAssetDocumentWindow::CollectSnapRects(const plHybridArray<plUInt32, 8>& excludePath)
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
    plUiLayout::ComputeChildRects(pElement, rect, childRects, &m_LayoutContext);

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
    collect(pProps->m_Children[i], pProps->m_Children[i]->ComputeRect(canvasRect, &m_LayoutContext));
    path.PopBack();
  }
}

void plUiDocumentAssetDocumentWindow::SnapDragDeltas(float& ref_fDx, float& ref_fDy, plHybridArray<SnapGuide, 4>& out_guides) const
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

void plUiDocumentAssetDocumentWindow::SendGuidesToEngine(plArrayPtr<const SnapGuide> guides)
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

void plUiDocumentAssetDocumentWindow::CreatePalettePanel()
{
  m_pPaletteView = AddPanel<plUiPaletteView>({"Palette", plTranslate("GameUi.Panel.Palette")});
  m_pPaletteView->m_OnAdd = [this](const plRTTI* pType)
  { AddElement(pType); };
}

void plUiDocumentAssetDocumentWindow::CreateHierarchyPanel()
{
  m_pHierarchyView = AddPanel<plUiHierarchyView>({"Hierarchy", plTranslate("GameUi.Panel.Hierarchy")}, GetDocument());
  m_pHierarchyView->m_OnSelectionChanged = [this](plArrayPtr<const plUuid> elements)
  { SelectElements(elements); };
  m_pHierarchyView->m_OnToggle = [this](const plUuid& guid, bool bLock)
  { ToggleElementFlag(GetDocument()->GetObjectManager()->GetObject(guid), bLock); };
  m_pHierarchyView->m_OnDelete = [this]()
  { RemoveSelectedElements(); };
  m_pHierarchyView->m_OnAdd = [this](const plRTTI* pType)
  { AddElement(pType); };
  m_pHierarchyView->m_OnMove = [this](plArrayPtr<const plUuid> elements, const plUuid& parentGuid, plInt32 iIndex)
  {
    const plDocumentObject* pNewParent = parentGuid.IsValid() ? GetDocument()->GetObjectManager()->GetObject(parentGuid) : GetPropertiesObject();
    if (pNewParent == nullptr)
      return;

    plHybridArray<const plDocumentObject*, 8> objects;
    for (const plUuid& guid : elements)
    {
      if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid))
        objects.PushBack(pObj);
    }

    MoveElements(objects, pNewParent, iIndex);
  };
  m_pHierarchyView->m_OnMessage = [this](plStringView sMessage)
  { SetCanvasStatusMessage(sMessage); };
}

void plUiDocumentAssetDocumentWindow::RebuildHierarchy()
{
  if (m_pHierarchyView == nullptr)
    return;

  m_pHierarchyView->Refresh();
  SyncHierarchySelection();
}

void plUiDocumentAssetDocumentWindow::SelectElements(plArrayPtr<const plUuid> elements)
{
  m_bIgnoreHierarchySelection = true;

  plDeque<const plDocumentObject*> newSelection;
  for (const plUuid& guid : elements)
  {
    if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(guid))
      newSelection.PushBack(pObj);
  }

  GetDocument()->GetSelectionManager()->SetSelection(newSelection);

  m_bIgnoreHierarchySelection = false;

  SendSelectionToEngine();
}

void plUiDocumentAssetDocumentWindow::ToggleElementFlag(const plDocumentObject* pObj, bool bLock)
{
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  if (!bLock)
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

void plUiDocumentAssetDocumentWindow::AddElementAt(const plRTTI* pType, const plVec2I32& viewPos)
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

void plUiDocumentAssetDocumentWindow::AddElement(const plRTTI* pType)
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

void plUiDocumentAssetDocumentWindow::MoveElements(plArrayPtr<const plDocumentObject* const> objects, const plDocumentObject* pNewParent, int iIndex)
{
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

void plUiDocumentAssetDocumentWindow::CreateCanvasToolBar()
{
  // its own row under the document tool bar
  m_pCanvasToolBarView = AddRow<plUiCanvasToolBarView>({"GameUi.CanvasToolBar", "Canvas"}, plUiCanvasToolBarView::Height);
  m_pCanvasToolBarView->m_OnChanged = [this](plUiCanvasToolBarView::Control control, plInt32 iValue)
  { OnCanvasToolBarChanged(control, iValue); };

  SyncCanvasToolBar();
}

void plUiDocumentAssetDocumentWindow::OnCanvasToolBarChanged(plUiCanvasToolBarView::Control control, plInt32 iValue)
{
  using Control = plUiCanvasToolBarView::Control;

  switch (control)
  {
    case Control::Device:
      SetDevicePreset(iValue);
      break;

    case Control::Zoom:
      SetZoomPreset(iValue);
      break;

    case Control::Language:
      // the strip: the languages, then the pseudo-localization; -1 = the authored texts
      m_bPseudoLocalization = iValue == static_cast<plInt32>(m_Languages.GetCount());
      m_sLanguage = (iValue >= 0 && iValue < static_cast<plInt32>(m_Languages.GetCount())) ? m_Languages[iValue] : plString();
      UpdateLayoutContext();
      SendLanguageToEngine();
      break;

    case Control::FitAll:
      FitAllButtons();
      break;

    case Control::HighlightUntranslated:
      m_bHighlightUntranslated = iValue != 0;
      SendLanguageToEngine();
      break;

    case Control::SelectMode:
    case Control::AnchorsMode:
      m_bAnchorsMode = control == Control::AnchorsMode;
      break;

    case Control::Interact:
      SetInteractMode(iValue != 0);
      break;

    case Control::Snap:
      m_bSnap = iValue != 0;
      break;

    case Control::SafeArea:
      m_bSafeArea = iValue != 0;
      RefreshCanvasView();
      break;

    case Control::NavGraph:
      m_bNavGraph = iValue != 0;
      RefreshCanvasView();
      break;

    case Control::Checker:
      m_bCheckerboard = iValue != 0;
      RefreshCanvasView();
      break;

    default:
      break;
  }

  SyncCanvasToolBar();
}

void plUiDocumentAssetDocumentWindow::SetDevicePreset(plInt32 iPreset)
{
  if (iPreset < 0 || iPreset >= static_cast<plInt32>(PL_ARRAY_SIZE(s_DevicePresets)))
    return;

  m_iDevicePreset = iPreset;
  m_vDeviceResolution.Set(s_DevicePresets[iPreset].m_uiWidth, s_DevicePresets[iPreset].m_uiHeight);
  m_vPan.SetZero();
  RefreshCanvasView();
}

void plUiDocumentAssetDocumentWindow::SetZoomPreset(plInt32 iPreset)
{
  // the entry after the presets is the zoom the mouse wheel set; it stays as it is
  if (iPreset >= 0 && iPreset < static_cast<plInt32>(PL_ARRAY_SIZE(s_ZoomPresets)))
    m_fZoom = s_ZoomPresets[iPreset];

  m_vPan.SetZero();
  RefreshCanvasView();
}

void plUiDocumentAssetDocumentWindow::SyncCanvasToolBar()
{
  if (m_pCanvasToolBarView == nullptr)
    return;

  using Control = plUiCanvasToolBarView::Control;

  plHybridArray<plString, 8> devices;
  for (const DevicePreset& preset : s_DevicePresets)
    devices.PushBack(preset.m_uiWidth == 0 ? plString(plTranslate(preset.m_szLabel)) : plString(preset.m_szLabel));
  m_pCanvasToolBarView->SetDevice(devices, m_iDevicePreset);

  // the zoom presets, and the zoom the mouse wheel set as an extra entry
  plHybridArray<plString, 8> zooms;
  zooms.PushBack(plTranslate("GameUi.Toolbar.ZoomFit"));
  zooms.PushBack("50 %");
  zooms.PushBack("100 %");
  zooms.PushBack("200 %");

  plInt32 iZoom = -1;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_ZoomPresets); ++i)
  {
    if (m_fZoom == s_ZoomPresets[i])
      iZoom = static_cast<plInt32>(i);
  }

  if (iZoom < 0)
  {
    plStringBuilder sZoom;
    sZoom.SetFormat("{} %", static_cast<plInt32>(plMath::Round(m_fZoom * 100.0f)));
    zooms.PushBack(sZoom);
    iZoom = static_cast<plInt32>(zooms.GetCount()) - 1;
  }

  m_pCanvasToolBarView->SetZoom(zooms, iZoom, true);

  // the language strip: the project's languages, then the pseudo-localization
  plHybridArray<plString, 8> languages;
  plInt32 iLanguage = -1;
  for (plUInt32 i = 0; i < m_Languages.GetCount(); ++i)
  {
    languages.PushBack(GetLanguageLabel(m_Languages[i]));
    if (!m_bPseudoLocalization && m_Languages[i] == m_sLanguage)
      iLanguage = static_cast<plInt32>(i);
  }

  languages.PushBack(plTranslate("GameUi.Toolbar.Pseudo"));
  if (m_bPseudoLocalization)
    iLanguage = static_cast<plInt32>(m_Languages.GetCount());

  m_pCanvasToolBarView->SetLanguage(languages, iLanguage);

  m_pCanvasToolBarView->SetChecked(Control::HighlightUntranslated, m_bHighlightUntranslated);
  m_pCanvasToolBarView->SetChecked(Control::SelectMode, !m_bAnchorsMode);
  m_pCanvasToolBarView->SetChecked(Control::AnchorsMode, m_bAnchorsMode);
  m_pCanvasToolBarView->SetChecked(Control::Interact, m_bInteractMode);
  m_pCanvasToolBarView->SetChecked(Control::Snap, m_bSnap);
  m_pCanvasToolBarView->SetChecked(Control::SafeArea, m_bSafeArea);
  m_pCanvasToolBarView->SetChecked(Control::NavGraph, m_bNavGraph);
  m_pCanvasToolBarView->SetChecked(Control::Checker, m_bCheckerboard);
}

void plUiDocumentAssetDocumentWindow::CollectLanguages()
{
  // languages = subfolders of the project's game and editor localization directories, by name
  plGameUiLocalization::CollectLanguages(m_LanguageInfos);

  m_Languages.Clear();
  for (const plGameUiLocalization::Language& language : m_LanguageInfos)
    m_Languages.PushBack(language.m_sName);
}

const plGameUiLocalization::Language* plUiDocumentAssetDocumentWindow::GetPreviewLanguage() const
{
  // the pseudo-localization stretches the English texts where the project has them
  return plGameUiLocalization::FindLanguage(m_LanguageInfos, m_bPseudoLocalization ? plStringView("en") : m_sLanguage.GetView());
}

void plUiDocumentAssetDocumentWindow::UpdateLayoutContext()
{
  const plGameUiLocalization::Language* pLanguage = GetPreviewLanguage();
  m_pPreviewTranslator = pLanguage != nullptr ? plGameUiLocalization::CreateTranslator(*pLanguage) : nullptr;

  m_LayoutContext = plUiLayoutContext();
  m_LayoutContext.m_pTranslator = m_pPreviewTranslator.Borrow();
  m_LayoutContext.m_fPseudoExpansion = m_bPseudoLocalization ? 0.4f : 0.0f;
}

plString plUiDocumentAssetDocumentWindow::GetLanguageLabel(plStringView sLanguage) const
{
  const plStringView sNative = plGameUiLocalization::GetNativeName(sLanguage);

  // the editor font covers Latin, Greek and Cyrillic; other scripts read in English
  plResourceLock<plUiFontResource> pFont(plEditorUi::GetDefaultFont(), plResourceAcquireMode::BlockTillLoaded_NeverFail);
  if (pFont.GetAcquireResult() == plResourceAcquireResult::Final)
  {
    for (auto it = sNative.GetIteratorFront(); it.IsValid(); ++it)
    {
      if (pFont->GetGlyph(it.GetCharacter()) == nullptr)
        return plGameUiLocalization::GetEnglishName(sLanguage);
    }
  }

  return sNative;
}

void plUiDocumentAssetDocumentWindow::SendLanguageToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plStringBuilder sFolders;

  if (const plGameUiLocalization::Language* pLanguage = GetPreviewLanguage())
  {
    for (const plString& sFolder : pLanguage->m_Folders)
    {
      if (!sFolders.IsEmpty())
        sFolders.Append(";");

      sFolders.Append(sFolder);
    }
  }

  {
    plDocumentConfigMsgToEngine msg;
    msg.m_sWhatToDo = "UiLanguage";
    msg.m_sValue = sFolders.GetData();
    GetEditorEngineConnection()->SendMessage(&msg);
  }

  {
    // the strip's "Pseudo +40%"
    plDocumentConfigMsgToEngine msg;
    msg.m_sWhatToDo = "UiPseudo";
    msg.m_iValue = m_bPseudoLocalization ? 40 : 0;
    GetEditorEngineConnection()->SendMessage(&msg);
  }

  {
    plDocumentConfigMsgToEngine msg;
    msg.m_sWhatToDo = "UiHighlightUntranslated";
    msg.m_iValue = m_bHighlightUntranslated ? 1 : 0;
    GetEditorEngineConnection()->SendMessage(&msg);
  }

  // resource-version bump: binding rediscovery re-runs plTranslate with the new chain
  UpdatePreview();
}

void plUiDocumentAssetDocumentWindow::SendCanvasViewToEngine()
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiCanvasView";

  plStringBuilder sValue;
  sValue.SetFormat("{};{};{};{};{};{};{};{}", m_vDeviceResolution.x, m_vDeviceResolution.y, m_fZoom, m_vPan.x, m_vPan.y, m_bSafeArea ? 1 : 0, m_bCheckerboard ? 1 : 0, m_bNavGraph ? 1 : 0);
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plUiDocumentAssetDocumentWindow::RefreshCanvasView()
{
  // the rulers follow the transform by themselves
  SendCanvasViewToEngine();
  UpdateSelectionUi();
}

void plUiDocumentAssetDocumentWindow::SetInteractMode(bool bEnabled)
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

void plUiDocumentAssetDocumentWindow::SendInteractMouseState(const plVec2I32& viewPos, bool bButtonDown, float fWheelTicks /*= 0.0f*/)
{
  const float fWidth = m_pViewWidget->GetViewportSize().x;
  const float fHeight = m_pViewWidget->GetViewportSize().y;

  if (fWidth <= 0.0f || fHeight <= 0.0f)
    return;

  const float fX = plMath::Clamp(viewPos.x / fWidth, 0.0f, 1.0f);
  const float fY = plMath::Clamp(viewPos.y / fHeight, 0.0f, 1.0f);

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

  /// Asks for a new clip's name and duration with the engine dialog.
  bool AskForNewClip(plString& out_sName, float& out_fDuration)
  {
    if (!plEngineDialog::IsEngineEnabled())
      return false;

    plUiAddClipEngineDlg dlg;
    if (dlg.Exec() != plEngineDialog::Accepted)
      return false;

    out_sName = dlg.GetName();
    out_fDuration = dlg.GetDuration();
    return true;
  }

  /// Asks for a new track's element name and target with the engine dialog.
  bool AskForNewTrack(plArrayPtr<const plString> elementNames, const plString& sPreselect, plString& out_sElement, plInt32& out_iTarget)
  {
    if (!plEngineDialog::IsEngineEnabled())
      return false;

    plUiAddTrackEngineDlg dlg(elementNames, sPreselect);
    if (dlg.Exec() != plEngineDialog::Accepted)
      return false;

    out_sElement = dlg.GetElementName();
    out_iTarget = dlg.GetTarget();
    return true;
  }
} // namespace

void plUiDocumentAssetDocumentWindow::CollectElementList(plDynamicArray<ElementListEntry>& out_elements) const
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

void plUiDocumentAssetDocumentWindow::GetBindablePropertiesForType(const plRTTI* pType, plDynamicArray<plString>& out_bindingProps)
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

const char* plUiDocumentAssetDocumentWindow::GetBindingPropertyDisplayName(plStringView sBindingProperty)
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

void plUiDocumentAssetDocumentWindow::LoadTemplateEntries()
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
    plUiBindingsPanelBase::MockEntry& mockEntry = m_TemplateEntries.ExpandAndGetRef();
    mockEntry.m_sName = entry.m_sName.GetString();
    mockEntry.m_sValue = entry.m_InitialValue.CanConvertTo<plString>() ? entry.m_InitialValue.ConvertTo<plString>() : plString();
  }
}

void plUiDocumentAssetDocumentWindow::OnAddBindingRequested()
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

  if (!plEngineDialog::IsEngineEnabled())
    return;

  plDynamicArray<plUiAddBindingDialog::Element> dialogElements;
  plDynamicArray<plString> templateEntries;
  plInt32 iPreselect = 0;

  for (plUInt32 i = 0; i < elements.GetCount(); ++i)
  {
    auto& element = dialogElements.ExpandAndGetRef();
    element.m_sLabel = elements[i].m_sLabel;
    element.m_pType = elements[i].m_pObject->GetType();

    if (elements[i].m_Guid == preselectGuid)
      iPreselect = static_cast<plInt32>(i);
  }

  for (const auto& entry : m_TemplateEntries)
    templateEntries.PushBack(entry.m_sName);

  plUiAddBindingDialog dlg(dialogElements, iPreselect, templateEntries);
  if (dlg.Exec() != plEngineDialog::Accepted)
    return;

  const plInt32 iElement = dlg.GetElementIndex();
  if (iElement < 0 || iElement >= static_cast<plInt32>(elements.GetCount()))
    return;

  const plUuid elementGuid = elements[iElement].m_Guid;
  const plString sBindingProperty = dlg.GetBindingProperty();
  const plString sEntry = dlg.GetEntry();

  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Add UI Binding");
  if (pAccessor->SetValueByName(pObj, sBindingProperty, sEntry).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnRemoveBindingRequested(const plUuid& elementGuid, plStringView sBindingProperty)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Remove UI Binding");
  if (pAccessor->SetValueByName(pObj, sBindingProperty, plString()).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnBindingEntryEdited(const plUuid& elementGuid, plStringView sBindingProperty, plStringView sNewEntry)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Change UI Binding");
  if (pAccessor->SetValueByName(pObj, sBindingProperty, plString(sNewEntry)).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnBindingRowActivated(const plUuid& elementGuid)
{
  if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObj);
  }
}

void plUiDocumentAssetDocumentWindow::CreateBindingsPanel()
{
  m_pBindingsView = PL_DEFAULT_NEW(plUiBindingsView);
  m_pBindingsPanel = m_pBindingsView.Borrow();
  AddPanel({"Bindings", plTranslate("GameUi.Panel.Bindings")}, m_pBindingsView->CreateContent());

  m_pBindingsPanel->m_OnMockDataChanged = [this]()
  { SendMockBlackboardToEngine(); };
  m_pBindingsPanel->m_OnAddBindingRequested = [this]()
  { OnAddBindingRequested(); };
  m_pBindingsPanel->m_OnRemoveBindingRequested = [this](const plUuid& guid, plStringView sProperty)
  { OnRemoveBindingRequested(guid, sProperty); };
  m_pBindingsPanel->m_OnBindingEntryEdited = [this](const plUuid& guid, plStringView sProperty, plStringView sEntry)
  { OnBindingEntryEdited(guid, sProperty, sEntry); };
  m_pBindingsPanel->m_OnBindingRowActivated = [this](const plUuid& guid)
  { OnBindingRowActivated(guid); };
  m_pBindingsPanel->m_OnScenarioSaveRequested = [this](plStringView sName)
  { OnScenarioSaveRequested(sName); };
  m_pBindingsPanel->m_OnScenarioDeleteRequested = [this](plStringView sName)
  { OnScenarioDeleteRequested(sName); };
  m_pBindingsPanel->m_OnScenarioSelected = [this](plStringView sName)
  { OnScenarioSelected(sName); };
}

void plUiDocumentAssetDocumentWindow::UpdateBindingsList()
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

  plDynamicArray<plUiBindingsPanelBase::BindingInfo> bindings;

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
      plUiBindingsPanelBase::BindingInfo& info = bindings.ExpandAndGetRef();
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

      plUiBindingsPanelBase::BindingInfo& info = bindings.ExpandAndGetRef();
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

    plUiBindingsPanelBase::BindingInfo& info = bindings.ExpandAndGetRef();
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

void plUiDocumentAssetDocumentWindow::CreateAnimationsPanel()
{
  m_pAnimationsPanel = AddPanel<plUiAnimationsPanel>({"Animations", plTranslate("GameUi.Panel.Animations")});

  m_pAnimationsPanel->m_OnAddClipRequested = [this]()
  { OnAddClipRequested(); };
  m_pAnimationsPanel->m_OnRemoveClipRequested = [this](const plUuid& guid)
  { OnRemoveClipRequested(guid); };
  m_pAnimationsPanel->m_OnClipActivated = [this](const plUuid& guid)
  { OnClipActivated(guid); };
  m_pAnimationsPanel->m_OnClipRenamed = [this](const plUuid& guid, plStringView sName)
  { OnClipRenamed(guid, sName); };
  m_pAnimationsPanel->m_OnClipDurationChanged = [this](const plUuid& guid, float fDuration)
  { OnClipDurationChanged(guid, fDuration); };
  m_pAnimationsPanel->m_OnAddTrackRequested = [this](const plUuid& guid)
  { OnAddTrackRequested(guid); };
  m_pAnimationsPanel->m_OnRemoveTrackRequested = [this](const plUuid& guid)
  { OnRemoveTrackRequested(guid); };
  m_pAnimationsPanel->m_OnAddKeyRequested = [this](const plUuid& guid, float fTime)
  { OnAddKeyRequested(guid, fTime); };
  m_pAnimationsPanel->m_OnRemoveKeyRequested = [this](const plUuid& guid)
  { OnRemoveKeyRequested(guid); };
  m_pAnimationsPanel->m_OnKeyMoved = [this](const plUuid& guid, float fTime)
  { OnKeyMoved(guid, fTime); };
  m_pAnimationsPanel->m_OnKeyEdited = [this](const plUiAnimationsPanel::KeyInfo& key)
  { OnKeyEdited(key); };
  m_pAnimationsPanel->m_OnScrubbed = [this](plStringView sName, float fTime)
  { OnClipScrubbed(sName, fTime); };
  m_pAnimationsPanel->m_OnPlayRequested = [this](plStringView sName)
  { OnClipPlay(sName); };
  m_pAnimationsPanel->m_OnStopRequested = [this]()
  { OnClipStop(); };
  m_pAnimationsPanel->m_OnEditsBegin = [this](plStringView sName)
  { OnAnimationEditsBegin(sName); };
  m_pAnimationsPanel->m_OnEditsEnd = [this](bool bCommit)
  { OnAnimationEditsEnd(bCommit); };
}

void plUiDocumentAssetDocumentWindow::OnClipRenamed(const plUuid& clipGuid, plStringView sName)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(clipGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Rename UI Animation Clip");
  pAccessor->SetValueByName(pObj, "Name", plString(sName)).IgnoreResult();
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnClipDurationChanged(const plUuid& clipGuid, float fDuration)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(clipGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Change UI Animation Clip Duration");
  pAccessor->SetValueByName(pObj, "Duration", fDuration).IgnoreResult();
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnAnimationEditsBegin(plStringView sName)
{
  // edits of several keys, and curve drags, are one undo step: the per-key transactions nest in this one
  GetDocument()->GetObjectAccessor()->StartTransaction(sName);
}

void plUiDocumentAssetDocumentWindow::OnAnimationEditsEnd(bool bCommit)
{
  if (bCommit)
    GetDocument()->GetObjectAccessor()->FinishTransaction();
  else
    GetDocument()->GetObjectAccessor()->CancelTransaction();
}

void plUiDocumentAssetDocumentWindow::UpdateAnimationsList()
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

  plDynamicArray<plUiAnimationsPanel::ClipInfo> clips;

  plHybridArray<const plDocumentObject*, 16> clipObjects, trackObjects, keyObjects;
  childrenOf(GetPropertiesObject(), "Animations", clipObjects);

  for (const plDocumentObject* pClipObj : clipObjects)
  {
    const auto& clip = pClipObj->GetTypeAccessor();

    plUiAnimationsPanel::ClipInfo& info = clips.ExpandAndGetRef();
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

      plUiAnimationsPanel::TrackInfo& trackInfo = info.m_Tracks.ExpandAndGetRef();
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

        plUiAnimationsPanel::KeyInfo& keyInfo = trackInfo.m_Keys.ExpandAndGetRef();
        keyInfo.m_Guid = pKeyObj->GetGuid();
        keyInfo.m_fTime = key.GetValue("Time").ConvertTo<float>();
        keyInfo.m_fValue = key.GetValue("Value").ConvertTo<float>();
        keyInfo.m_iEasing = key.GetValue("Easing").ConvertTo<plInt32>();

        const plVariant color = key.GetValue("Color");
        if (color.IsA<plColor>())
          keyInfo.m_Color = color.Get<plColor>();
      }

      trackInfo.m_Keys.Sort([](const plUiAnimationsPanel::KeyInfo& a, const plUiAnimationsPanel::KeyInfo& b)
        { return a.m_fTime < b.m_fTime; });
    }
  }

  m_pAnimationsPanel->SetClips(clips);
}

void plUiDocumentAssetDocumentWindow::OnAddClipRequested()
{
  plString sName;
  float fDuration = 0.0f;
  if (!AskForNewClip(sName, fDuration))
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
    pAccessor->SetValueByName(pClip, "Name", sName).IgnoreResult();
    pAccessor->SetValueByName(pClip, "Duration", fDuration).IgnoreResult();
  }

  pAccessor->FinishTransaction();

  // show the clip in the property grid so triggers/loop mode can be set right away
  if (const plDocumentObject* pClip = GetDocument()->GetObjectManager()->GetObject(clipGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pClip);
  }
}

void plUiDocumentAssetDocumentWindow::OnRemoveClipRequested(const plUuid& clipGuid)
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

void plUiDocumentAssetDocumentWindow::OnClipActivated(const plUuid& clipGuid)
{
  if (const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(clipGuid))
  {
    GetDocument()->GetSelectionManager()->SetSelection(pObj);
  }
}

void plUiDocumentAssetDocumentWindow::OnAddTrackRequested(const plUuid& clipGuid)
{
  const plDocumentObject* pClip = GetDocument()->GetObjectManager()->GetObject(clipGuid);
  if (pClip == nullptr)
    return;

  // offer the NAMED elements (tracks reference elements by name)
  plDynamicArray<plString> elementNames;
  plString sPreselect;
  {
    plDynamicArray<ElementListEntry> elements;
    CollectElementList(elements);

    const plDocumentObject* pSelected = GetDocument()->GetSelectionManager()->GetCurrentObject();

    for (const ElementListEntry& element : elements)
    {
      const plVariant name = element.m_pObject->GetTypeAccessor().GetValue("Name");
      if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
      {
        elementNames.PushBack(name.Get<plString>());

        if (element.m_pObject == pSelected)
        {
          sPreselect = name.Get<plString>();
        }
      }
    }
  }

  plString sElement;
  plInt32 iTarget = 0;
  if (!AskForNewTrack(elementNames, sPreselect, sElement, iTarget))
    return;

  const float fDuration = pClip->GetTypeAccessor().GetValue("Duration").ConvertTo<float>();
  const float fDefaultValue = GetTargetDefaultValue(iTarget);

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
    pAccessor->SetValueByName(pTrack, "Element", sElement).IgnoreResult();
    pAccessor->SetValueByName(pTrack, "Target", iTarget).IgnoreResult();

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

void plUiDocumentAssetDocumentWindow::OnRemoveTrackRequested(const plUuid& trackGuid)
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

void plUiDocumentAssetDocumentWindow::OnAddKeyRequested(const plUuid& trackGuid, float fTime, bool bSelectKey)
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

void plUiDocumentAssetDocumentWindow::OnRemoveKeyRequested(const plUuid& keyGuid)
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

void plUiDocumentAssetDocumentWindow::OnKeyMoved(const plUuid& keyGuid, float fNewTime)
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

void plUiDocumentAssetDocumentWindow::OnKeyEdited(const plUiAnimationsPanel::KeyInfo& key)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(key.m_Guid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Edit UI Animation Key");
  pAccessor->SetValueByName(pObj, "Time", key.m_fTime).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Value", key.m_fValue).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Color", key.m_Color).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Easing", key.m_iEasing).IgnoreResult();
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnClipScrubbed(plStringView sClipName, float fTime)
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiClipPreview";

  // time first — the clip name may contain any characters
  plStringBuilder sValue;
  sValue.SetFormat("{};{}", fTime, sClipName);
  msg.m_sValue = sValue.GetData();

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plUiDocumentAssetDocumentWindow::OnClipPlay(plStringView sClipName)
{
  if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDocumentConfigMsgToEngine msg;
  msg.m_sWhatToDo = "UiClipPlay";
  msg.m_sValue = sClipName;

  GetEditorEngineConnection()->SendMessage(&msg);
}

void plUiDocumentAssetDocumentWindow::OnClipStop()
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

void plUiDocumentAssetDocumentWindow::CreateStatesPanel()
{
  m_pStatesView = PL_DEFAULT_NEW(plUiStatesView);
  m_pStatesPanel = m_pStatesView.Borrow();
  AddPanel({"States", plTranslate("GameUi.Panel.States")}, m_pStatesView->CreateContent());

  m_pStatesPanel->m_OnDisabledBindingChanged = [this](const plUuid& guid, plStringView sValue)
  { OnElementStringPropertyChanged(guid, "DisabledBinding", sValue); };
  m_pStatesPanel->m_OnAddStateRequested = [this](const plUuid& guid)
  { OnAddStateRequested(guid); };
  m_pStatesPanel->m_OnRemoveStateRequested = [this](const plUuid& guid)
  { OnRemoveStateRequested(guid); };
  m_pStatesPanel->m_OnStateEdited = [this](const plUiStatesPanelBase::StateRow& state)
  { OnStateEdited(state); };
  m_pStatesPanel->m_OnStateStyleChanged = [this](const plUuid& guid, plStringView sStyleRef)
  { OnElementStringPropertyChanged(guid, "Style", sStyleRef); };
  // the same "select the object in the grid" as a clip
  m_pStatesPanel->m_OnStateActivated = [this](const plUuid& guid)
  { OnClipActivated(guid); };
  m_pStatesPanel->m_OnTestValueChanged = [this](plStringView sEntry, plStringView sValue)
  { OnMockTestValue(sEntry, sValue); };
}

void plUiDocumentAssetDocumentWindow::UpdateStatesPanel()
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

  plUiStatesPanelBase::ElementStateInfo info;
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

      plUiStatesPanelBase::StateRow& row = info.m_States.ExpandAndGetRef();
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

void plUiDocumentAssetDocumentWindow::OnAddStateRequested(const plUuid& elementGuid)
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

void plUiDocumentAssetDocumentWindow::OnRemoveStateRequested(const plUuid& stateGuid)
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

void plUiDocumentAssetDocumentWindow::OnStateEdited(const plUiStatesPanelBase::StateRow& state)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(state.m_Guid);
  if (pObj == nullptr)
    return;

  plStringBuilder sName(state.m_sName);
  sName.Trim(" \t\r\n");

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Edit UI State");
  pAccessor->SetValueByName(pObj, "Name", plString(sName)).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Entry", state.m_sEntry).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Condition", state.m_iCondition).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Value", state.m_fValue).IgnoreResult();
  pAccessor->SetValueByName(pObj, "ValueText", state.m_sValueText).IgnoreResult();
  pAccessor->SetValueByName(pObj, "Visibility", state.m_iVisibility).IgnoreResult();
  pAccessor->SetValueByName(pObj, "PlayClip", state.m_sPlayClip).IgnoreResult();
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnElementStringPropertyChanged(const plUuid& elementGuid, const char* szProperty, plStringView sValue)
{
  const plDocumentObject* pObj = GetDocument()->GetObjectManager()->GetObject(elementGuid);
  if (pObj == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->StartTransaction("Edit UI Element State");
  if (pAccessor->SetValueByName(pObj, szProperty, plString(sValue)).Failed())
  {
    pAccessor->CancelTransaction();
    return;
  }
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnMockTestValue(plStringView sEntry, plStringView sValue)
{
  if (m_pBindingsPanel == nullptr)
    return;

  m_pBindingsPanel->SetMockValueSilently(sEntry, sValue);
  SendMockBlackboardToEngine();
}

void plUiDocumentAssetDocumentWindow::ProcessMessageEventHandler(const plEditorEngineDocumentMsg* pMsg)
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

    if (pConfigMsg->m_sWhatToDo == "UiTextFit")
    {
      ParseTextFit(pConfigMsg->m_sPayload);
      UpdateTextFitPanel();
      UpdateSizeFitPanel();
      return;
    }

    if (pConfigMsg->m_sWhatToDo == "UiDrawStats")
    {
      // m_sPayload = "batches;quads"
      plHybridArray<plStringView, 2> parts;
      pConfigMsg->m_sPayload.GetView().Split(false, parts, ";");

      if (parts.GetCount() == 2)
      {
        plStringBuilder sBatches(parts[0]), sQuads(parts[1]);

        m_pCanvasStatusRow->SetDrawStats(sBatches, sQuads);
      }

      return;
    }
  }

  plEngineDocumentWindow::ProcessMessageEventHandler(pMsg);
}

void plUiDocumentAssetDocumentWindow::SendMockBlackboardToEngine()
{
  if (m_pBindingsPanel == nullptr || plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
    return;

  plDynamicArray<plUiBindingsPanelBase::MockEntry> entries;
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

void plUiDocumentAssetDocumentWindow::UpdateScenariosList()
{
  if (m_pBindingsPanel == nullptr)
    return;

  plDynamicArray<plString> names;

  for (const plUiMockScenario* pScenario : m_pAssetDoc->GetProperties()->m_MockScenarios)
  {
    if (pScenario != nullptr && !pScenario->m_sName.IsEmpty())
    {
      names.PushBack(pScenario->m_sName);
    }
  }

  m_pBindingsPanel->SetScenarios(names);
}

const plDocumentObject* plUiDocumentAssetDocumentWindow::FindScenarioObject(plStringView sWanted) const
{
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

void plUiDocumentAssetDocumentWindow::OnScenarioSaveRequested(plStringView sName)
{
  if (m_pBindingsPanel == nullptr)
    return;

  // snapshot the current mock table in the "UiMockBlackboard" newline format
  plDynamicArray<plUiBindingsPanelBase::MockEntry> entries;
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
      bOk = bOk && pAccessor->SetValueByName(pNewObj, "Name", plString(sName)).Succeeded();
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

void plUiDocumentAssetDocumentWindow::OnScenarioDeleteRequested(plStringView sName)
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

void plUiDocumentAssetDocumentWindow::OnScenarioSelected(plStringView sName)
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

  plDynamicArray<plUiBindingsPanelBase::MockEntry> entries;

  for (plUInt32 i = 0; i + 1 < parts.GetCount(); i += 2)
  {
    plUiBindingsPanelBase::MockEntry& entry = entries.ExpandAndGetRef();
    entry.m_sName = parts[i];
    entry.m_sValue = parts[i + 1];
  }

  if (parts.GetCount() % 2 == 1 && !parts.PeekBack().IsEmpty())
  {
    plUiBindingsPanelBase::MockEntry& entry = entries.ExpandAndGetRef();
    entry.m_sName = parts.PeekBack();
    entry.m_sValue = plString();
  }

  // SetMockEntries reports MockDataChanged when values changed -> live preview push
  m_pBindingsPanel->SetMockEntries(entries);
}

void plUiDocumentAssetDocumentWindow::SetEngineConnectedIndicator(bool bConnected)
{
  m_pCanvasStatusRow->SetEngineState(bConnected, bConnected ? plTranslate("GameUi.Status.EngineConnected") : plTranslate("GameUi.Status.EngineRestarting"));
}

void plUiDocumentAssetDocumentWindow::EngineProcessEventHandler(const plEditorEngineProcessConnection::Event& e)
{
  if (e.m_Type == plEditorEngineProcessConnection::Event::Type::ProcessCrashed)
  {
    m_Timers.Start(plTime::MakeZero(), [this]()
      { SetEngineConnectedIndicator(false); });
  }

  if (e.m_Type == plEditorEngineProcessConnection::Event::Type::ProcessRestarted)
  {
    // the engine process lost all session state: re-push everything the preview needs
    m_Timers.Start(plTime::MakeZero(), [this]()
      {
        SetEngineConnectedIndicator(true);
        UpdatePreview();
        SendCanvasViewToEngine();
        SendMockBlackboardToEngine();
        SendSelectionToEngine();
        SendLanguageToEngine(); });
  }
}

//////////////////////////////////////////////////////////////////////////
// rect transform panel + status row

void plUiDocumentAssetDocumentWindow::CreateRectTransformPanel()
{
  m_pRectTransformView = AddPanel<plUiRectTransformView>({"RectTransform", plTranslate("GameUi.Section.RectTransform")});
  m_pRectTransformView->m_OnAnchorPreset = [this](plInt8 iCol, plInt8 iRow)
  { ApplyAnchorPreset(iCol, iRow); };
  m_pRectTransformView->m_OnOffsetsEdited = [this](const plVec4& vOffsets)
  { ApplyOffsets(vOffsets); };
}

plString plUiDocumentAssetDocumentWindow::ComputeAnchorSummary(const plUiElement* pElement)
{
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

  return sText;
}

bool plUiDocumentAssetDocumentWindow::LoadCompiledStyle(const plString& sStyleRef, plUiStyle& out_style) const
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

void plUiDocumentAssetDocumentWindow::UpdateEffectiveStyle()
{
  if (m_pEffectiveStyleView == nullptr)
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
    m_pEffectiveStyleView->SetEmpty();
    return;
  }

  plStringBuilder sName = selected.m_pElement->m_sName;
  if (sName.IsEmpty())
    sName = plTranslate(selected.m_pElement->GetDynamicRTTI()->GetTypeName());

  // ————— resolve the cascade: element locals, replaced wholesale by the style asset —————

  const plVariant styleRef = pObj->GetTypeAccessor().GetValue("Style");
  const plString sStyleRef = styleRef.IsA<plString>() ? styleRef.Get<plString>() : plString();

  plUiStyle style;
  const bool bStyled = !sStyleRef.IsEmpty();
  const bool bStyleLoaded = bStyled && LoadCompiledStyle(sStyleRef, style);
  const plString sStyleName = bStyled ? GetAssetRefDisplayName(sStyleRef) : plString();

  plDynamicArray<plUiEffectiveStyleView::Row> rows;
  using Source = plUiEffectiveStyleView::Row::Source;

  const plString sElementSource = plTranslate("GameUi.EffectiveStyle.SourceElement");
  plStringBuilder sStyleSource;
  sStyleSource.SetFormat("{}: {}", plTranslate("GameUi.EffectiveStyle.SourceStyle"), sStyleName);

  auto addColor = [&](const char* szName, const plColorGammaUB& color, bool bFromStyle)
  {
    plUiEffectiveStyleView::Row& row = rows.ExpandAndGetRef();
    row.m_sProperty = plTranslate(szName);

    plStringBuilder sValue;
    sValue.SetFormat("#{}{}{} \xC2\xB7 {}%", plArgU(color.r, 2, true, 16, true), plArgU(color.g, 2, true, 16, true), plArgU(color.b, 2, true, 16, true), static_cast<int>(color.a / 2.55f));
    row.m_sValue = sValue;

    row.m_Swatch = plColorGammaUB(color.r, color.g, color.b, 255);
    row.m_sSource = bFromStyle ? plString(sStyleSource) : sElementSource;
    row.m_Source = bFromStyle ? Source::Style : Source::Element;
  };

  auto addValue = [&](const char* szName, plStringView sValue, bool bFromStyle)
  {
    plUiEffectiveStyleView::Row& row = rows.ExpandAndGetRef();
    row.m_sProperty = plTranslate(szName);
    row.m_sValue = sValue;
    row.m_sSource = bFromStyle ? plString(sStyleSource) : sElementSource;
    row.m_Source = bFromStyle ? Source::Style : Source::Element;
  };

  plStringBuilder sValue;

  if (bStyled && !bStyleLoaded)
  {
    plUiEffectiveStyleView::Row& row = rows.ExpandAndGetRef();
    row.m_sProperty = plTranslate("GameUi.EffectiveStyle.NotTransformed");
    row.m_sSource = sStyleSource;
    row.m_Source = Source::Style;
  }
  else if (pVisual != nullptr)
  {
    // "style replaces visuals": with a loaded style EVERY value comes from it
    const bool bS = bStyleLoaded;

    addColor("Color", bS ? style.m_Color : pVisual->m_Color, bS);

    const plVec4 vCornerRadius = bS ? style.m_vCornerRadius : pVisual->m_vCornerRadius;
    sValue.SetFormat("{} / {} / {} / {} px", vCornerRadius.x, vCornerRadius.y, vCornerRadius.z, vCornerRadius.w);
    addValue("CornerRadius", sValue, bS);

    const float fBorder = bS ? style.m_fBorderWidth : pVisual->m_fBorderWidth;
    if (fBorder > 0.0f)
    {
      sValue.SetFormat("{} px", fBorder);
      addValue("BorderWidth", sValue, bS);
      addColor("BorderColor", bS ? style.m_BorderColor : pVisual->m_BorderColor, bS);
    }

    const float fShadow = bS ? style.m_fShadowSize : pVisual->m_fShadowSize;
    if (fShadow > 0.0f)
    {
      sValue.SetFormat("{} px", fShadow);
      addValue("ShadowSize", sValue, bS);
      addColor("ShadowColor", bS ? style.m_ShadowColor : pVisual->m_ShadowColor, bS);
    }

    if (const plUiButton* pButton = plDynamicCast<const plUiButton*>(pVisual))
    {
      addColor("HoverColor", bS ? style.m_HoverColor : pButton->m_HoverColor, bS);
      addColor("PressedColor", bS ? style.m_PressedColor : pButton->m_PressedColor, bS);

      if (bS)
      {
        sValue.SetFormat("{} s", style.m_fTransitionDuration);
        addValue("TransitionDuration", sValue, bS);
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
      sValue.SetFormat("{} px", bS ? style.m_uiFontSize : pTextInput->m_uiFontSize);
      addValue("FontSize", sValue, bS);
    }
  }
  else if (pText != nullptr)
  {
    const bool bS = bStyleLoaded;
    addColor("TextColor", bS ? style.m_TextColor : pText->m_Color, bS);
    sValue.SetFormat("{} px", bS ? style.m_uiFontSize : pText->m_uiFontSize);
    addValue("FontSize", sValue, bS);
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

    plStringBuilder sProperty;
    sProperty.SetFormat("{}: {}", plTranslate("GameUi.EffectiveStyle.SourceState"), stateName.IsA<plString>() ? stateName.Get<plString>().GetView() : plStringView());

    plUiEffectiveStyleView::Row& row = rows.ExpandAndGetRef();
    row.m_sProperty = sProperty;
    row.m_sValue = GetAssetRefDisplayName(stateStyle.Get<plString>());
    row.m_sSource = entry.IsA<plString>() ? entry.Get<plString>() : plString();
    row.m_Source = Source::State;
  }

  m_pEffectiveStyleView->SetRows(sName, rows);
}

void plUiDocumentAssetDocumentWindow::ApplyAnchorPreset(plInt8 iCol, plInt8 iRow)
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

void plUiDocumentAssetDocumentWindow::ApplyOffsets(const plVec4& vOffsets)
{
  HitResult selected;
  if (!GetSelectedElementRect(selected))
    return;

  const plDocumentObject* pObj = GetElementDocObject(selected.m_Path);
  if (pObj == nullptr)
    return;

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

void plUiDocumentAssetDocumentWindow::UpdateSelectionUi()
{
  HitResult selected;
  const bool bHasElement = GetSelectedElementRect(selected);

  if (m_pRectTransformView != nullptr)
  {
    plUiRectTransformView::State state;
    state.m_bHasElement = bHasElement;

    if (bHasElement)
    {
      state.m_bInContainer = IsContainerParent(m_pAssetDoc->GetProperties(), selected.m_Path);
      state.m_vAnchorMin = selected.m_pElement->m_vAnchorMin;
      state.m_vAnchorMax = selected.m_pElement->m_vAnchorMax;
      state.m_vOffsets = selected.m_pElement->m_vOffsets;
      state.m_sSummary = ComputeAnchorSummary(selected.m_pElement);
    }

    m_pRectTransformView->SetState(state);
  }

  if (bHasElement)
  {
    plStringBuilder sName = selected.m_pElement->m_sName;
    if (sName.IsEmpty())
      sName = "<unnamed>";

    plStringBuilder sElement, sPosition, sSize;
    sElement.SetFormat("{} ({})", sName, plTranslate(selected.m_pElement->GetDynamicRTTI()->GetTypeName()));
    sPosition.SetFormat("{}, {}", plArgF(selected.m_Rect.x, 0), plArgF(selected.m_Rect.y, 0));
    sSize.SetFormat("{} \xC3\x97 {}", plArgF(selected.m_Rect.width, 0), plArgF(selected.m_Rect.height, 0));
    m_pCanvasStatusRow->SetSelection(sElement, sPosition, sSize);
  }
  else
  {
    m_pCanvasStatusRow->SetSelection({}, {}, {});
  }

  UpdateEffectiveStyle();
  UpdateSizeFitPanel();
  UpdateTextFitPanel();
}

void plUiDocumentAssetDocumentWindow::SetCanvasStatusMessage(plStringView sMessage)
{
  m_pCanvasStatusRow->SetMessage(sMessage);
}

void plUiDocumentAssetDocumentWindow::RemoveSelectedElements()
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
