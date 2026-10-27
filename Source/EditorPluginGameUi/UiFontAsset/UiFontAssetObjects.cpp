#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiFontAsset/UiFontAssetObjects.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiFontAssetProperties, 2, plRTTIDefaultAllocator<plUiFontAssetProperties>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("FontFile", m_sFontFile)->AddAttributes(new plFileBrowserAttribute("Select font file", "*.ttf;*.otf;*.ttc")),
    PL_MEMBER_PROPERTY("BaseSize", m_uiBaseSize)->AddAttributes(new plDefaultValueAttribute(48), new plClampValueAttribute(16, 256), new plSuffixAttribute("px")),
    PL_ARRAY_MEMBER_PROPERTY("FallbackFonts", m_FallbackFonts)->AddAttributes(new plFileBrowserAttribute("Select fallback font file", "*.ttf;*.otf;*.ttc")),
    PL_MEMBER_PROPERTY("Latin", m_bLatin)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("LatinExtended", m_bLatinExtended),
    PL_MEMBER_PROPERTY("Cyrillic", m_bCyrillic),
    PL_MEMBER_PROPERTY("Greek", m_bGreek),
    PL_MEMBER_PROPERTY("StringTableCharacters", m_bStringTableCharacters)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_MEMBER_PROPERTY("CustomCharacters", m_sCustomCharacters),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiFontAssetProperties::plUiFontAssetProperties() = default;
plUiFontAssetProperties::~plUiFontAssetProperties() = default;
