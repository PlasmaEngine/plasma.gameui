#include <EnginePluginGameUi/EnginePluginGameUiPCH.h>

#include <EnginePluginGameUi/UiDocumentAsset/UiDocumentContext.h>
#include <EnginePluginGameUi/UiDocumentAsset/UiDocumentViewContext.h>
#include <Foundation/Math/ColorScheme.h>
#include <GameUiPlugin/Elements/UiLayout.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>

plUiDocumentViewContext::plUiDocumentViewContext(plUiDocumentContext* pUiDocumentContext)
  : plEngineProcessViewContext(pUiDocumentContext)
{
  m_pUiDocumentContext = pUiDocumentContext;

  // Start with something valid.
  m_Camera.SetCameraMode(plCameraMode::PerspectiveFixedFovX, 45.0f, 0.01f, 1000.0f);
  m_Camera.LookAt(plVec3(1, 1, 1), plVec3::MakeZero(), plVec3(0.0f, 0.0f, 1.0f));
}

plUiDocumentViewContext::~plUiDocumentViewContext() = default;

bool plUiDocumentViewContext::UpdateThumbnailCamera(const plBoundingBoxSphere& bounds)
{
  return !FocusCameraOnObject(m_Camera, bounds, 45.0f, -plVec3(5, -2, 3));
}

void plUiDocumentViewContext::SetCamera(const plViewRedrawMsgToEngine* pMsg)
{
  plEngineProcessViewContext::SetCamera(pMsg);

  m_fWindowWidth = static_cast<float>(pMsg->m_uiWindowWidth);
  m_fWindowHeight = static_cast<float>(pMsg->m_uiWindowHeight);
}

void plUiDocumentViewContext::DrawSelectionGizmos() const
{
  DrawSelectionGizmos(m_fWindowWidth, m_fWindowHeight);
}

void plUiDocumentViewContext::DrawSelectionGizmos(float fViewWidth, float fViewHeight) const
{
  if (fViewWidth <= 0.0f || fViewHeight <= 0.0f)
    return;

  const plUiDocumentResourceHandle& hResource = m_pUiDocumentContext->GetResource();
  if (!hResource.IsValid())
    return;

  plResourceLock<plUiDocumentResource> pResource(hResource, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
  if (pResource.GetAcquireResult() != plResourceAcquireResult::Final || pResource->GetRoot() == nullptr)
    return;

  // the canvas transform must exactly match what the preview component renders with
  float fScale = 1.0f;
  plVec2 vOffset = plVec2::MakeZero();
  plVec2 vCanvasSize = plVec2::MakeZero();
  plHybridArray<plUiFocusable, 32> focusables;
  plUiLayoutContext layoutContext;
  {
    // const world so the const TryGetComponent overload is used (write-access assert otherwise)
    const plWorld* pWorld = m_pUiDocumentContext->GetWorld();
    PL_LOCK(pWorld->GetReadMarker());

    const plUiCanvasComponent* pComponent = nullptr;
    if (!pWorld->TryGetComponent(m_pUiDocumentContext->GetCanvasComponent(), pComponent))
      return;

    pComponent->ComputeCanvasTransform(plVec2(fViewWidth, fViewHeight), fScale, vOffset, vCanvasSize);

    // the selection is placed where the preview draws it (fit content measures the displayed texts)
    layoutContext = pComponent->GetLayoutContext();

    if (m_pUiDocumentContext->GetShowNavGraph())
    {
      pComponent->CollectFocusables(plVec2(fViewWidth, fViewHeight), focusables);
    }
  }

  const plRectFloat canvasRect(0.0f, 0.0f, vCanvasSize.x, vCanvasSize.y);
  const plRectFloat screenCanvasRect(vOffset.x, vOffset.y, vCanvasSize.x * fScale, vCanvasSize.y * fScale);

  const plColor selectionColor = plColorScheme::GetCategoryColor("UI", plColorScheme::CategoryColorUsage::ViewportIcon);
  const plDebugRendererContext debugContext(m_hView);

  // device frame outline when a preset is active
  if (m_pUiDocumentContext->HasDevicePreset())
  {
    plDebugRenderer::Draw2DLineRectangle(debugContext, screenCanvasRect, 0.0f, plColor(0.6f, 0.6f, 0.6f));

    plDebugRenderer::Draw2DText(debugContext, plFmt("{} x {}", vCanvasSize.x, vCanvasSize.y),
      plVec2I32(static_cast<plInt32>(screenCanvasRect.x), static_cast<plInt32>(screenCanvasRect.y) - 20), plColor(0.6f, 0.6f, 0.6f), 14);
  }

  // safe-area overlay (5% inset, console-cert style)
  if (m_pUiDocumentContext->GetShowSafeArea())
  {
    const float fInsetX = screenCanvasRect.width * 0.05f;
    const float fInsetY = screenCanvasRect.height * 0.05f;

    const plRectFloat safeRect(screenCanvasRect.x + fInsetX, screenCanvasRect.y + fInsetY, screenCanvasRect.width - 2.0f * fInsetX, screenCanvasRect.height - 2.0f * fInsetY);

    plDebugRenderer::Draw2DLineRectangle(debugContext, safeRect, 0.0f, plColor(0.04f, 0.87f, 0.7f, 0.6f)); // PlasmaBranding teal
  }

  // gamepad navigation graph overlay (mockup: teal edges = geometric auto-nav, orange
  // edges = explicit NavUp/Down/Left/Right overrides, star = the DefaultFocus widget)
  if (m_pUiDocumentContext->GetShowNavGraph() && !focusables.IsEmpty())
  {
    const plColor autoColor(0.04f, 0.87f, 0.7f, 0.85f);
    const plColor explicitColor(1.0f, 0.55f, 0.1f, 0.9f);

    plHybridArray<plDebugRenderer::Line, 64> autoLines;
    plHybridArray<plDebugRenderer::Line, 16> explicitLines;

    auto toScreen = [&](const plRectFloat& rect)
    {
      return plVec2((rect.x + rect.width * 0.5f) * fScale + vOffset.x, (rect.y + rect.height * 0.5f) * fScale + vOffset.y);
    };

    auto addArrow = [&](plDynamicArray<plDebugRenderer::Line>& lines, const plVec2& vFrom, const plVec2& vTo)
    {
      lines.PushBack(plDebugRenderer::Line(plVec3(vFrom.x, vFrom.y, 0), plVec3(vTo.x, vTo.y, 0)));

      // arrowhead at 65% toward the target, so mutual edges stay readable
      plVec2 vDir = vTo - vFrom;
      const float fLen = vDir.GetLength();

      if (fLen > 12.0f)
      {
        vDir /= fLen;
        const plVec2 vPerp(-vDir.y, vDir.x);
        const plVec2 vTip = vFrom + vDir * (fLen * 0.65f);

        lines.PushBack(plDebugRenderer::Line(plVec3(vTip.x, vTip.y, 0), plVec3(vTip.x - vDir.x * 7.0f + vPerp.x * 4.0f, vTip.y - vDir.y * 7.0f + vPerp.y * 4.0f, 0)));
        lines.PushBack(plDebugRenderer::Line(plVec3(vTip.x, vTip.y, 0), plVec3(vTip.x - vDir.x * 7.0f - vPerp.x * 4.0f, vTip.y - vDir.y * 7.0f - vPerp.y * 4.0f, 0)));
      }
    };

    for (const plUiFocusable& focusable : focusables)
    {
      const plVec2 vFrom = toScreen(focusable.m_Rect);

      for (plUInt8 uiDir = plUiNavAction::Up; uiDir <= plUiNavAction::Right; ++uiDir)
      {
        bool bExplicit = false;
        const plInt32 iTarget = plUiCanvasComponent::FindNavTarget(focusable, static_cast<plUiNavAction::Enum>(uiDir), focusables, &bExplicit);

        if (iTarget < 0)
          continue;

        if (bExplicit)
        {
          addArrow(explicitLines, vFrom, toScreen(focusables[iTarget].m_Rect));
        }
        else
        {
          addArrow(autoLines, vFrom, toScreen(focusables[iTarget].m_Rect));
        }
      }

      // DefaultFocus star marker at the widget's top-left corner
      if (focusable.m_pElement->m_bDefaultFocus)
      {
        const plVec2 vStar(focusable.m_Rect.x * fScale + vOffset.x, focusable.m_Rect.y * fScale + vOffset.y);
        constexpr float fR = 6.0f;

        for (plUInt32 i = 0; i < 4; ++i)
        {
          const plAngle angle = plAngle::MakeFromDegree(45.0f * static_cast<float>(i));
          const plVec2 vSpoke(plMath::Cos(angle) * fR, plMath::Sin(angle) * fR);
          autoLines.PushBack(plDebugRenderer::Line(plVec3(vStar.x - vSpoke.x, vStar.y - vSpoke.y, 0), plVec3(vStar.x + vSpoke.x, vStar.y + vSpoke.y, 0)));
        }
      }
    }

    plDebugRenderer::Draw2DLines(debugContext, autoLines, autoColor);
    plDebugRenderer::Draw2DLines(debugContext, explicitLines, explicitColor);
  }

  // drag-snap guide lines (mirrored from the editor while dragging; brand-bright cyan)
  {
    const auto& guides = m_pUiDocumentContext->GetSnapGuides();

    if (!guides.IsEmpty())
    {
      plHybridArray<plDebugRenderer::Line, 8> guideLines;

      for (const plUiPreviewGuide& guide : guides)
      {
        if (guide.m_bVertical)
        {
          const float x = guide.m_fPos * fScale + vOffset.x;
          guideLines.PushBack(plDebugRenderer::Line(plVec3(x, guide.m_fMin * fScale + vOffset.y, 0), plVec3(x, guide.m_fMax * fScale + vOffset.y, 0)));
        }
        else
        {
          const float y = guide.m_fPos * fScale + vOffset.y;
          guideLines.PushBack(plDebugRenderer::Line(plVec3(guide.m_fMin * fScale + vOffset.x, y, 0), plVec3(guide.m_fMax * fScale + vOffset.x, y, 0)));
        }
      }

      plDebugRenderer::Draw2DLines(debugContext, guideLines, plColor(0.02f, 0.98f, 0.79f)); // #06fac9
    }
  }

  DrawTextFitMarks(debugContext, fScale, vOffset);

  const auto& selectedPaths = m_pUiDocumentContext->GetSelectedElementPaths();

  for (const auto& path : selectedPaths)
  {
    const plUiElement* pElement = pResource->GetRoot();
    const plUiElement* pParentElement = nullptr;
    plRectFloat rect = pElement->ComputeRect(canvasRect, &layoutContext);
    plRectFloat parentRect = rect;

    bool bValid = true;
    for (plUInt32 uiIndex : path)
    {
      if (uiIndex >= pElement->m_Children.GetCount())
      {
        bValid = false;
        break;
      }

      parentRect = rect;
      pParentElement = pElement;
      pElement = pElement->m_Children[uiIndex];
      rect = plUiLayout::ComputeChildRect(pParentElement, pElement, parentRect, uiIndex, &layoutContext);
    }

    if (!bValid)
      continue;

    const bool bContainerChild = plDynamicCast<const plUiStackPanel*>(pParentElement) != nullptr || plDynamicCast<const plUiGridPanel*>(pParentElement) != nullptr;

    const plRectFloat screenRect(rect.x * fScale + vOffset.x, rect.y * fScale + vOffset.y, rect.width * fScale, rect.height * fScale);
    const plRectFloat screenParentRect(parentRect.x * fScale + vOffset.x, parentRect.y * fScale + vOffset.y, parentRect.width * fScale, parentRect.height * fScale);

    plDebugRenderer::Draw2DLineRectangle(debugContext, screenRect, 0.0f, selectionColor);

    // corner + edge handles (visual affordance for the resize zones the editor-side mouse handling uses)
    constexpr float fHandleSize = 7.0f;
    const float xs[3] = {screenRect.x, screenRect.x + screenRect.width * 0.5f, screenRect.x + screenRect.width};
    const float ys[3] = {screenRect.y, screenRect.y + screenRect.height * 0.5f, screenRect.y + screenRect.height};

    for (plUInt32 row = 0; row < 3; ++row)
    {
      for (plUInt32 col = 0; col < 3; ++col)
      {
        if (row == 1 && col == 1)
          continue; // no center handle

        const plRectFloat handleRect(xs[col] - fHandleSize * 0.5f, ys[row] - fHandleSize * 0.5f, fHandleSize, fHandleSize);
        plDebugRenderer::Draw2DRectangle(debugContext, handleRect, 0.0f, selectionColor);
      }
    }

    // anchor diamonds on the parent rect + margin lines from the element to its anchor reference
    // (meaningless inside stack/grid containers, where the parent places the child)
    if (!bContainerChild)
    {
      plHybridArray<plDebugRenderer::Line, 24> lines;

      auto addDiamond = [&](float x, float y)
      {
        constexpr float fSize = 6.0f;
        lines.PushBack(plDebugRenderer::Line(plVec3(x - fSize, y, 0), plVec3(x, y - fSize, 0)));
        lines.PushBack(plDebugRenderer::Line(plVec3(x, y - fSize, 0), plVec3(x + fSize, y, 0)));
        lines.PushBack(plDebugRenderer::Line(plVec3(x + fSize, y, 0), plVec3(x, y + fSize, 0)));
        lines.PushBack(plDebugRenderer::Line(plVec3(x, y + fSize, 0), plVec3(x - fSize, y, 0)));
      };

      const float fAnchorMinX = screenParentRect.x + pElement->m_vAnchorMin.x * screenParentRect.width;
      const float fAnchorMaxX = screenParentRect.x + pElement->m_vAnchorMax.x * screenParentRect.width;
      const float fAnchorMinY = screenParentRect.y + pElement->m_vAnchorMin.y * screenParentRect.height;
      const float fAnchorMaxY = screenParentRect.y + pElement->m_vAnchorMax.y * screenParentRect.height;

      addDiamond(fAnchorMinX, fAnchorMinY);
      addDiamond(fAnchorMaxX, fAnchorMinY);
      addDiamond(fAnchorMinX, fAnchorMaxY);
      addDiamond(fAnchorMaxX, fAnchorMaxY);

      // margin lines: element edge midpoints to the anchor reference along each axis,
      // labeled with the CANVAS-space distance (mockup's "64px" measurements)
      const float fMidY = screenRect.y + screenRect.height * 0.5f;
      const float fMidX = screenRect.x + screenRect.width * 0.5f;

      const plColor labelColor(0.55f, 0.55f, 0.55f);

      auto addMarginLabel = [&](float fScreenDist, float x, float y)
      {
        const plInt32 iCanvasPx = static_cast<plInt32>(plMath::Round(fScreenDist / fScale));
        plDebugRenderer::Draw2DText(debugContext, plFmt("{}px", iCanvasPx), plVec2I32(static_cast<plInt32>(x), static_cast<plInt32>(y)), labelColor, 12, plDebugTextHAlign::Center, plDebugTextVAlign::Bottom);
      };

      if (plMath::Abs(screenRect.x - fAnchorMinX) > 2.0f)
      {
        lines.PushBack(plDebugRenderer::Line(plVec3(screenRect.x, fMidY, 0), plVec3(fAnchorMinX, fMidY, 0)));
        addMarginLabel(plMath::Abs(screenRect.x - fAnchorMinX), (screenRect.x + fAnchorMinX) * 0.5f, fMidY - 2.0f);
      }

      if (plMath::Abs((screenRect.x + screenRect.width) - fAnchorMaxX) > 2.0f)
      {
        lines.PushBack(plDebugRenderer::Line(plVec3(screenRect.x + screenRect.width, fMidY, 0), plVec3(fAnchorMaxX, fMidY, 0)));
        addMarginLabel(plMath::Abs((screenRect.x + screenRect.width) - fAnchorMaxX), (screenRect.x + screenRect.width + fAnchorMaxX) * 0.5f, fMidY - 2.0f);
      }

      if (plMath::Abs(screenRect.y - fAnchorMinY) > 2.0f)
      {
        lines.PushBack(plDebugRenderer::Line(plVec3(fMidX, screenRect.y, 0), plVec3(fMidX, fAnchorMinY, 0)));
        addMarginLabel(plMath::Abs(screenRect.y - fAnchorMinY), fMidX, (screenRect.y + fAnchorMinY) * 0.5f);
      }

      if (plMath::Abs((screenRect.y + screenRect.height) - fAnchorMaxY) > 2.0f)
      {
        lines.PushBack(plDebugRenderer::Line(plVec3(fMidX, screenRect.y + screenRect.height, 0), plVec3(fMidX, fAnchorMaxY, 0)));
        addMarginLabel(plMath::Abs((screenRect.y + screenRect.height) - fAnchorMaxY), fMidX, (screenRect.y + screenRect.height + fAnchorMaxY) * 0.5f);
      }

      plDebugRenderer::Draw2DLines(debugContext, lines, selectionColor);
    }
  }
}

void plUiDocumentViewContext::DrawTextFitMarks(const plDebugRendererContext& context, float fScale, const plVec2& vOffset) const
{
  // the danger colour of the editor theme (the Text fit list uses it for the same issues)
  const plColor markColor = plColorGammaUB(0xD7, 0x82, 0x78);
  constexpr float fRadius = 3.5f;
  constexpr float fInset = 3.0f;

  for (const plUiTextFitIssue& issue : m_pUiDocumentContext->GetTextFitIssues())
  {
    if (issue.m_Kind == plUiTextFitIssue::Kind::Shrunk)
      continue;

    const plVec2 vCenter((issue.m_Rect.x + issue.m_Rect.width) * fScale + vOffset.x - fInset - fRadius, issue.m_Rect.y * fScale + vOffset.y + fInset + fRadius);

    // a disc from one-pixel rows
    for (float fY = -fRadius; fY <= fRadius; fY += 1.0f)
    {
      const float fHalf = plMath::Sqrt(plMath::Max(fRadius * fRadius - fY * fY, 0.0f));
      plDebugRenderer::Draw2DRectangle(context, plRectFloat(vCenter.x - fHalf, vCenter.y + fY, fHalf * 2.0f, 1.0f), 0.0f, markColor);
    }
  }
}

plViewHandle plUiDocumentViewContext::CreateView()
{
  plView* pView = nullptr;
  plRenderWorld::CreateView("UI Document Editor - View", pView);
  pView->SetCameraUsageHint(plCameraUsageHint::EditorView);

  pView->SetRenderPipelineResource(CreateDefaultRenderPipeline());

  plEngineProcessDocumentContext* pDocumentContext = GetDocumentContext();
  pView->SetWorld(pDocumentContext->GetWorld());
  pView->SetCamera(&m_Camera);
  return pView->GetHandle();
}
