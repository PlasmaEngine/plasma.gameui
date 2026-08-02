#pragma once

#include <Core/ResourceManager/Resource.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GameUiPlugin/GameUiPluginDLL.h>

/// \brief The values of a UI Style asset — ONE complete look.
///
/// An element with a Style reference takes ALL its visual values from here and its own
/// visual properties are ignored ("style replaces visuals"). Widgets consume their
/// subset: visual elements the brush block, buttons additionally the interaction block,
/// texts the text block, progress bars the fill block. A state can reference another
/// style; while active it is cross-faded over the state's enter duration.
class PL_GAMEUIPLUGIN_DLL plUiStyle : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiStyle, plReflectedClass);

public:
  plUiStyle();
  ~plUiStyle();

  // brush (all visual elements)
  plColorGammaUB m_Color = plColorGammaUB(255, 255, 255, 255);
  plEnum<plUiBrushMode> m_BrushMode;
  plTexture2DResourceHandle m_hTexture;
  plVec4 m_vSliceMargins = plVec4(16.0f);
  plVec4 m_vCornerRadius = plVec4::MakeZero(); ///< px (TL, TR, BR, BL)
  float m_fBorderWidth = 0.0f;
  plColorGammaUB m_BorderColor = plColorGammaUB(255, 255, 255, 255);
  plEnum<plUiGradientMode> m_GradientMode;
  plColorGammaUB m_GradientColor = plColorGammaUB(128, 128, 128, 255);
  float m_fShadowSize = 0.0f;
  plVec2 m_vShadowOffset = plVec2(0.0f, 4.0f);
  plColorGammaUB m_ShadowColor = plColorGammaUB(0, 0, 0, 150);

  // interaction states (buttons / toggles)
  plColorGammaUB m_HoverColor = plColorGammaUB(230, 230, 230, 255);
  plColorGammaUB m_PressedColor = plColorGammaUB(170, 170, 170, 255);
  plColorGammaUB m_DisabledColor = plColorGammaUB(110, 110, 110, 200);
  plColorGammaUB m_CheckedColor = plColorGammaUB(120, 190, 120, 255);
  plTexture2DResourceHandle m_hHoverTexture;
  plTexture2DResourceHandle m_hPressedTexture;
  plTexture2DResourceHandle m_hDisabledTexture;
  plTexture2DResourceHandle m_hCheckedTexture;
  float m_fTransitionDuration = 0.12f; ///< seconds to blend between interaction states
  plEnum<plUiAnimEasing> m_TransitionEasing;

  // text (plUiText)
  plUiFontResourceHandle m_hFont;
  plColorGammaUB m_TextColor = plColorGammaUB(255, 255, 255, 255);
  plUInt32 m_uiFontSize = 16;

  // fill (progress bars / sliders)
  plColorGammaUB m_FillColor = plColorGammaUB(220, 80, 70, 255);
  plTexture2DResourceHandle m_hFillTexture;
  plColorGammaUB m_HandleColor = plColorGammaUB(240, 240, 240, 255);
  plTexture2DResourceHandle m_hHandleTexture;
};

/// \brief Compiled data of a UI Style asset: one style.
struct PL_GAMEUIPLUGIN_DLL plUiStyleResourceDescriptor
{
  plUiStyleResourceDescriptor();
  plUiStyleResourceDescriptor(plUiStyleResourceDescriptor&& other);
  ~plUiStyleResourceDescriptor();

  void operator=(plUiStyleResourceDescriptor&& rhs);

  plResult Save(plStreamWriter& inout_stream);
  plResult Load(plStreamReader& inout_stream);

  void Clear();

  plUiStyle* m_pStyle = nullptr; ///< owned
};

using plUiStyleResourceHandle = plTypedResourceHandle<class plUiStyleResource>;

/// \brief Resource wrapping a compiled UI Style (one look).
///
/// A missing/empty style degrades gracefully: GetStyle returns nullptr and elements
/// render with their locally authored visuals.
class PL_GAMEUIPLUGIN_DLL plUiStyleResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plUiStyleResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plUiStyleResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plUiStyleResource, plUiStyleResourceDescriptor);

public:
  plUiStyleResource();
  ~plUiStyleResource();

  /// \brief The style values, or nullptr while not loaded.
  const plUiStyle* GetStyle() const { return m_Descriptor.m_pStyle; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* Stream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  plUiStyleResourceDescriptor m_Descriptor;
};
