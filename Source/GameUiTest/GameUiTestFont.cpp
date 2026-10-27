#include <GameUiTest/GameUiTestPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <GameUiTest/GameUiTestFont.h>

plUiFontResourceHandle plGameUiTestCreateFont(plStringView sName)
{
  plUiFontResourceHandle hFont = plResourceManager::GetExistingResource<plUiFontResource>(sName);
  if (hFont.IsValid())
    return hFont;

  plUiFontResourceDescriptor desc;
  desc.m_fBaseSize = 10.0f;
  desc.m_fAscent = 8.0f;
  desc.m_fDescent = -2.0f;
  desc.m_fLineGap = 0.0f;

  auto addGlyph = [&](plUInt32 uiCodepoint)
  {
    plUiFontGlyph& glyph = desc.m_Glyphs.ExpandAndGetRef();
    glyph.m_uiCodepoint = uiCodepoint;
    glyph.m_vAtlasSize = plVec2U32(5, 8);
    glyph.m_vBearing = plVec2(0.5f, 8.0f);
    glyph.m_fAdvance = 6.0f;
  };

  for (plUInt32 cp = 32; cp <= 126; ++cp)
    addGlyph(cp);

  // German: Ü ü ö ä ß
  for (plUInt32 cp : {0xDCu, 0xFCu, 0xF6u, 0xE4u, 0xDFu})
    addGlyph(cp);

  return plResourceManager::GetOrCreateResource<plUiFontResource>(sName, std::move(desc), sName);
}
