#include <GameUiPlugin/GameUiPluginPCH.h>

#include <Foundation/Utilities/AssetFileHeader.h>
#include <GameUiPlugin/Resources/UiFontResource.h>
#include <RendererFoundation/Device/Device.h>

static plTypeVersion s_UiFontDescVersion = 1;

plResult plUiFontResourceDescriptor::Save(plStreamWriter& inout_stream) const
{
  inout_stream.WriteVersion(s_UiFontDescVersion);

  inout_stream << m_fBaseSize;
  inout_stream << m_fAscent;
  inout_stream << m_fDescent;
  inout_stream << m_fLineGap;
  inout_stream << m_uiAtlasWidth;
  inout_stream << m_uiAtlasHeight;

  PL_SUCCEED_OR_RETURN(inout_stream.WriteArray(m_AtlasData));
  PL_SUCCEED_OR_RETURN(inout_stream.WriteArray(m_Glyphs));
  PL_SUCCEED_OR_RETURN(inout_stream.WriteHashTable(m_Kerning));

  return PL_SUCCESS;
}

plResult plUiFontResourceDescriptor::Load(plStreamReader& inout_stream)
{
  const plTypeVersion uiVersion = inout_stream.ReadVersion(s_UiFontDescVersion);
  PL_IGNORE_UNUSED(uiVersion);

  inout_stream >> m_fBaseSize;
  inout_stream >> m_fAscent;
  inout_stream >> m_fDescent;
  inout_stream >> m_fLineGap;
  inout_stream >> m_uiAtlasWidth;
  inout_stream >> m_uiAtlasHeight;

  PL_SUCCEED_OR_RETURN(inout_stream.ReadArray(m_AtlasData));
  PL_SUCCEED_OR_RETURN(inout_stream.ReadArray(m_Glyphs));
  PL_SUCCEED_OR_RETURN(inout_stream.ReadHashTable(m_Kerning));

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiFontResource, 1, plRTTIDefaultAllocator<plUiFontResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plUiFontResource);
// clang-format on

plUiFontResource::plUiFontResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

plUiFontResource::~plUiFontResource()
{
  PL_ASSERT_DEV(m_hAtlasTexture.IsInvalidated(), "Atlas texture has not been cleaned up properly");
}

const plUiFontGlyph* plUiFontResource::GetGlyph(plUInt32 uiCodepoint) const
{
  plUInt32 uiIndex = 0;
  if (m_GlyphLookup.TryGetValue(uiCodepoint, uiIndex))
  {
    return &m_Descriptor.m_Glyphs[uiIndex];
  }

  return nullptr;
}

float plUiFontResource::GetKerning(plUInt32 uiFirst, plUInt32 uiSecond) const
{
  float fKerning = 0.0f;
  m_Descriptor.m_Kerning.TryGetValue((static_cast<plUInt64>(uiFirst) << 32) | uiSecond, fKerning);
  return fKerning;
}

plResourceLoadDesc plUiFontResource::UnloadData(Unload WhatToUnload)
{
  DestroyAtlasTexture();

  m_Descriptor = plUiFontResourceDescriptor();
  m_GlyphLookup.Clear();

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Unloaded;

  return res;
}

plResourceLoadDesc plUiFontResource::UpdateContent(plStreamReader* Stream)
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

  plUiFontResourceDescriptor desc;
  if (desc.Load(*Stream).Failed())
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  return CreateResource(std::move(desc));
}

void plUiFontResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryCPU = sizeof(*this) + m_Descriptor.m_AtlasData.GetCount() + m_Descriptor.m_Glyphs.GetCount() * sizeof(plUiFontGlyph);
  out_NewMemoryUsage.m_uiMemoryGPU = m_Descriptor.m_AtlasData.GetCount();
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plUiFontResource, plUiFontResourceDescriptor)
{
  DestroyAtlasTexture();

  m_Descriptor = std::move(descriptor);

  m_GlyphLookup.Clear();
  m_GlyphLookup.Reserve(m_Descriptor.m_Glyphs.GetCount());
  for (plUInt32 i = 0; i < m_Descriptor.m_Glyphs.GetCount(); ++i)
  {
    m_GlyphLookup.Insert(m_Descriptor.m_Glyphs[i].m_uiCodepoint, i);
  }

  CreateAtlasTexture();

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Loaded;

  return res;
}

void plUiFontResource::CreateAtlasTexture()
{
  if (m_Descriptor.m_uiAtlasWidth == 0 || m_Descriptor.m_uiAtlasHeight == 0 || m_Descriptor.m_AtlasData.IsEmpty())
    return;

  plGALTextureCreationDescription desc;
  desc.m_uiWidth = m_Descriptor.m_uiAtlasWidth;
  desc.m_uiHeight = m_Descriptor.m_uiAtlasHeight;
  desc.m_Format = plGALResourceFormat::RUByteNormalized;

  plGALSystemMemoryDescription initialData;
  initialData.m_pData = plMakeByteBlobPtr(m_Descriptor.m_AtlasData.GetData(), m_Descriptor.m_AtlasData.GetCount());
  initialData.m_uiRowPitch = m_Descriptor.m_uiAtlasWidth;
  initialData.m_uiSlicePitch = m_Descriptor.m_uiAtlasWidth * m_Descriptor.m_uiAtlasHeight;

  m_hAtlasTexture = plGALDevice::GetDefaultDevice()->CreateTexture(desc, plMakeArrayPtr(&initialData, 1));
}

void plUiFontResource::DestroyAtlasTexture()
{
  if (!m_hAtlasTexture.IsInvalidated())
  {
    plGALDevice::GetDefaultDevice()->DestroyTexture(m_hAtlasTexture);
    m_hAtlasTexture.Invalidate();
  }
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Resources_Implementation_UiFontResource);
