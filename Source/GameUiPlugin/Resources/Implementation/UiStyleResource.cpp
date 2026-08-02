#include <GameUiPlugin/GameUiPluginPCH.h>

#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>

// version 1 stored a LIST of style classes; that format never shipped and is not readable
// anymore — v1 files load as missing and re-transform on the next asset save
static plTypeVersion s_UiStyleDescVersion = 2;

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiStyle, 1, plRTTIDefaultAllocator<plUiStyle>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Color", m_Color)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(255, 255, 255, 255)), new plGroupAttribute("Brush")),
    PL_ENUM_MEMBER_PROPERTY("BrushMode", plUiBrushMode, m_BrushMode)->AddAttributes(new plGroupAttribute("Brush")),
    PL_RESOURCE_MEMBER_PROPERTY("Texture", m_hTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("SliceMargins", m_vSliceMargins)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\x80;\xE2\x96\xB2;\xE2\x96\xB6;\xE2\x96\xBC"), new plDefaultValueAttribute(plVec4(16.0f)), new plSuffixAttribute("px"), new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("CornerRadius", m_vCornerRadius)->AddAttributes(new plVectorLabelsAttribute("\xE2\x97\xA4;\xE2\x97\xA5;\xE2\x97\xA2;\xE2\x97\xA3"), new plClampValueAttribute(plVec4(0.0f), plVec4(1000.0f)), new plSuffixAttribute("px"), new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("BorderWidth", m_fBorderWidth)->AddAttributes(new plClampValueAttribute(0.0f, 100.0f), new plSuffixAttribute("px"), new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("BorderColor", m_BorderColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(255, 255, 255, 255)), new plGroupAttribute("Brush")),
    PL_ENUM_MEMBER_PROPERTY("GradientMode", plUiGradientMode, m_GradientMode)->AddAttributes(new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("GradientColor", m_GradientColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(128, 128, 128, 255)), new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("ShadowSize", m_fShadowSize)->AddAttributes(new plClampValueAttribute(0.0f, 200.0f), new plSuffixAttribute("px"), new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("ShadowOffset", m_vShadowOffset)->AddAttributes(new plVectorLabelsAttribute("\xE2\x96\xB6;\xE2\x96\xBC"), new plDefaultValueAttribute(plVec2(0.0f, 4.0f)), new plSuffixAttribute("px"), new plGroupAttribute("Brush")),
    PL_MEMBER_PROPERTY("ShadowColor", m_ShadowColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(0, 0, 0, 150)), new plGroupAttribute("Brush")),

    PL_MEMBER_PROPERTY("HoverColor", m_HoverColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(230, 230, 230, 255)), new plGroupAttribute("Interaction")),
    PL_MEMBER_PROPERTY("PressedColor", m_PressedColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(170, 170, 170, 255)), new plGroupAttribute("Interaction")),
    PL_MEMBER_PROPERTY("DisabledColor", m_DisabledColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(110, 110, 110, 200)), new plGroupAttribute("Interaction")),
    PL_RESOURCE_MEMBER_PROPERTY("HoverTexture", m_hHoverTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Interaction")),
    PL_RESOURCE_MEMBER_PROPERTY("PressedTexture", m_hPressedTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Interaction")),
    PL_RESOURCE_MEMBER_PROPERTY("DisabledTexture", m_hDisabledTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Interaction")),
    PL_MEMBER_PROPERTY("CheckedColor", m_CheckedColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(120, 190, 120, 255)), new plGroupAttribute("Interaction")),
    PL_RESOURCE_MEMBER_PROPERTY("CheckedTexture", m_hCheckedTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Interaction")),
    PL_MEMBER_PROPERTY("TransitionDuration", m_fTransitionDuration)->AddAttributes(new plDefaultValueAttribute(0.12f), new plClampValueAttribute(0.0f, 10.0f), new plSuffixAttribute(" sec"), new plGroupAttribute("Interaction")),
    PL_ENUM_MEMBER_PROPERTY("TransitionEasing", plUiAnimEasing, m_TransitionEasing)->AddAttributes(new plGroupAttribute("Interaction")),

    PL_RESOURCE_MEMBER_PROPERTY("Font", m_hFont)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Font"), new plGroupAttribute("Text")),
    PL_MEMBER_PROPERTY("TextColor", m_TextColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(255, 255, 255, 255)), new plGroupAttribute("Text")),
    PL_MEMBER_PROPERTY("FontSize", m_uiFontSize)->AddAttributes(new plDefaultValueAttribute(16), new plClampValueAttribute(4, 256), new plGroupAttribute("Text")),

    PL_MEMBER_PROPERTY("FillColor", m_FillColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(220, 80, 70, 255)), new plGroupAttribute("Fill")),
    PL_RESOURCE_MEMBER_PROPERTY("FillTexture", m_hFillTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Fill")),
    PL_MEMBER_PROPERTY("HandleColor", m_HandleColor)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColorGammaUB(240, 240, 240, 255)), new plGroupAttribute("Fill")),
    PL_RESOURCE_MEMBER_PROPERTY("HandleTexture", m_hHandleTexture)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Texture_2D"), new plGroupAttribute("Fill")),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiStyleResource, 1, plRTTIDefaultAllocator<plUiStyleResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plUiStyleResource);
// clang-format on

plUiStyle::plUiStyle() = default;
plUiStyle::~plUiStyle() = default;

//////////////////////////////////////////////////////////////////////////

plUiStyleResourceDescriptor::plUiStyleResourceDescriptor() = default;

plUiStyleResourceDescriptor::plUiStyleResourceDescriptor(plUiStyleResourceDescriptor&& other)
{
  *this = std::move(other);
}

plUiStyleResourceDescriptor::~plUiStyleResourceDescriptor()
{
  Clear();
}

void plUiStyleResourceDescriptor::operator=(plUiStyleResourceDescriptor&& rhs)
{
  Clear();

  m_pStyle = rhs.m_pStyle;
  rhs.m_pStyle = nullptr;
}

void plUiStyleResourceDescriptor::Clear()
{
  if (m_pStyle != nullptr)
  {
    PL_DEFAULT_DELETE(m_pStyle);
    m_pStyle = nullptr;
  }
}

plResult plUiStyleResourceDescriptor::Save(plStreamWriter& inout_stream)
{
  inout_stream.WriteVersion(s_UiStyleDescVersion);

  const bool bHasStyle = m_pStyle != nullptr;
  inout_stream << bHasStyle;

  if (bHasStyle)
  {
    plReflectionSerializer::WriteObjectToBinary(inout_stream, m_pStyle->GetDynamicRTTI(), m_pStyle);
  }

  return PL_SUCCESS;
}

plResult plUiStyleResourceDescriptor::Load(plStreamReader& inout_stream)
{
  Clear();

  const plTypeVersion uiVersion = inout_stream.ReadVersion(s_UiStyleDescVersion);

  if (uiVersion < 2)
  {
    return PL_FAILURE; // the class-list format never shipped; re-transform the asset
  }

  bool bHasStyle = false;
  inout_stream >> bHasStyle;

  if (bHasStyle)
  {
    const plRTTI* pRtti = nullptr;
    void* pObject = plReflectionSerializer::ReadObjectFromBinary(inout_stream, pRtti);

    if (pObject == nullptr || pRtti == nullptr || !pRtti->IsDerivedFrom<plUiStyle>())
    {
      return PL_FAILURE;
    }

    m_pStyle = static_cast<plUiStyle*>(pObject);
  }

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

plUiStyleResource::plUiStyleResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

plUiStyleResource::~plUiStyleResource() = default;

plResourceLoadDesc plUiStyleResource::UnloadData(Unload WhatToUnload)
{
  m_Descriptor.Clear();

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Unloaded;

  return res;
}

plResourceLoadDesc plUiStyleResource::UpdateContent(plStreamReader* Stream)
{
  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;

  if (Stream == nullptr)
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  // skip the absolute file path data that the standard file reader writes into the stream
  {
    plStringBuilder sAbsFilePath;
    (*Stream) >> sAbsFilePath;
  }

  plAssetFileHeader assetHeader;
  assetHeader.Read(*Stream).IgnoreResult();

  plUiStyleResourceDescriptor desc;
  if (desc.Load(*Stream).Failed())
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  return CreateResource(std::move(desc));
}

void plUiStyleResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryCPU = sizeof(*this);
  out_NewMemoryUsage.m_uiMemoryGPU = 0;
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plUiStyleResource, plUiStyleResourceDescriptor)
{
  m_Descriptor = std::move(descriptor);

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Loaded;

  return res;
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Resources_Implementation_UiStyleResource);
