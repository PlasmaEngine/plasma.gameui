#pragma once

#include <Foundation/Reflection/Reflection.h>

class plUiFontAssetProperties : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiFontAssetProperties, plReflectedClass);

public:
  plUiFontAssetProperties();
  ~plUiFontAssetProperties();

  plString m_sFontFile;
  plUInt32 m_uiBaseSize = 48;

  /// Font files that draw the characters the font file has no glyph for, tried in order (a CJK face behind a Latin
  /// one). Their glyphs are baked into the same atlas.
  plDynamicArray<plString> m_FallbackFonts;

  bool m_bLatin = true;          ///< U+0020 - U+007E
  bool m_bLatinExtended = false; ///< U+00A0 - U+017F
  bool m_bCyrillic = false;      ///< U+0400 - U+04FF
  bool m_bGreek = false;         ///< U+0370 - U+03FF

  /// Bakes every character the project's string tables use (<project>/Localization), so translated texts draw without
  /// listing their characters.
  bool m_bStringTableCharacters = true;

  plString m_sCustomCharacters; ///< any additional characters to bake
};
