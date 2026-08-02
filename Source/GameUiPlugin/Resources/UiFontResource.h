#pragma once

#include <Core/ResourceManager/Resource.h>
#include <Foundation/Containers/HashTable.h>
#include <GameUiPlugin/GameUiPluginDLL.h>
#include <RendererFoundation/RendererFoundationDLL.h>

/// \brief One baked SDF glyph. All pixel values are at the font's base size.
struct plUiFontGlyph
{
  PL_DECLARE_POD_TYPE();

  plUInt32 m_uiCodepoint = 0;
  plVec2U32 m_vAtlasPos = plVec2U32::MakeZero();  ///< top-left in the atlas, in pixels
  plVec2U32 m_vAtlasSize = plVec2U32::MakeZero(); ///< bitmap size in pixels
  plVec2 m_vBearing = plVec2::MakeZero();         ///< x: pen to left edge; y: baseline to top edge (positive up)
  float m_fAdvance = 0.0f;
};

inline plStreamWriter& operator<<(plStreamWriter& inout_stream, const plUiFontGlyph& glyph)
{
  inout_stream << glyph.m_uiCodepoint;
  inout_stream << glyph.m_vAtlasPos;
  inout_stream << glyph.m_vAtlasSize;
  inout_stream << glyph.m_vBearing;
  inout_stream << glyph.m_fAdvance;
  return inout_stream;
}

inline plStreamReader& operator>>(plStreamReader& inout_stream, plUiFontGlyph& ref_glyph)
{
  inout_stream >> ref_glyph.m_uiCodepoint;
  inout_stream >> ref_glyph.m_vAtlasPos;
  inout_stream >> ref_glyph.m_vAtlasSize;
  inout_stream >> ref_glyph.m_vBearing;
  inout_stream >> ref_glyph.m_fAdvance;
  return inout_stream;
}

/// \brief Compiled data of a UI Font asset: SDF glyph atlas + metrics.
struct PL_GAMEUIPLUGIN_DLL plUiFontResourceDescriptor
{
  plResult Save(plStreamWriter& inout_stream) const;
  plResult Load(plStreamReader& inout_stream);

  float m_fBaseSize = 48.0f; ///< pixel size the glyphs were baked at
  float m_fAscent = 0.0f;    ///< baseline to top of line, at base size (positive)
  float m_fDescent = 0.0f;   ///< baseline to bottom of line, at base size (negative)
  float m_fLineGap = 0.0f;

  plUInt32 m_uiAtlasWidth = 0;
  plUInt32 m_uiAtlasHeight = 0;
  plDynamicArray<plUInt8> m_AtlasData; ///< R8 SDF bitmap, 128 = glyph edge

  plDynamicArray<plUiFontGlyph> m_Glyphs;

  /// key = (first codepoint << 32) | second codepoint, value = kerning in pixels at base size
  plHashTable<plUInt64, float> m_Kerning;
};

using plUiFontResourceHandle = plTypedResourceHandle<class plUiFontResource>;

/// \brief Resource wrapping a baked SDF font (glyph atlas texture + metrics).
class PL_GAMEUIPLUGIN_DLL plUiFontResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plUiFontResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plUiFontResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plUiFontResource, plUiFontResourceDescriptor);

public:
  plUiFontResource();
  ~plUiFontResource();

  bool HasGlyphs() const { return !m_Descriptor.m_Glyphs.IsEmpty() && !m_hAtlasTexture.IsInvalidated(); }

  const plUiFontGlyph* GetGlyph(plUInt32 uiCodepoint) const;
  float GetKerning(plUInt32 uiFirst, plUInt32 uiSecond) const;

  float GetBaseSize() const { return m_Descriptor.m_fBaseSize; }
  float GetAscent() const { return m_Descriptor.m_fAscent; }
  float GetDescent() const { return m_Descriptor.m_fDescent; }
  float GetLineHeight() const { return m_Descriptor.m_fAscent - m_Descriptor.m_fDescent + m_Descriptor.m_fLineGap; }

  plUInt32 GetAtlasWidth() const { return m_Descriptor.m_uiAtlasWidth; }
  plUInt32 GetAtlasHeight() const { return m_Descriptor.m_uiAtlasHeight; }
  plGALTextureHandle GetAtlasTexture() const { return m_hAtlasTexture; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* Stream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  void CreateAtlasTexture();
  void DestroyAtlasTexture();

  plUiFontResourceDescriptor m_Descriptor;
  plHashTable<plUInt32, plUInt32> m_GlyphLookup; ///< codepoint -> index into m_Glyphs
  plGALTextureHandle m_hAtlasTexture;
};
