#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontAssetObjects.h>

class plUiFontAssetDocument : public plSimpleAssetDocument<plUiFontAssetProperties>
{
  PL_ADD_DYNAMIC_REFLECTION(plUiFontAssetDocument, plSimpleAssetDocument<plUiFontAssetProperties>);

public:
  plUiFontAssetDocument(plStringView sDocumentPath);

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
    const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;

  virtual void UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const override;
};
