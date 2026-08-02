#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <EditorPluginGameUi/UiStyleAsset/UiStyleAssetObjects.h>

class plUiStyleAssetDocument : public plSimpleAssetDocument<plUiStyleAssetProperties>
{
  PL_ADD_DYNAMIC_REFLECTION(plUiStyleAssetDocument, plSimpleAssetDocument<plUiStyleAssetProperties>);

public:
  plUiStyleAssetDocument(plStringView sDocumentPath);

  /// \brief Writes the compiled style descriptor. Shared by the transform and the live
  /// engine preview (plResourceUpdateMsgToEngine).
  plStatus WriteAsset(plStreamWriter& inout_stream) const;

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
    const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
};
