#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiLocalization.h>
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

    // the characters the translations use, in every language of the project
    if (pProp->m_bStringTableCharacters)
    {
      plDynamicArray<plGameUiLocalization::Language> languages;
      plGameUiLocalization::CollectLanguages(languages);

      for (const plGameUiLocalization::Language& language : languages)
        plGameUiLocalization::CollectCharacters(language, unique);
    }

    for (plUInt32 cp : unique)
    {
      out_codepoints.PushBack(cp);
    }

    out_codepoints.Sort();
  }

  constexpr FT_Int s_iSdfSpread = 16;

  /// One font file loaded into FreeType at the base size; the bytes stay alive with the face.
  struct BakeFace
  {
    plString m_sFile;
    plDynamicArray<plUInt8> m_Data;
    FT_Face m_Face = nullptr;
  };

  plStatus LoadFace(FT_Library ftLibrary, plStringView sFile, plUInt32 uiBaseSize, BakeFace& out_face)
  {
    out_face.m_sFile = sFile;

    plFileReader reader;
    if (reader.Open(sFile).Failed())
      return plStatus(plFmt("Failed to open font file '{}'", sFile));

    out_face.m_Data.SetCountUninitialized(static_cast<plUInt32>(reader.GetFileSize()));
    reader.ReadBytes(out_face.m_Data.GetData(), out_face.m_Data.GetCount());

    // a collection (.ttc) bakes its first face
    if (FT_New_Memory_Face(ftLibrary, out_face.m_Data.GetData(), out_face.m_Data.GetCount(), 0, &out_face.m_Face) != 0)
      return plStatus(plFmt("Failed to load font face from '{}'", sFile));

    if (FT_Set_Pixel_Sizes(out_face.m_Face, 0, uiBaseSize) != 0)
      return plStatus(plFmt("Failed to set the pixel size of '{}'", sFile));

    return plStatus(PL_SUCCESS);
  }

  /// The first face with a glyph for the character: the font file, then the fallbacks in order. nullptr when none has
  /// one.
  BakeFace* FindFaceWithGlyph(plDeque<BakeFace>& ref_faces, plUInt32 uiCodepoint, plUInt32& out_uiGlyphIndex)
  {
    for (BakeFace& face : ref_faces)
    {
      out_uiGlyphIndex = FT_Get_Char_Index(face.m_Face, uiCodepoint);
      if (out_uiGlyphIndex != 0)
        return &face;
    }

    return nullptr;
  }

  /// Kerning pairs between glyphs of the same face; a pair across faces has no kerning.
  void CollectKerning(const plDeque<plUiFontBakeGlyph>& glyphs, const plDynamicArray<const BakeFace*>& glyphFaces, plUiFontResourceDescriptor& out_desc)
  {
    for (plUInt32 i = 0; i < glyphs.GetCount(); ++i)
    {
      const FT_Face face = glyphFaces[i]->m_Face;
      if (!FT_HAS_KERNING(face))
        continue;

      for (plUInt32 j = 0; j < glyphs.GetCount(); ++j)
      {
        if (glyphFaces[j] != glyphFaces[i])
          continue;

        FT_Vector kerning;
        if (FT_Get_Kerning(face, glyphs[i].m_uiGlyphIndex, glyphs[j].m_uiGlyphIndex, FT_KERNING_DEFAULT, &kerning) != 0)
          continue;

        if (kerning.x != 0)
        {
          const plUInt64 uiKey = (static_cast<plUInt64>(glyphs[i].m_uiCodepoint) << 32) | glyphs[j].m_uiCodepoint;
          out_desc.m_Kerning.Insert(uiKey, kerning.x / 64.0f);
        }
      }
    }
  }

  plStatus BakeFont(const plUiFontAssetProperties* pProp, plUiFontResourceDescriptor& out_desc)
  {
    if (pProp->m_sFontFile.IsEmpty())
    {
      return plStatus("No font file specified");
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
      FT_Int iSpread = s_iSdfSpread;
      FT_Property_Set(ftLibrary, "sdf", "spread", &iSpread); // best effort: older modules ignore it
    }

    // the font file first, then the fallbacks; the faces are released before the library
    plDeque<BakeFace> faces;
    PL_SCOPE_EXIT(for (BakeFace& face : faces) { if (face.m_Face != nullptr) FT_Done_Face(face.m_Face); });

    PL_SUCCEED_OR_RETURN(LoadFace(ftLibrary, pProp->m_sFontFile, pProp->m_uiBaseSize, faces.ExpandAndGetRef()));

    for (const plString& sFallback : pProp->m_FallbackFonts)
    {
      if (sFallback.IsEmpty())
        continue;

      // loaded in place: the face points into its bytes, which must not move
      const plStatus status = LoadFace(ftLibrary, sFallback, pProp->m_uiBaseSize, faces.ExpandAndGetRef());
      if (status.Failed())
      {
        plLog::Warning("UI font fallback skipped: {}", status.m_sMessage);
        if (faces.PeekBack().m_Face != nullptr)
          FT_Done_Face(faces.PeekBack().m_Face);
        faces.PopBack();
      }
    }

    // the line metrics come from the font file
    const FT_Face face = faces[0].m_Face;
    out_desc.m_fBaseSize = static_cast<float>(pProp->m_uiBaseSize);
    out_desc.m_fAscent = face->size->metrics.ascender / 64.0f;
    out_desc.m_fDescent = face->size->metrics.descender / 64.0f;
    out_desc.m_fLineGap = (face->size->metrics.height / 64.0f) - (out_desc.m_fAscent - out_desc.m_fDescent);
    out_desc.m_fSdfSpread = static_cast<float>(s_iSdfSpread);

    plDynamicArray<plUInt32> codepoints;
    CollectCodepoints(pProp, codepoints);

    if (codepoints.IsEmpty())
    {
      return plStatus("No characters selected to bake");
    }

    // render all glyphs to SDF bitmaps
    plDeque<plUiFontBakeGlyph> glyphs;
    plDynamicArray<const BakeFace*> glyphFaces;
    bool bSdfUnsupported = false;
    plUInt32 uiMissing = 0;

    for (plUInt32 cp : codepoints)
    {
      plUInt32 uiGlyphIndex = 0;
      BakeFace* pFace = FindFaceWithGlyph(faces, cp, uiGlyphIndex);
      if (pFace == nullptr)
      {
        // not present in the font nor its fallbacks
        if (cp > 32)
          ++uiMissing;
        continue;
      }

      const FT_Face glyphFace = pFace->m_Face;
      if (FT_Load_Glyph(glyphFace, uiGlyphIndex, FT_LOAD_DEFAULT) != 0)
        continue;

      if (FT_Render_Glyph(glyphFace->glyph, FT_RENDER_MODE_SDF) != 0)
      {
        // fall back to a regular AA bitmap; text stays readable but won't scale as crisply
        bSdfUnsupported = true;

        if (FT_Render_Glyph(glyphFace->glyph, FT_RENDER_MODE_NORMAL) != 0)
          continue;
      }

      const FT_Bitmap& bitmap = glyphFace->glyph->bitmap;

      glyphFaces.PushBack(pFace);
      plUiFontBakeGlyph& glyph = glyphs.ExpandAndGetRef();
      glyph.m_uiCodepoint = cp;
      glyph.m_uiGlyphIndex = uiGlyphIndex;
      glyph.m_vBearing.Set(static_cast<float>(glyphFace->glyph->bitmap_left), static_cast<float>(glyphFace->glyph->bitmap_top));
      glyph.m_fAdvance = glyphFace->glyph->advance.x / 64.0f;
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

    if (uiMissing > 0)
    {
      plLog::Warning("{} of the selected characters are in neither '{}' nor its fallback fonts; they draw as boxes.", uiMissing, pProp->m_sFontFile);
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

    CollectKerning(glyphs, glyphFaces, out_desc);

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

  for (const plString& sFallback : pProp->m_FallbackFonts)
  {
    if (!sFallback.IsEmpty())
      pInfo->m_TransformDependencies.Insert(sFallback);
  }

  // an edited translation rebakes the fonts that take the string tables' characters
  if (pProp->m_bStringTableCharacters)
    plGameUiLocalization::AddTableDependencies(pInfo->m_TransformDependencies);
}

namespace
{
  /// The characters of the set the text does not contain yet, in codepoint order.
  plString GetNewCharacters(plStringView sExisting, const plHashSet<plUInt32>& characters)
  {
    plHashSet<plUInt32> existing;
    for (auto it = sExisting.GetIteratorFront(); it.IsValid(); ++it)
      existing.Insert(it.GetCharacter());

    plDynamicArray<plUInt32> added;
    for (plUInt32 uiCp : characters)
    {
      // spaces and control characters never need a glyph
      if (uiCp > 32 && !existing.Contains(uiCp))
        added.PushBack(uiCp);
    }

    added.Sort();

    plStringBuilder sAdded;
    for (plUInt32 uiCp : added)
      sAdded.Append(uiCp);

    return sAdded;
  }
} // namespace

plStatus plUiFontAssetDocument::AddCustomCharacters(plStringView sFontResource, const plHashSet<plUInt32>& characters, plUInt32& out_uiAdded)
{
  out_uiAdded = 0;

  if (!plConversionUtils::IsStringUuid(sFontResource))
    return plStatus("The text uses no UI Font asset.");

  const plUuid fontGuid = plConversionUtils::ConvertStringToUuid(sFontResource);
  auto pSubAsset = plAssetCurator::GetSingleton()->GetSubAsset(fontGuid);
  if (!pSubAsset.isValid())
    return plStatus("The UI Font asset was not found.");

  const plString sPath = pSubAsset->m_pAssetInfo->m_Path.GetAbsolutePath();
  const bool bWasOpen = plDocumentManager::GetDocumentByGuid(fontGuid) != nullptr;

  plDocument* pDoc = plEditorApp::GetSingleton()->OpenDocument(sPath, plDocumentFlags::None);
  plUiFontAssetDocument* pFontDoc = plDynamicCast<plUiFontAssetDocument*>(pDoc);
  if (pFontDoc == nullptr)
    return plStatus(plFmt("'{}' could not be opened.", sPath));

  const plString sAdded = GetNewCharacters(pFontDoc->GetProperties()->m_sCustomCharacters, characters);
  out_uiAdded = sAdded.GetCharacterCount();

  plStatus result(PL_SUCCESS);
  if (out_uiAdded > 0)
  {
    plStringBuilder sCharacters = pFontDoc->GetProperties()->m_sCustomCharacters;
    sCharacters.Append(sAdded);

    plObjectAccessorBase* pAccessor = pFontDoc->GetObjectAccessor();
    pAccessor->StartTransaction("Add Characters");
    result = pAccessor->SetValueByName(pFontDoc->GetPropertyObject(), "CustomCharacters", plString(sCharacters));
    pAccessor->FinishTransaction();

    if (result.Succeeded())
      result = pFontDoc->SaveDocument(true);

    if (result.Succeeded())
    {
      const plTransformStatus transform = plAssetCurator::GetSingleton()->TransformAsset(fontGuid, plTransformFlags::Default);
      if (transform.Failed())
        plLog::Error("Transforming '{}' failed: {}", sPath, transform.m_sMessage);
    }
  }

  if (!bWasOpen)
    pFontDoc->GetDocumentManager()->CloseDocument(pFontDoc);

  return result;
}
