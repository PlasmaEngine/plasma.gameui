#include <EnginePluginGameUi/EnginePluginGameUiPCH.h>

#include <EnginePluginGameUi/UiStyleAsset/UiStyleContext.h>
#include <EnginePluginGameUi/UiStyleAsset/UiStyleViewContext.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>

plUiStyleViewContext::plUiStyleViewContext(plUiStyleContext* pUiStyleContext)
  : plEngineProcessViewContext(pUiStyleContext)
{
  m_pUiStyleContext = pUiStyleContext;

  // Start with something valid.
  m_Camera.SetCameraMode(plCameraMode::PerspectiveFixedFovX, 45.0f, 0.01f, 1000.0f);
  m_Camera.LookAt(plVec3(1, 1, 1), plVec3::MakeZero(), plVec3(0.0f, 0.0f, 1.0f));
}

plUiStyleViewContext::~plUiStyleViewContext() = default;

plViewHandle plUiStyleViewContext::CreateView()
{
  plView* pView = nullptr;
  plRenderWorld::CreateView("UI Style Editor - View", pView);
  pView->SetCameraUsageHint(plCameraUsageHint::EditorView);

  pView->SetRenderPipelineResource(CreateDefaultRenderPipeline());

  plEngineProcessDocumentContext* pDocumentContext = GetDocumentContext();
  pView->SetWorld(pDocumentContext->GetWorld());
  pView->SetCamera(&m_Camera);
  return pView->GetHandle();
}
