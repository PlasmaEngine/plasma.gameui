#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAsset.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiLocalization.h>
#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Elements/UiTextFit.h>
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

  void CreateDescriptor(const plUiDocumentAssetProperties& properties, plUiDocumentResourceDescriptor& out_desc)
  {
    // the document edits the children of an implicit full-canvas root panel
    plUiPanel* pRoot = PL_DEFAULT_NEW(plUiPanel);
    pRoot->m_sName = "Root";
    pRoot->m_Color = plColorGammaUB(0, 0, 0, 0); // invisible background
    pRoot->m_vAnchorMin = plVec2::MakeZero();    // stretch across the whole canvas
    pRoot->m_vAnchorMax = plVec2(1.0f, 1.0f);
    pRoot->m_vOffsets = plVec4::MakeZero();
    out_desc.m_pRoot = pRoot;

    for (const plUiElement* pChild : properties.m_Children)
    {
      if (pChild == nullptr)
        continue;

      void* pClone = plReflectionSerializer::Clone(pChild, pChild->GetDynamicRTTI());
      pRoot->m_Children.PushBack(static_cast<plUiElement*>(pClone));
    }

    // animation clips
    for (const plUiAnimationClip* pClip : properties.m_Animations)
    {
      if (pClip == nullptr)
        continue;

      void* pClone = plReflectionSerializer::Clone(pClip, pClip->GetDynamicRTTI());
      out_desc.m_Clips.PushBack(static_cast<plUiAnimationClip*>(pClone));
    }
  }

  /// The canvas size the transform checks the texts at.
  constexpr float s_fTextFitWidth = 1920.0f;
  constexpr float s_fTextFitHeight = 1080.0f;

  /// The most text fit warnings one language logs; the rest are counted.
  constexpr plUInt32 s_uiMaxTextFitWarnings = 20;

  /// The name a text fit issue is reported under: the button or toggle a label belongs to, else the text.
  plStringView GetIssueElementName(const plUiElement* pRoot, const plUiTextFitIssue& issue)
  {
    const plUiElement* pElement = pRoot;
    const plUiElement* pParent = nullptr;

    for (plUInt32 uiChild : issue.m_Path)
    {
      if (uiChild >= pElement->m_Children.GetCount() || pElement->m_Children[uiChild] == nullptr)
        break;

      pParent = pElement;
      pElement = pElement->m_Children[uiChild];
    }

    const bool bControlLabel = pParent != nullptr && (pParent->GetDynamicRTTI() == plGetStaticRTTI<plUiButton>() || pParent->GetDynamicRTTI() == plGetStaticRTTI<plUiToggle>());
    return bControlLabel ? pParent->m_sName.GetView() : pElement->m_sName.GetView();
  }

  void LogTextFit(const plUiElement* pRoot, const plUiLayoutContext& context, plStringView sLanguage)
  {
    plDynamicArray<plUiTextFitIssue> issues;
    plUiTextFit::Analyze(pRoot, plRectFloat(0, 0, s_fTextFitWidth, s_fTextFitHeight), context, issues);

    plUInt32 uiLogged = 0;
    for (const plUiTextFitIssue& issue : issues)
    {
      // a shrunk text fits; the preview shows it, the transform does not warn
      if (issue.m_Kind == plUiTextFitIssue::Kind::Shrunk)
        continue;

      if (uiLogged == s_uiMaxTextFitWarnings)
      {
        plLog::Warning("Text fit ({}): more texts do not fit; the UI Document editor's Text fit list shows all of them.", sLanguage);
        break;
      }

      plLog::Warning("Text fit ({}): {}", sLanguage, plUiTextFit::Describe(issue, GetIssueElementName(pRoot, issue)));
      ++uiLogged;
    }
  }

  /// Warns about every text that does not fit at 1920x1080: once with the authored texts, once per language of the
  /// project's string tables.
  void CheckTextFit(const plUiElement* pRoot)
  {
    plUiLayoutContext context;
    context.m_bWaitForFonts = true;
    LogTextFit(pRoot, context, "authored");

    plDynamicArray<plGameUiLocalization::Language> languages;
    plGameUiLocalization::CollectLanguages(languages);

    for (const plGameUiLocalization::Language& language : languages)
    {
      plUniquePtr<plTranslatorFromFiles> pTranslator = plGameUiLocalization::CreateTranslator(language);
      context.m_pTranslator = pTranslator.Borrow();
      LogTextFit(pRoot, context, language.m_sName);
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
  CreateDescriptor(*pProp, desc);

  if (desc.Save(inout_stream).Failed())
  {
    return plStatus("Failed to serialize UI document descriptor");
  }

  return plStatus(PL_SUCCESS);
}

void plUiDocumentAssetDocument::UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const
{
  SUPER::UpdateAssetDocumentInfo(pInfo);

  // the transform's Text fit warnings check every language
  plGameUiLocalization::AddTableDependencies(pInfo->m_TransformDependencies);
}

plTransformStatus plUiDocumentAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  const plUiDocumentAssetProperties* pProp = GetProperties();
  ValidateFlow(*pProp, GetDocumentPath());

  plUiDocumentResourceDescriptor desc;
  CreateDescriptor(*pProp, desc);

  // only the transform checks the text fit; the editor's preview writes the asset without it
  CheckTextFit(desc.m_pRoot);

  if (desc.Save(stream).Failed())
  {
    return plStatus("Failed to serialize UI document descriptor");
  }

  return plStatus(PL_SUCCESS);
}

plTransformStatus plUiDocumentAssetDocument::InternalCreateThumbnail(const ThumbnailInfo& ThumbnailInfo)
{
  plStatus status = plAssetDocument::RemoteCreateThumbnail(ThumbnailInfo);
  return status;
}
