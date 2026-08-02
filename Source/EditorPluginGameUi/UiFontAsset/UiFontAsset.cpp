#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiFontAsset/UiFontAsset.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <GameUiPlugin/Resources/UiFontResource.h>
#include <Texture/Utils/TexturePacker.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiFontAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiFontAssetDocument::plUiFontAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plUiFontAssetProperties>(sDocumentPath, plAssetDocEngineConnection::None)
{
}

namespace
{
  struct plUiFontBakeGlyph
  {
    plUInt32 m_uiCodepoint = 0;
    plUInt32 m_uiGlyphIndex = 0;
    plVec2 m_vBearing = plVec2::MakeZero();
    float m_fAdvance = 0.0f;
    plUInt32 m_uiWidth = 0;
    plUInt32 m_uiHeight = 0;
    plDynamicArray<plUInt8> m_Bitmap;
  };

  void CollectCodepoints(const plUiFontAssetProperties* pProp, plDynamicArray<plUInt32>& out_codepoints)
  {
    plHashSet<plUInt32> unique;

    auto addRange = [&](plUInt32 uiFirst, plUInt32 uiLast)
    {
      for (plUInt32 cp = uiFirst; cp <= uiLast; ++cp)
      {
        unique.Insert(cp);
      }
    };

    if (pProp->m_bLatin)
      addRange(0x20, 0x7E);
    if (pProp->m_bLatinExtended)
      addRange(0xA0, 0x17F);
    if (pProp->m_bGreek)
      addRange(0x370, 0x3FF);
    if (pProp->m_bCyrillic)
      addRange(0x400, 0x4FF);

    for (auto it = pProp->m_sCustomCharacters.GetIteratorFront(); it.IsValid(); ++it)
    {
      unique.Insert(it.GetCharacter());
    }

    for (plUInt32 cp : unique)
    {
      out_codepoints.PushBack(cp);
    }

    out_codepoints.Sort();
  }

  plStatus BakeFont(const plUiFontAssetProperties* pProp, plUiFontResourceDescriptor& out_desc)
  {
    if (pProp->m_sFontFile.IsEmpty())
    {
      return plStatus("No font file specified");
    }

    // read the font file into memory
    plDynamicArray<plUInt8> fontData;
    {
      plFileReader reader;
      if (reader.Open(pProp->m_sFontFile).Failed())
      {
        return plStatus(plFmt("Failed to open font file '{}'", pProp->m_sFontFile));
      }

      fontData.SetCountUninitialized(static_cast<plUInt32>(reader.GetFileSize()));
      reader.ReadBytes(fontData.GetData(), fontData.GetCount());
    }

    FT_Library ftLibrary = nullptr;
    if (FT_Init_FreeType(&ftLibrary) != 0)
    {
      return plStatus("Failed to initialize FreeType");
    }

    PL_SCOPE_EXIT(FT_Done_FreeType(ftLibrary));

    // widen the SDF gradient band (default spread is 8): a larger spread lets bilinear
    // sampling reconstruct the glyph contour smoothly under strong magnification —
    // the narrow default band shows lumpy 'stair' edges when text is scaled up
    {
      FT_Int iSpread = 16;
      FT_Property_Set(ftLibrary, "sdf", "spread", &iSpread); // best effort: older modules ignore it
    }

    FT_Face face = nullptr;
    if (FT_New_Memory_Face(ftLibrary, fontData.GetData(), fontData.GetCount(), 0, &face) != 0)
    {
      return plStatus(plFmt("Failed to load font face from '{}'", pProp->m_sFontFile));
    }

    PL_SCOPE_EXIT(FT_Done_Face(face));

    if (FT_Set_Pixel_Sizes(face, 0, pProp->m_uiBaseSize) != 0)
    {
      return plStatus("Failed to set font pixel size");
    }

    out_desc.m_fBaseSize = static_cast<float>(pProp->m_uiBaseSize);
    out_desc.m_fAscent = face->size->metrics.ascender / 64.0f;
    out_desc.m_fDescent = face->size->metrics.descender / 64.0f;
    out_desc.m_fLineGap = (face->size->metrics.height / 64.0f) - (out_desc.m_fAscent - out_desc.m_fDescent);

    plDynamicArray<plUInt32> codepoints;
    CollectCodepoints(pProp, codepoints);

    if (codepoints.IsEmpty())
    {
      return plStatus("No characters selected to bake");
    }

    // render all glyphs to SDF bitmaps
    plDeque<plUiFontBakeGlyph> glyphs;
    bool bSdfUnsupported = false;

    for (plUInt32 cp : codepoints)
    {
      const plUInt32 uiGlyphIndex = FT_Get_Char_Index(face, cp);
      if (uiGlyphIndex == 0)
        continue; // not present in this font

      if (FT_Load_Glyph(face, uiGlyphIndex, FT_LOAD_DEFAULT) != 0)
        continue;

      if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF) != 0)
      {
        // fall back to a regular AA bitmap; text stays readable but won't scale as crisply
        bSdfUnsupported = true;

        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
          continue;
      }

      const FT_Bitmap& bitmap = face->glyph->bitmap;

      plUiFontBakeGlyph& glyph = glyphs.ExpandAndGetRef();
      glyph.m_uiCodepoint = cp;
      glyph.m_uiGlyphIndex = uiGlyphIndex;
      glyph.m_vBearing.Set(static_cast<float>(face->glyph->bitmap_left), static_cast<float>(face->glyph->bitmap_top));
      glyph.m_fAdvance = face->glyph->advance.x / 64.0f;
      glyph.m_uiWidth = bitmap.width;
      glyph.m_uiHeight = bitmap.rows;

      if (bitmap.width > 0 && bitmap.rows > 0 && bitmap.buffer != nullptr)
      {
        glyph.m_Bitmap.SetCountUninitialized(bitmap.width * bitmap.rows);

        for (plUInt32 row = 0; row < bitmap.rows; ++row)
        {
          plMemoryUtils::Copy(glyph.m_Bitmap.GetData() + row * bitmap.width, bitmap.buffer + row * bitmap.pitch, bitmap.width);
        }
      }
    }

    if (bSdfUnsupported)
    {
      plLog::Warning("The FreeType build does not support SDF rendering; '{}' was baked with regular anti-aliased glyphs.", pProp->m_sFontFile);
    }

    if (glyphs.IsEmpty())
    {
      return plStatus("The font contains none of the selected characters");
    }

    // pack the glyph bitmaps into an atlas (2 px padding against bilinear bleed)
    constexpr plUInt32 uiPadding = 2;

    plTexturePacker packer;
    plUInt32 uiAtlasSize = 256;

    for (; uiAtlasSize <= 8192; uiAtlasSize *= 2)
    {
      packer.SetTextureSize(uiAtlasSize, uiAtlasSize, glyphs.GetCount());

      for (const plUiFontBakeGlyph& glyph : glyphs)
      {
        packer.AddTexture(glyph.m_uiWidth + 2 * uiPadding, glyph.m_uiHeight + 2 * uiPadding);
      }

      if (packer.PackTextures().Succeeded())
        break;
    }

    if (uiAtlasSize > 8192)
    {
      return plStatus("Font atlas would exceed 8192x8192 - reduce base size or character ranges");
    }

    // blit into the atlas and build the glyph table
    out_desc.m_uiAtlasWidth = uiAtlasSize;
    out_desc.m_uiAtlasHeight = uiAtlasSize;
    out_desc.m_AtlasData.SetCount(uiAtlasSize * uiAtlasSize); // zero-initialized = fully outside

    const auto& packedTextures = packer.GetTextures();

    out_desc.m_Glyphs.Reserve(glyphs.GetCount());

    for (plUInt32 i = 0; i < glyphs.GetCount(); ++i)
    {
      const plUiFontBakeGlyph& baked = glyphs[i];
      const plVec2U32 vPos = packedTextures[i].m_Position + plVec2U32(uiPadding, uiPadding);

      for (plUInt32 row = 0; row < baked.m_uiHeight; ++row)
      {
        plMemoryUtils::Copy(out_desc.m_AtlasData.GetData() + (vPos.y + row) * uiAtlasSize + vPos.x, baked.m_Bitmap.GetData() + row * baked.m_uiWidth, baked.m_uiWidth);
      }

      plUiFontGlyph& glyph = out_desc.m_Glyphs.ExpandAndGetRef();
      glyph.m_uiCodepoint = baked.m_uiCodepoint;
      glyph.m_vAtlasPos = vPos;
      glyph.m_vAtlasSize.Set(baked.m_uiWidth, baked.m_uiHeight);
      glyph.m_vBearing = baked.m_vBearing;
      glyph.m_fAdvance = baked.m_fAdvance;
    }

    // kerning pairs
    if (FT_HAS_KERNING(face))
    {
      for (const plUiFontBakeGlyph& first : glyphs)
      {
        for (const plUiFontBakeGlyph& second : glyphs)
        {
          FT_Vector kerning;
          if (FT_Get_Kerning(face, first.m_uiGlyphIndex, second.m_uiGlyphIndex, FT_KERNING_DEFAULT, &kerning) != 0)
            continue;

          if (kerning.x != 0)
          {
            const plUInt64 uiKey = (static_cast<plUInt64>(first.m_uiCodepoint) << 32) | second.m_uiCodepoint;
            out_desc.m_Kerning.Insert(uiKey, kerning.x / 64.0f);
          }
        }
      }
    }

    return plStatus(PL_SUCCESS);
  }
} // namespace

plTransformStatus plUiFontAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  const plUiFontAssetProperties* pProp = GetProperties();

  plUiFontResourceDescriptor desc;

  plStatus status = BakeFont(pProp, desc);
  if (status.Failed())
    return status;

  if (desc.Save(stream).Failed())
    return plStatus("Failed to write UI font resource");

  return plStatus(PL_SUCCESS);
}

void plUiFontAssetDocument::UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const
{
  SUPER::UpdateAssetDocumentInfo(pInfo);

  const plUiFontAssetProperties* pProp = GetProperties();

  if (!pProp->m_sFontFile.IsEmpty())
  {
    pInfo->m_TransformDependencies.Insert(pProp->m_sFontFile);
  }
}
