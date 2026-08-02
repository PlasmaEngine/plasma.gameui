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

  bool m_bLatin = true;          ///< U+0020 - U+007E
  bool m_bLatinExtended = false; ///< U+00A0 - U+017F
  bool m_bCyrillic = false;      ///< U+0400 - U+04FF
  bool m_bGreek = false;         ///< U+0370 - U+03FF
  plString m_sCustomCharacters;  ///< any additional characters to bake
};
