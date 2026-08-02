#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiStyleAsset/UiStyleAssetObjects.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiStyleAssetProperties, 2, plRTTIDefaultAllocator<plUiStyleAssetProperties>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Style", m_Style),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiStyleAssetProperties::plUiStyleAssetProperties() = default;
plUiStyleAssetProperties::~plUiStyleAssetProperties() = default;
