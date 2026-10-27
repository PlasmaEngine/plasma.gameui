#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiStyleAsset/UiStyleAsset.h>
#include <EditorPluginGameUi/UiStyleAsset/UiStyleAssetManager.h>
#include <EditorPluginGameUi/UiStyleAsset/UiStyleAssetWindow.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiStyleAssetDocumentManager, 1, plRTTIDefaultAllocator<plUiStyleAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiStyleAssetDocumentManager::plUiStyleAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plUiStyleAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "UI Style";
  m_DocTypeDesc.m_sFileExtension = "plUiStyleAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/GameUiStyle.svg";
  m_DocTypeDesc.m_sAssetCategory = "UI";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plUiStyleAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_Ui_Style");

  m_DocTypeDesc.m_sResourceFileExtension = "plBinUiStyle";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave;
}

plUiStyleAssetDocumentManager::~plUiStyleAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plUiStyleAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plUiStyleAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  switch (e.m_Type)
  {
    case plDocumentManager::Event::Type::DocumentWindowRequested:
    {
      if (e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plUiStyleAssetDocument>())
      {
        new plUiStyleAssetDocumentWindow(static_cast<plUiStyleAssetDocument*>(e.m_pDocument)); // NOLINT: Not a memory leak
      }
    }
    break;

    default:
      break;
  }
}

void plUiStyleAssetDocumentManager::InternalCreateDocument(
  plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plUiStyleAssetDocument(sPath);
}

void plUiStyleAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  inout_DocumentTypes.PushBack(&m_DocTypeDesc);
}
