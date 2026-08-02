#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiFontAsset/UiFontAsset.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontAssetManager.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontAssetWindow.moc.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiFontAssetDocumentManager, 1, plRTTIDefaultAllocator<plUiFontAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiFontAssetDocumentManager::plUiFontAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plUiFontAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "UI Font";
  m_DocTypeDesc.m_sFileExtension = "plUiFontAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/GameUiFont.svg";
  m_DocTypeDesc.m_sAssetCategory = "UI";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plUiFontAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_Ui_Font");

  m_DocTypeDesc.m_sResourceFileExtension = "plBinUiFont";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave;
}

plUiFontAssetDocumentManager::~plUiFontAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plUiFontAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plUiFontAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  switch (e.m_Type)
  {
    case plDocumentManager::Event::Type::DocumentWindowRequested:
    {
      if (e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plUiFontAssetDocument>())
      {
        new plQtUiFontAssetDocumentWindow(static_cast<plUiFontAssetDocument*>(e.m_pDocument)); // NOLINT: Not a memory leak
      }
    }
    break;

    default:
      break;
  }
}

void plUiFontAssetDocumentManager::InternalCreateDocument(
  plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plUiFontAssetDocument(sPath);
}

void plUiFontAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  inout_DocumentTypes.PushBack(&m_DocTypeDesc);
}
