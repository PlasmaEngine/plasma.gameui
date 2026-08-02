#pragma once

#include <GameUiPlugin/Resources/UiStyleResource.h>

/// \brief Editable properties of a UI Style asset: ONE style.
///
/// The style is the runtime plUiStyle type embedded by value; the transform clones it
/// into the compiled descriptor.
class plUiStyleAssetProperties : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiStyleAssetProperties, plReflectedClass);

public:
  plUiStyleAssetProperties();
  ~plUiStyleAssetProperties();

  plUiStyle m_Style;
};
