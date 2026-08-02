#pragma once

#include <GameUiPlugin/GameUiPluginDLL.h>

#include <Core/ResourceManager/ResourceHandle.h>
#include <RendererCore/Pipeline/Renderer.h>
#include <RendererCore/Textures/Texture2DResource.h>
#include <RendererFoundation/Resources/BufferPool.h>

using plShaderResourceHandle = plTypedResourceHandle<class plShaderResource>;

/// \brief Renders plUiRenderData (batched UI quads) directly into the target of the GUI render phase.
class PL_GAMEUIPLUGIN_DLL plUiRenderer : public plRenderer
{
  PL_ADD_DYNAMIC_REFLECTION(plUiRenderer, plRenderer);
  PL_DISALLOW_COPY_AND_ASSIGN(plUiRenderer);

public:
  plUiRenderer();
  ~plUiRenderer();

  virtual void GetSupportedRenderDataTypes(plHybridArray<const plRTTI*, 8>& ref_types) const override;
  virtual void GetSupportedRenderDataCategories(plHybridArray<plRenderData::Category, 8>& ref_categories) const override;
  virtual void RenderBatch(const plRenderViewContext& renderViewContext, const plRenderPipelinePass* pPass, const plRenderDataBatch& batch) const override;

private:
  void SetupRenderer();

  static constexpr plUInt32 s_uiVertexBufferSize = 1024 * 64;
  static constexpr plUInt32 s_uiIndexBufferSize = s_uiVertexBufferSize * 2;

  plShaderResourceHandle m_hShader;
  plShaderResourceHandle m_hTextShader;
  plTexture2DResourceHandle m_hWhiteTexture;
  plGALBufferPool m_VertexBuffer;
  plGALBufferPool m_IndexBuffer;
  plSmallArray<plGALVertexAttribute, 8> m_VertexAttributes;
};
