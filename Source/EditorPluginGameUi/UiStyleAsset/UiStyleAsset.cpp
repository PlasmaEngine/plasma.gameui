#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiStyleAsset/UiStyleAsset.h>
#include <Foundation/Serialization/ReflectionSerializer.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiStyleAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiStyleAssetDocument::plUiStyleAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plUiStyleAssetProperties>(sDocumentPath, plAssetDocEngineConnection::Simple)
{
}

plStatus plUiStyleAssetDocument::WriteAsset(plStreamWriter& inout_stream) const
{
  const plUiStyleAssetProperties* pProp = GetProperties();

  plUiStyleResourceDescriptor desc;
  desc.m_pStyle = static_cast<plUiStyle*>(plReflectionSerializer::Clone(&pProp->m_Style, plGetStaticRTTI<plUiStyle>()));

  if (desc.Save(inout_stream).Failed())
  {
    return plStatus("Failed to write UI style resource");
  }

  return plStatus(PL_SUCCESS);
}

plTransformStatus plUiStyleAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  return plTransformStatus(WriteAsset(stream));
}
