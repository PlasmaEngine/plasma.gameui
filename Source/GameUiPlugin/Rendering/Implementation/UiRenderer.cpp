#include <GameUiPlugin/GameUiPluginPCH.h>

#include <GameUiPlugin/Rendering/UiRenderData.h>
#include <GameUiPlugin/Rendering/UiRenderer.h>

#include <RendererCore/Pipeline/RenderDataBatch.h>
#include <RendererCore/Pipeline/ViewData.h>
#include <RendererCore/RenderContext/RenderContext.h>
#include <RendererCore/Shader/ShaderResource.h>
#include <RendererFoundation/Device/Device.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiRenderData, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiRenderer, 1, plRTTIDefaultAllocator<plUiRenderer>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiRenderer::plUiRenderer()
{
  SetupRenderer();
}

plUiRenderer::~plUiRenderer()
{
  m_hShader.Invalidate();
  m_hTextShader.Invalidate();
  m_hWhiteTexture.Invalidate();
}

void plUiRenderer::GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& ref_types) const
{
  ref_types.PushBack(plGetStaticRTTI<plUiRenderData>());
}

void plUiRenderer::GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& ref_categories) const
{
  ref_categories.PushBack(plDefaultRenderDataCategories::GUI);
}

void plUiRenderer::RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const
{
  plRenderContext* pRenderContext = renderViewContext.m_pRenderContext;
  plGALCommandEncoder* pCommandEncoder = pRenderContext->GetCommandEncoder();

  plBindGroupBuilder& bindGroup = pRenderContext->GetBindGroup();

  for (auto it = batch.GetIterator<plUiRenderData>(); it.IsValid(); ++it)
  {
    const plUiRenderData* pRenderData = it;

    if (pRenderData->m_Vertices.IsEmpty() || pRenderData->m_Indices.IsEmpty())
      continue;

    PL_ASSERT_DEV(pRenderData->m_Vertices.GetCount() < s_uiVertexBufferSize, "UI canvas has too many vertices to render in one drawcall");
    PL_ASSERT_DEV(pRenderData->m_Indices.GetCount() < s_uiIndexBufferSize, "UI canvas has too many indices to render in one drawcall");

    plGALBufferHandle hVertexBuffer = m_VertexBuffer.GetNewBuffer();
    plGALBufferHandle hIndexBuffer = m_IndexBuffer.GetNewBuffer();

    pCommandEncoder->UpdateBuffer(hVertexBuffer, 0, pRenderData->m_Vertices.ToByteArray(), plGALUpdateMode::AheadOfTime);
    pCommandEncoder->UpdateBuffer(hIndexBuffer, 0, pRenderData->m_Indices.ToByteArray(), plGALUpdateMode::AheadOfTime);

    for (const plUiBatch& uiBatch : pRenderData->m_Batches)
    {
      if (uiBatch.m_uiIndexCount == 0)
        continue;

      pCommandEncoder->SetScissorRect(uiBatch.m_ScissorRect);

      pRenderContext->BindShader(uiBatch.m_bSdfText ? m_hTextShader : m_hShader);
      pRenderContext->BindMeshBuffer(plMakeArrayPtr(&hVertexBuffer, 1), hIndexBuffer, m_VertexAttributes, plGALPrimitiveTopology::Triangles, pRenderData->m_Indices.GetCount() / 3);

      if (uiBatch.m_pTexture != nullptr && uiBatch.m_pTexture->IsValid())
      {
        bindGroup.BindTexture("BaseTexture", *uiBatch.m_pTexture, plResourceAcquireMode::AllowLoadingFallback);
      }
      else if (!uiBatch.m_hNativeTexture.IsInvalidated())
      {
        bindGroup.BindTexture("BaseTexture", uiBatch.m_hNativeTexture);
      }
      else
      {
        bindGroup.BindTexture("BaseTexture", m_hWhiteTexture, plResourceAcquireMode::AllowLoadingFallback);
      }

      pRenderContext->DrawMeshBuffer(uiBatch.m_uiIndexCount / 3, uiBatch.m_uiFirstIndex / 3).IgnoreResult();
    }
  }

  // restore the full-viewport scissor for whatever renders next (plImguiRenderer pattern)
  {
    const plRectFloat viewport = renderViewContext.m_pViewData->m_ViewPortRect;
    pCommandEncoder->SetScissorRect(plRectU32(static_cast<plUInt32>(viewport.x), static_cast<plUInt32>(viewport.y), static_cast<plUInt32>(viewport.width), static_cast<plUInt32>(viewport.height)));
  }
}

void plUiRenderer::SetupRenderer()
{
  if (m_hShader.IsValid())
    return;

  m_hShader = plResourceManager::LoadResource<plShaderResource>("Shaders/GameUi.plShader");
  m_hTextShader = plResourceManager::LoadResource<plShaderResource>("Shaders/GameUiText.plShader");
  m_hWhiteTexture = plResourceManager::LoadResource<plTexture2DResource>("White.color");

  // Create the vertex buffer
  {
    plGALBufferCreationDescription desc;
    desc.m_BufferFlags = plGALBufferUsageFlags::VertexBuffer | plGALBufferUsageFlags::Transient;

    desc.m_uiStructSize = sizeof(plUiVertex);
    desc.m_uiTotalSize = s_uiVertexBufferSize * desc.m_uiStructSize;
    desc.m_ResourceAccess.m_bImmutable = false;

    m_VertexBuffer.Initialize(desc, "GameUiRenderer - VertexBuffer");
  }

  // Create the index buffer
  {
    plGALBufferCreationDescription desc;
    desc.m_uiStructSize = sizeof(plUInt32);
    desc.m_uiTotalSize = s_uiIndexBufferSize * desc.m_uiStructSize;
    desc.m_BufferFlags = plGALBufferUsageFlags::IndexBuffer | plGALBufferUsageFlags::Transient;
    desc.m_ResourceAccess.m_bImmutable = false;

    m_IndexBuffer.Initialize(desc, "GameUiRenderer - IndexBuffer");
  }

  // Setup the vertex declaration
  {
    {
      auto& va = m_VertexAttributes.ExpandAndGetRef();
      va.m_eSemantic = plGALVertexAttributeSemantic::Position;
      va.m_eFormat = plGALResourceFormat::XYZFloat;
      va.m_uiOffset = offsetof(plUiVertex, m_Position);
    }

    {
      auto& va = m_VertexAttributes.ExpandAndGetRef();
      va.m_eSemantic = plGALVertexAttributeSemantic::TexCoord0;
      va.m_eFormat = plGALResourceFormat::UVFloat;
      va.m_uiOffset = offsetof(plUiVertex, m_TexCoord);
    }

    {
      auto& va = m_VertexAttributes.ExpandAndGetRef();
      va.m_eSemantic = plGALVertexAttributeSemantic::Color0;
      va.m_eFormat = plGALResourceFormat::RGBAUByteNormalized;
      va.m_uiOffset = offsetof(plUiVertex, m_Color);
    }

    // SDF brush parameters (rounding / borders / shadows), see plUiVertex
    {
      auto& va = m_VertexAttributes.ExpandAndGetRef();
      va.m_eSemantic = plGALVertexAttributeSemantic::TexCoord1;
      va.m_eFormat = plGALResourceFormat::XYZWFloat;
      va.m_uiOffset = offsetof(plUiVertex, m_SdfCoord);
    }

    {
      auto& va = m_VertexAttributes.ExpandAndGetRef();
      va.m_eSemantic = plGALVertexAttributeSemantic::TexCoord2;
      va.m_eFormat = plGALResourceFormat::XYZWFloat;
      va.m_uiOffset = offsetof(plUiVertex, m_SdfParams);
    }

    {
      auto& va = m_VertexAttributes.ExpandAndGetRef();
      va.m_eSemantic = plGALVertexAttributeSemantic::TexCoord3;
      va.m_eFormat = plGALResourceFormat::XYZWFloat;
      va.m_uiOffset = offsetof(plUiVertex, m_SdfRadii);
    }

    {
      auto& va = m_VertexAttributes.ExpandAndGetRef();
      va.m_eSemantic = plGALVertexAttributeSemantic::Color1;
      va.m_eFormat = plGALResourceFormat::RGBAUByteNormalized;
      va.m_uiOffset = offsetof(plUiVertex, m_ColorB);
    }
  }
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Rendering_Implementation_UiRenderer);
