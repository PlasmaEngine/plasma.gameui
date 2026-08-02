#pragma once

#include <GameUiPlugin/GameUiPluginDLL.h>

#include <Foundation/Math/Rect.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <RendererCore/Textures/Texture2DResource.h>

struct plUiVertex
{
  PL_DECLARE_POD_TYPE();

  plVec3 m_Position;
  plVec2 m_TexCoord;
  plColorLinearUB m_Color;

  // SDF brush parameters, constant across each quad (interpolation is exact).
  // m_SdfCoord: localU, localV (0..1 across the quad), mode (0 plain / 1 fill / 2 border / 3 shadow), softness px
  // m_SdfParams: halfWidth px, halfHeight px, borderWidth px, unused
  // m_SdfRadii: corner radii px (TL, TR, BR, BL)
  plVec4 m_SdfCoord;
  plVec4 m_SdfParams;
  plVec4 m_SdfRadii;
  plColorLinearUB m_ColorB; ///< border color (mode 2)
};

/// \brief One draw call of a UI canvas: a consecutive index range sharing one texture.
///
/// \note m_pTexture points into the element tree of the (loaded and pinned) UI document resource,
/// which outlives the frame. It is a raw pointer on purpose: batches live in the frame allocator,
/// whose memory is reset without running destructors, so no ref-counted handles may live here.
/// plGALTextureHandle is a plain handle (not ref-counted), so it may be stored by value.
struct plUiBatch
{
  PL_DECLARE_POD_TYPE();

  plUInt32 m_uiFirstIndex;
  plUInt32 m_uiIndexCount;
  const plTexture2DResourceHandle* m_pTexture; ///< nullptr = untextured (white) or native texture / text
  plGALTextureHandle m_hNativeTexture;         ///< used when m_pTexture is nullptr and this is valid (e.g. font atlas)
  bool m_bSdfText;                             ///< render with the SDF text shader
  plRectU32 m_ScissorRect;                     ///< screen-space clip; the full viewport when unclipped
};

class PL_GAMEUIPLUGIN_DLL plUiRenderData : public plRenderData
{
  PL_ADD_DYNAMIC_REFLECTION(plUiRenderData, plRenderData);

public:
  plArrayPtr<plUiVertex> m_Vertices;
  plArrayPtr<plUInt32> m_Indices;
  plArrayPtr<plUiBatch> m_Batches;
};
