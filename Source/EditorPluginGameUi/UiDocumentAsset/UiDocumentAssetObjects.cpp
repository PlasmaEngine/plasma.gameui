#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetObjects.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiMockScenario, 1, plRTTIDefaultAllocator<plUiMockScenario>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Data", m_sData),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiDocumentAssetProperties, 1, plRTTIDefaultAllocator<plUiDocumentAssetProperties>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("BlackboardTemplate", m_sBlackboardTemplate)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_BlackboardTemplate")),
    PL_ARRAY_MEMBER_PROPERTY("Children", m_Children)->AddFlags(plPropertyFlags::PointerOwner),
    PL_ARRAY_MEMBER_PROPERTY("Animations", m_Animations)->AddFlags(plPropertyFlags::PointerOwner),
    // tool-managed via the Bindings dock scenario chips
    PL_ARRAY_MEMBER_PROPERTY("MockScenarios", m_MockScenarios)->AddFlags(plPropertyFlags::PointerOwner)->AddAttributes(new plHiddenAttribute()),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiDocumentAssetProperties::plUiDocumentAssetProperties() = default;

plUiDocumentAssetProperties::~plUiDocumentAssetProperties()
{
  for (plUiElement* pChild : m_Children)
  {
    PL_DEFAULT_DELETE(pChild);
  }

  m_Children.Clear();

  for (plUiAnimationClip* pClip : m_Animations)
  {
    PL_DEFAULT_DELETE(pClip);
  }

  m_Animations.Clear();

  for (plUiMockScenario* pScenario : m_MockScenarios)
  {
    PL_DEFAULT_DELETE(pScenario);
  }

  m_MockScenarios.Clear();
}
