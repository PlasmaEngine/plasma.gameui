#include <EnginePluginGameUi/EnginePluginGameUiPCH.h>

#include <EnginePluginGameUi/UiDocumentAsset/UiDocumentContext.h>
#include <EnginePluginGameUi/UiDocumentAsset/UiDocumentViewContext.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiDocumentContext, 1, plRTTIDefaultAllocator<plUiDocumentContext>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_CONSTANT_PROPERTY("DocumentType", (const char*) "UI Document"),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiDocumentContext::plUiDocumentContext()
  : plEngineProcessDocumentContext(plEngineProcessDocumentContextFlags::CreateWorld)
{
}

plUiDocumentContext::~plUiDocumentContext() = default;

void plUiDocumentContext::OnInitialize()
{
  auto pWorld = m_pWorld;
  PL_LOCK(pWorld->GetWriteMarker());

  // Preview object
  {
    plGameObjectDesc obj;
    obj.m_sName.Assign("UiDocumentPreview");
    obj.m_bDynamic = true;
    pWorld->CreateObject(obj, m_pMainObject);

    plUiCanvasComponent* pComponent = nullptr;
    m_hCanvasComponent = plUiCanvasComponent::CreateComponent(m_pMainObject, pComponent);

    plStringBuilder sResourceGuid;
    plConversionUtils::ToString(GetDocumentGuid(), sResourceGuid);
    m_hMainResource = plResourceManager::LoadResource<plUiDocumentResource>(sResourceGuid);

    pComponent->SetUiDocument(m_hMainResource);

    // the mock blackboard drives data bindings in the preview (fed by the editor's Bindings dock)
    m_pMockBlackboard = plBlackboard::Create();
    m_pMockBlackboard->SetName("EditorMock");
    pComponent->RegisterBlackboard(m_pMockBlackboard);

    // forward preview-side changes (e.g. interact-mode button clicks writing entries)
    // back to the editor, so the mock table reflects them
    m_pMockBlackboard->OnEntryEvent().AddEventHandler([this](const plBlackboard::EntryEvent& e)
      {
        plSimpleDocumentConfigMsgToEditor msg;
        msg.m_DocumentGuid = GetDocumentGuid();
        msg.m_sWhatToDo = "UiMockChanged";
        msg.m_sPayload = e.m_sName.GetString();
        msg.m_PayloadValue = e.m_pEntry->m_Value;

        SendProcessMessage(&msg);
      },
      m_MockBoardUnsubscriber);
  }
}

void plUiDocumentContext::UpdateDocumentContext()
{
  plEngineProcessDocumentContext::UpdateDocumentContext();

  // the gizmos every frame, so a frame that renders a view always has them
  for (plEngineProcessViewContext* pView : m_ViewContexts)
  {
    if (pView != nullptr)
      static_cast<plUiDocumentViewContext*>(pView)->DrawSelectionGizmos();
  }

  // draw-list stats for the editor status bar, pushed only on change (the UiMockChanged channel)
  plUInt32 uiBatches = 0, uiQuads = 0;

  {
    // go through a CONST world so the const TryGetComponent overload is selected — the
    // non-const one asserts on CheckForWriteAccess, and this tick only holds the read lock
    const plWorld* pWorld = m_pWorld;
    PL_LOCK(pWorld->GetReadMarker());

    const plUiCanvasComponent* pComponent = nullptr;
    if (!pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
      return;

    uiBatches = pComponent->GetLastExtractedBatchCount();
    uiQuads = pComponent->GetLastExtractedQuadCount();

    UpdateTextFit(*pComponent);
  }

  if (uiBatches == m_uiLastSentBatches && uiQuads == m_uiLastSentQuads)
    return;

  m_uiLastSentBatches = uiBatches;
  m_uiLastSentQuads = uiQuads;

  plSimpleDocumentConfigMsgToEditor msg;
  msg.m_DocumentGuid = GetDocumentGuid();
  msg.m_sWhatToDo = "UiDrawStats";

  plStringBuilder sPayload;
  sPayload.SetFormat("{};{}", uiBatches, uiQuads);
  msg.m_sPayload = sPayload.GetData();

  SendProcessMessage(&msg);
}

plVec2 plUiDocumentContext::GetPreviewViewportSize() const
{
  for (const plEngineProcessViewContext* pView : m_ViewContexts)
  {
    if (pView != nullptr)
      return static_cast<const plUiDocumentViewContext*>(pView)->GetWindowSize();
  }

  return plVec2::MakeZero();
}

void plUiDocumentContext::UpdateTextFit(const plUiCanvasComponent& component)
{
  const plVec2 vViewport = GetPreviewViewportSize();
  if (vViewport.x <= 0.0f || vViewport.y <= 0.0f)
    return;

  component.CollectTextFit(vViewport, m_TextFitIssues);

  // one record per issue: kind, element path, needed, available, shrunk size, font, text, missing characters
  constexpr char szField[] = "\x1F";
  constexpr char szRecord[] = "\x1E";

  plStringBuilder sPayload;
  for (const plUiTextFitIssue& issue : m_TextFitIssues)
  {
    if (!sPayload.IsEmpty())
      sPayload.Append(szRecord);

    sPayload.AppendFormat("{}", static_cast<plUInt32>(issue.m_Kind));
    sPayload.Append(szField);

    for (plUInt32 i = 0; i < issue.m_Path.GetCount(); ++i)
      sPayload.AppendFormat(i == 0 ? "{}" : "/{}", issue.m_Path[i]);

    sPayload.AppendFormat("{}{}{}{}{}{}{}", szField, plArgF(issue.m_fNeeded, 1), szField, plArgF(issue.m_fAvailable, 1), szField, plArgF(issue.m_fShrunkSize, 1), szField);

    if (issue.m_hFont.IsValid())
      sPayload.Append(issue.m_hFont.GetResourceID());

    plStringBuilder sText = issue.m_sText;
    sText.ReplaceAll("\n", " ");
    sPayload.Append(szField, sText, szField);

    for (plUInt32 uiCp : issue.m_MissingGlyphs)
      sPayload.Append(uiCp);
  }

  if (sPayload == m_sLastSentTextFit)
    return;

  m_sLastSentTextFit = sPayload;

  plSimpleDocumentConfigMsgToEditor msg;
  msg.m_DocumentGuid = GetDocumentGuid();
  msg.m_sWhatToDo = "UiTextFit";
  msg.m_sPayload = sPayload.GetData();
  SendProcessMessage(&msg);
}

void plUiDocumentContext::HandleMessage(const plEditorEngineDocumentMsg* pMsg)
{
  if (const plDocumentConfigMsgToEngine* pCfgMsg = plDynamicCast<const plDocumentConfigMsgToEngine*>(pMsg))
  {
    if (pCfgMsg->m_sWhatToDo == "UiMockBlackboard")
    {
      // m_sValue = newline-separated name/value pairs (values may contain semicolons)
      if (m_pMockBlackboard != nullptr)
      {
        PL_LOCK(m_pWorld->GetWriteMarker());

        // remove-then-re-add on purpose: it bumps the blackboard's structure counter,
        // which makes bound canvases remap their bindings on the next update
        m_pMockBlackboard->RemoveAllEntries();

        plHybridArray<plStringView, 32> parts;
        pCfgMsg->m_sValue.GetView().Split(true, parts, "\n");

        for (plUInt32 i = 0; i + 1 < parts.GetCount(); i += 2)
        {
          const plStringView sName = parts[i];
          const plStringView sValue = parts[i + 1];

          if (sName.IsEmpty())
            continue;

          // type inference: bool > number > string
          plVariant value;

          if (sValue.IsEqual_NoCase("true"))
          {
            value = true;
          }
          else if (sValue.IsEqual_NoCase("false"))
          {
            value = false;
          }
          else
          {
            double fNumber = 0.0;
            if (plConversionUtils::StringToFloat(sValue, fNumber).Succeeded())
            {
              value = fNumber;
            }
            else
            {
              value = plString(sValue);
            }
          }

          m_pMockBlackboard->SetEntryValue(sName, value);

          // interact-mode clicks must sync back to the editor's mock table
          m_pMockBlackboard->SetEntryFlags(plTempHashedString(sName), plBlackboardEntryFlags::OnChangeEvent).IgnoreResult();
        }

        // a trailing empty value can be dropped by the split: treat the dangling name as empty string
        if (parts.GetCount() % 2 == 1 && !parts.PeekBack().IsEmpty())
        {
          m_pMockBlackboard->SetEntryValue(parts.PeekBack(), plVariant(plString()));
          m_pMockBlackboard->SetEntryFlags(plTempHashedString(parts.PeekBack()), plBlackboardEntryFlags::OnChangeEvent).IgnoreResult();
        }
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiCanvasView")
    {
      // m_sValue = "resW;resH;zoom;panX;panY;safeArea;checkerboard[;navGraph]"
      plHybridArray<plStringView, 8> parts;
      pCfgMsg->m_sValue.GetView().Split(false, parts, ";");

      if (parts.GetCount() == 7 || parts.GetCount() == 8)
      {
        plUiCanvasPreviewView view;

        plUInt32 uiResX = 0, uiResY = 0;
        double fZoom = 0.0, fPanX = 0.0, fPanY = 0.0;
        plInt32 iSafeArea = 0, iChecker = 0, iNavGraph = 0;

        plConversionUtils::StringToUInt(parts[0], uiResX).IgnoreResult();
        plConversionUtils::StringToUInt(parts[1], uiResY).IgnoreResult();
        plConversionUtils::StringToFloat(parts[2], fZoom).IgnoreResult();
        plConversionUtils::StringToFloat(parts[3], fPanX).IgnoreResult();
        plConversionUtils::StringToFloat(parts[4], fPanY).IgnoreResult();
        plConversionUtils::StringToInt(parts[5], iSafeArea).IgnoreResult();
        plConversionUtils::StringToInt(parts[6], iChecker).IgnoreResult();

        if (parts.GetCount() >= 8)
        {
          plConversionUtils::StringToInt(parts[7], iNavGraph).IgnoreResult();
        }

        view.m_vResolution.Set(uiResX, uiResY);
        view.m_fZoom = static_cast<float>(fZoom);
        view.m_vPan.Set(static_cast<float>(fPanX), static_cast<float>(fPanY));
        view.m_bCheckerboard = iChecker != 0;

        m_bShowSafeArea = iSafeArea != 0;
        m_bShowNavGraph = iNavGraph != 0;
        m_bHasDevicePreset = uiResX > 0 && uiResY > 0;

        PL_LOCK(m_pWorld->GetWriteMarker());

        plUiCanvasComponent* pComponent = nullptr;
        if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
        {
          pComponent->SetPreviewView(view);
        }
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiInteract")
    {
      PL_LOCK(m_pWorld->GetWriteMarker());

      plUiCanvasComponent* pComponent = nullptr;
      if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
      {
        pComponent->SetInputInjectionEnabled(pCfgMsg->m_iValue != 0);
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiLanguage")
    {
      // m_sValue = ";"-separated ABSOLUTE localization folder paths for the preview
      // language; empty = raw keys. This engine process registers no other translators
      // (verified), so rebuilding the whole chain is safe.
      plTranslationLookup::Clear();

      if (!pCfgMsg->m_sValue.IsEmpty())
      {
        plUniquePtr<plTranslatorFromFiles> pTranslator = PL_DEFAULT_NEW(plTranslatorFromFiles);

        plHybridArray<plStringView, 4> folders;
        pCfgMsg->m_sValue.GetView().Split(false, folders, ";");

        plStringBuilder sFolder;
        for (const plStringView sView : folders)
        {
          sFolder = sView;
          pTranslator->AddTranslationFilesFromFolder(sFolder);
        }

        plTranslationLookup::AddTranslator(std::move(pTranslator));
      }

      // the editor follows up with a preview push, which re-resolves localized texts
      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiHighlightUntranslated")
    {
      plTranslator::HighlightUntranslated(pCfgMsg->m_iValue != 0);
      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiPseudo")
    {
      // m_iValue = the pseudo-localization expansion in percent; 0 = off
      PL_LOCK(m_pWorld->GetWriteMarker());

      plUiCanvasComponent* pComponent = nullptr;
      if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
        pComponent->SetPseudoLocalization(static_cast<float>(pCfgMsg->m_iValue) / 100.0f);

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiGuides")
    {
      // m_sValue = ";"-separated "V|H pos min max" entries (canvas space); empty = clear
      m_SnapGuides.Clear();

      plHybridArray<plStringView, 8> entries;
      pCfgMsg->m_sValue.GetView().Split(false, entries, ";");

      for (const plStringView sEntry : entries)
      {
        plHybridArray<plStringView, 4> parts;
        sEntry.Split(false, parts, " ");

        if (parts.GetCount() != 4)
          continue;

        double fPos = 0.0, fMin = 0.0, fMax = 0.0;
        plConversionUtils::StringToFloat(parts[1], fPos).IgnoreResult();
        plConversionUtils::StringToFloat(parts[2], fMin).IgnoreResult();
        plConversionUtils::StringToFloat(parts[3], fMax).IgnoreResult();

        plUiPreviewGuide& guide = m_SnapGuides.ExpandAndGetRef();
        guide.m_bVertical = parts[0].IsEqual("V");
        guide.m_fPos = static_cast<float>(fPos);
        guide.m_fMin = static_cast<float>(fMin);
        guide.m_fMax = static_cast<float>(fMax);
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiChar")
    {
      // m_iValue = UTF-32 codepoint typed while interact mode is on
      if (pCfgMsg->m_iValue >= 32)
      {
        PL_LOCK(m_pWorld->GetWriteMarker());

        plUiCanvasComponent* pComponent = nullptr;
        if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
        {
          pComponent->InjectCharacter(static_cast<plUInt32>(pCfgMsg->m_iValue));
        }
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiEditKey")
    {
      // m_iValue = plUiEditKey::Enum (Backspace/Delete/Home/End)
      if (pCfgMsg->m_iValue >= 0 && pCfgMsg->m_iValue < plUiEditKey::Count)
      {
        PL_LOCK(m_pWorld->GetWriteMarker());

        plUiCanvasComponent* pComponent = nullptr;
        if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
        {
          pComponent->InjectEditKey(static_cast<plUiEditKey::Enum>(pCfgMsg->m_iValue));
        }
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiNavKey")
    {
      // m_iValue = plUiNavAction::Enum (interact mode: keyboard stands in for the gamepad)
      if (pCfgMsg->m_iValue >= 0 && pCfgMsg->m_iValue < plUiNavAction::Count)
      {
        PL_LOCK(m_pWorld->GetWriteMarker());

        plUiCanvasComponent* pComponent = nullptr;
        if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
        {
          pComponent->InjectNavAction(static_cast<plUiNavAction::Enum>(pCfgMsg->m_iValue));
        }
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiMouse")
    {
      // m_sValue = "x;y;buttonDown[;wheelTicks]" with x/y normalized to the view
      plHybridArray<plStringView, 4> parts;
      pCfgMsg->m_sValue.GetView().Split(false, parts, ";");

      if (parts.GetCount() == 3 || parts.GetCount() == 4)
      {
        double fX = 0.0, fY = 0.0, fWheel = 0.0;
        plInt32 iDown = 0;

        if (parts.GetCount() == 4)
        {
          plConversionUtils::StringToFloat(parts[3], fWheel).IgnoreResult();
        }

        if (plConversionUtils::StringToFloat(parts[0], fX).Succeeded() && plConversionUtils::StringToFloat(parts[1], fY).Succeeded() && plConversionUtils::StringToInt(parts[2], iDown).Succeeded())
        {
          PL_LOCK(m_pWorld->GetWriteMarker());

          plUiCanvasComponent* pComponent = nullptr;
          if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
          {
            pComponent->InjectMouseState(plVec2(static_cast<float>(fX), static_cast<float>(fY)), iDown != 0, static_cast<float>(fWheel));
          }
        }
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiClipPreview")
    {
      // m_sValue = "time;clipName" (time first — the name may contain semicolons)
      const char* szSeparator = pCfgMsg->m_sValue.GetView().FindSubString(";");

      if (szSeparator != nullptr)
      {
        const plStringView sTime(pCfgMsg->m_sValue.GetData(), szSeparator);
        const plStringView sName(szSeparator + 1, pCfgMsg->m_sValue.GetData() + pCfgMsg->m_sValue.GetElementCount());

        double fTime = 0.0;
        if (plConversionUtils::StringToFloat(sTime, fTime).Succeeded())
        {
          PL_LOCK(m_pWorld->GetWriteMarker());

          plUiCanvasComponent* pComponent = nullptr;
          if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
          {
            pComponent->SetClipPreview(sName, static_cast<float>(fTime));
          }
        }
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiClipPlay")
    {
      PL_LOCK(m_pWorld->GetWriteMarker());

      plUiCanvasComponent* pComponent = nullptr;
      if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
      {
        pComponent->SetClipPreview({}, 0.0f); // clear any scrub override
        pComponent->PlayClip(pCfgMsg->m_sValue);
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiClipStop")
    {
      PL_LOCK(m_pWorld->GetWriteMarker());

      plUiCanvasComponent* pComponent = nullptr;
      if (m_pWorld->TryGetComponent(m_hCanvasComponent, pComponent))
      {
        pComponent->SetClipPreview({}, 0.0f);
        pComponent->StopClips();
      }

      return;
    }

    if (pCfgMsg->m_sWhatToDo == "UiSelection")
    {
      m_SelectedPaths.Clear();

      plHybridArray<plStringView, 8> paths;
      pCfgMsg->m_sValue.GetView().Split(false, paths, ";");

      for (plStringView sPath : paths)
      {
        plHybridArray<plStringView, 8> indices;
        sPath.Split(false, indices, "/");

        plDynamicArray<plUInt32>& path = m_SelectedPaths.ExpandAndGetRef();
        for (plStringView sIndex : indices)
        {
          plUInt32 uiIndex = 0;
          if (plConversionUtils::StringToUInt(sIndex, uiIndex).Succeeded())
          {
            path.PushBack(uiIndex);
          }
        }
      }

      return;
    }
  }

  plEngineProcessDocumentContext::HandleMessage(pMsg);
}

plEngineProcessViewContext* plUiDocumentContext::CreateViewContext()
{
  return PL_DEFAULT_NEW(plUiDocumentViewContext, this);
}

void plUiDocumentContext::DestroyViewContext(plEngineProcessViewContext* pContext)
{
  PL_DEFAULT_DELETE(pContext);
}

bool plUiDocumentContext::UpdateThumbnailViewContext(plEngineProcessViewContext* pThumbnailViewContext)
{
  PL_LOCK(m_pMainObject->GetWorld()->GetWriteMarker());

  m_pMainObject->UpdateLocalBounds();
  plBoundingBoxSphere bounds = m_pMainObject->GetGlobalBounds();

  plUiDocumentViewContext* pViewContext = static_cast<plUiDocumentViewContext*>(pThumbnailViewContext);
  return pViewContext->UpdateThumbnailCamera(bounds);
}
