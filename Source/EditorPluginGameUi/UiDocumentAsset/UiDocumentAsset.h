#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetObjects.h>

class plUiDocumentAssetDocument : public plSimpleAssetDocument<plUiDocumentAssetProperties>
{
  PL_ADD_DYNAMIC_REFLECTION(plUiDocumentAssetDocument, plSimpleAssetDocument<plUiDocumentAssetProperties>);

public:
  plUiDocumentAssetDocument(plStringView sDocumentPath);

  /// \brief Serializes the compiled UI document resource data (widget tree) into the stream.
  ///
  /// Used by the live-preview resource update. The asset transform writes the same data and also logs the Text fit
  /// warnings.
  plStatus WriteAsset(plStreamWriter& inout_stream) const;

protected:
  virtual void UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const override;

  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
    const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;

  virtual plTransformStatus InternalCreateThumbnail(const ThumbnailInfo& ThumbnailInfo) override;
};
