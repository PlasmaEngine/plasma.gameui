#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontAssetObjects.h>

class plUiFontAssetDocument : public plSimpleAssetDocument<plUiFontAssetProperties>
{
  PL_ADD_DYNAMIC_REFLECTION(plUiFontAssetDocument, plSimpleAssetDocument<plUiFontAssetProperties>);

public:
  plUiFontAssetDocument(plStringView sDocumentPath);

  /// Appends the characters the font does not list yet to the Custom Characters of a UI Font asset (by its resource
  /// GUID) as one undoable edit, saves and transforms it. A font that is not open is opened without a window and
  /// closed again.
  static plStatus AddCustomCharacters(plStringView sFontResource, const plHashSet<plUInt32>& characters, plUInt32& out_uiAdded);

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
    const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;

  virtual void UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const override;
};
