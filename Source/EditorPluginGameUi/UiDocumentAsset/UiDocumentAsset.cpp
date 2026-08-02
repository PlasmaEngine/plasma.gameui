#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAsset.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>

namespace
{
  plUInt32 CountElementsNamed(plArrayPtr<plUiElement* const> elements, plStringView sName)
  {
    plUInt32 uiCount = 0;

    for (const plUiElement* pElement : elements)
    {
      if (pElement == nullptr)
        continue;

      if (pElement->m_sName == sName)
        ++uiCount;

      uiCount += CountElementsNamed(pElement->m_Children, sName);
    }

    return uiCount;
  }

  plUInt32 CountClipsNamed(plArrayPtr<plUiAnimationClip* const> clips, plStringView sName)
  {
    plUInt32 uiCount = 0;

    for (const plUiAnimationClip* pClip : clips)
    {
      if (pClip != nullptr && pClip->m_sName == sName)
        ++uiCount;
    }

    return uiCount;
  }

  void ValidateFlowRecursive(const plUiElement* pElement, const plUiDocumentAssetProperties& properties, plStringView sDocumentPath)
  {
    if (pElement == nullptr)
      return;

    if (const plUiSubDocument* pSubDocument = plDynamicCast<const plUiSubDocument*>(pElement))
    {
      for (plUInt32 uiReaction = 0; uiReaction < pSubDocument->m_Reactions.GetCount(); ++uiReaction)
      {
        const plUiEventReaction* pReaction = pSubDocument->m_Reactions[uiReaction];
        if (pReaction == nullptr)
          continue;

        if (pReaction->m_sEvent.IsEmpty())
        {
          plLog::Warning("UI document '{}': Sub-Document '{}' has a reaction with no Event.", sDocumentPath, pSubDocument->m_sName);
        }

        for (plUInt32 uiEarlier = 0; uiEarlier < uiReaction; ++uiEarlier)
        {
          const plUiEventReaction* pEarlier = pSubDocument->m_Reactions[uiEarlier];
          if (pEarlier != nullptr && !pReaction->m_sEvent.IsEmpty() && pEarlier->m_sEvent == pReaction->m_sEvent)
          {
            plLog::Warning("UI document '{}': Sub-Document '{}' has duplicate reactions for '{}'; only the first will execute.",
              sDocumentPath, pSubDocument->m_sName, pReaction->m_sEvent);
            break;
          }
        }

        if (pReaction->m_Actions.IsEmpty())
        {
          plLog::Warning("UI document '{}': Sub-Document '{}' reaction '{}' has no actions.", sDocumentPath, pSubDocument->m_sName, pReaction->m_sEvent);
        }

        for (const plUiEventAction* pAction : pReaction->m_Actions)
        {
          if (pAction == nullptr)
            continue;

          switch (pAction->m_Type)
          {
            case plUiEventActionType::SetState:
              if (pAction->m_sEntry.IsEmpty())
              {
                plLog::Warning("UI document '{}': Sub-Document '{}' reaction '{}' has a Set State action with no Entry.",
                  sDocumentPath, pSubDocument->m_sName, pReaction->m_sEvent);
              }
              break;

            case plUiEventActionType::Show:
            case plUiEventActionType::Hide:
            case plUiEventActionType::ToggleVisibility:
            {
              const plUInt32 uiMatches = pAction->m_sTarget.IsEmpty() ? 0 : CountElementsNamed(properties.m_Children, pAction->m_sTarget);
              if (uiMatches != 1)
              {
                plLog::Warning("UI document '{}': Sub-Document '{}' reaction '{}' targets element '{}', but {} matching parent elements were found.",
                  sDocumentPath, pSubDocument->m_sName, pReaction->m_sEvent, pAction->m_sTarget, uiMatches);
              }
              break;
            }

            case plUiEventActionType::PlayClip:
            {
              const plUInt32 uiMatches = pAction->m_sClip.IsEmpty() ? 0 : CountClipsNamed(properties.m_Animations, pAction->m_sClip);
              if (uiMatches != 1)
              {
                plLog::Warning("UI document '{}': Sub-Document '{}' reaction '{}' targets clip '{}', but {} matching parent clips were found.",
                  sDocumentPath, pSubDocument->m_sName, pReaction->m_sEvent, pAction->m_sClip, uiMatches);
              }
              break;
            }
          }
        }
      }
    }

    for (const plUiElement* pChild : pElement->m_Children)
    {
      ValidateFlowRecursive(pChild, properties, sDocumentPath);
    }
  }

  void ValidateFlow(const plUiDocumentAssetProperties& properties, plStringView sDocumentPath)
  {
    for (const plUiElement* pElement : properties.m_Children)
    {
      ValidateFlowRecursive(pElement, properties, sDocumentPath);
    }
  }
} // namespace

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiDocumentAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiDocumentAssetDocument::plUiDocumentAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plUiDocumentAssetProperties>(sDocumentPath, plAssetDocEngineConnection::Simple)
{
}

plStatus plUiDocumentAssetDocument::WriteAsset(plStreamWriter& inout_stream) const
{
  const plUiDocumentAssetProperties* pProp = GetProperties();
  ValidateFlow(*pProp, GetDocumentPath());

  plUiDocumentResourceDescriptor desc;

  // the document edits the children of an implicit full-canvas root panel
  plUiPanel* pRoot = PL_DEFAULT_NEW(plUiPanel);
  pRoot->m_sName = "Root";
  pRoot->m_Color = plColorGammaUB(0, 0, 0, 0); // invisible background
  pRoot->m_vAnchorMin = plVec2::MakeZero();    // stretch across the whole canvas
  pRoot->m_vAnchorMax = plVec2(1.0f, 1.0f);
  pRoot->m_vOffsets = plVec4::MakeZero();
  desc.m_pRoot = pRoot;

  for (const plUiElement* pChild : pProp->m_Children)
  {
    if (pChild == nullptr)
      continue;

    void* pClone = plReflectionSerializer::Clone(pChild, pChild->GetDynamicRTTI());
    pRoot->m_Children.PushBack(static_cast<plUiElement*>(pClone));
  }

  // animation clips
  for (const plUiAnimationClip* pClip : pProp->m_Animations)
  {
    if (pClip == nullptr)
      continue;

    void* pClone = plReflectionSerializer::Clone(pClip, pClip->GetDynamicRTTI());
    desc.m_Clips.PushBack(static_cast<plUiAnimationClip*>(pClone));
  }

  if (desc.Save(inout_stream).Failed())
  {
    return plStatus("Failed to serialize UI document descriptor");
  }

  return plStatus(PL_SUCCESS);
}

plTransformStatus plUiDocumentAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  return WriteAsset(stream);
}

plTransformStatus plUiDocumentAssetDocument::InternalCreateThumbnail(const ThumbnailInfo& ThumbnailInfo)
{
  plStatus status = plAssetDocument::RemoteCreateThumbnail(ThumbnailInfo);
  return status;
}
