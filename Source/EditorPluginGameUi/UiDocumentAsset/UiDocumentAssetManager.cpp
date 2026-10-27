#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAsset.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetManager.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetWindow.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiDocumentAssetDocumentManager, 1, plRTTIDefaultAllocator<plUiDocumentAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiDocumentAssetDocumentManager::plUiDocumentAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "UI Document";
  m_DocTypeDesc.m_sFileExtension = "plUiDocumentAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/GameUi.svg";
  m_DocTypeDesc.m_sAssetCategory = "UI";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plUiDocumentAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_Ui_Document");

  m_DocTypeDesc.m_sResourceFileExtension = "plBinUiDocument";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave | plAssetDocumentFlags::SupportsThumbnail;
}

plUiDocumentAssetDocumentManager::~plUiDocumentAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plUiDocumentAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plUiDocumentAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  switch (e.m_Type)
  {
    case plDocumentManager::Event::Type::DocumentWindowRequested:
    {
      if (e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plUiDocumentAssetDocument>())
      {
        new plUiDocumentAssetDocumentWindow(static_cast<plUiDocumentAssetDocument*>(e.m_pDocument)); // NOLINT: Not a memory leak
      }
    }
    break;

    default:
      break;
  }
}

void plUiDocumentAssetDocumentManager::InternalCreateDocument(
  plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plUiDocumentAssetDocument(sPath);
}

void plUiDocumentAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  inout_DocumentTypes.PushBack(&m_DocTypeDesc);
}
