#include <EnginePluginGameUi/EnginePluginGameUiPCH.h>

#include <EnginePluginGameUi/UiStyleAsset/UiStyleContext.h>
#include <EnginePluginGameUi/UiStyleAsset/UiStyleViewContext.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiStyleContext, 1, plRTTIDefaultAllocator<plUiStyleContext>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_CONSTANT_PROPERTY("DocumentType", (const char*) "UI Style"),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  /// places an element as a centered box below the given anchor fraction
  void PlaceCentered(plUiElement* pElement, float fAnchorY, float fWidth, float fHeight)
  {
    pElement->m_vAnchorMin.Set(0.5f, fAnchorY);
    pElement->m_vAnchorMax.Set(0.5f, fAnchorY);
    pElement->m_vOffsets.Set(-fWidth * 0.5f, -fHeight * 0.5f, fWidth, fHeight);
  }

  plUiText* MakeLabel(const char* szText, const plUiStyleResourceHandle& hStyle)
  {
    plUiText* pText = PL_DEFAULT_NEW(plUiText);
    pText->m_sText = szText;
    pText->m_hStyle = hStyle;
    pText->m_HAlign = plUiTextHAlign::Center;
    pText->m_VAlign = plUiTextVAlign::Center;

    // fill the parent (the labelled widget)
    pText->m_vAnchorMin.Set(0.0f, 0.0f);
    pText->m_vAnchorMax.Set(1.0f, 1.0f);
    pText->m_vOffsets.SetZero();

    return pText;
  }
} // namespace

plUiStyleContext::plUiStyleContext()
  : plEngineProcessDocumentContext(plEngineProcessDocumentContextFlags::CreateWorld)
{
}

plUiStyleContext::~plUiStyleContext() = default;

void plUiStyleContext::OnInitialize()
{
  auto pWorld = m_pWorld;
  PL_LOCK(pWorld->GetWriteMarker());

  plStringBuilder sStyleGuid;
  plConversionUtils::ToString(GetDocumentGuid(), sStyleGuid);
  m_hStyle = plResourceManager::LoadResource<plUiStyleResource>(sStyleGuid);

  // ————— showcase document, built in code (no asset): every sample uses the edited style —————
  {
    plUiDocumentResourceDescriptor desc;

    plUiPanel* pRoot = PL_DEFAULT_NEW(plUiPanel);
    pRoot->m_sName = "Root";
    pRoot->m_vAnchorMin.SetZero();
    pRoot->m_vAnchorMax.Set(1.0f, 1.0f);
    pRoot->m_vOffsets.SetZero();
    pRoot->m_Color = plColorGammaUB(20, 24, 33, 255); // dark backdrop (game-ish, not editor-themed)

    // text sample
    {
      plUiText* pText = PL_DEFAULT_NEW(plUiText);
      pText->m_sName = "SampleText";
      pText->m_sText = "The quick brown fox";
      pText->m_hStyle = m_hStyle;
      pText->m_HAlign = plUiTextHAlign::Center;
      pText->m_VAlign = plUiTextVAlign::Center;
      PlaceCentered(pText, 0.16f, 700.0f, 70.0f);
      pRoot->m_Children.PushBack(pText);
    }

    // button sample (hover/press it in the preview)
    {
      plUiButton* pButton = PL_DEFAULT_NEW(plUiButton);
      pButton->m_sName = "SampleButton";
      pButton->m_hStyle = m_hStyle;
      PlaceCentered(pButton, 0.32f, 300.0f, 64.0f);
      pButton->m_Children.PushBack(MakeLabel("Button", m_hStyle));
      pRoot->m_Children.PushBack(pButton);
    }

    // disabled button sample
    {
      plUiButton* pButton = PL_DEFAULT_NEW(plUiButton);
      pButton->m_sName = "SampleDisabled";
      pButton->m_hStyle = m_hStyle;
      pButton->m_sDisabledBinding = "PreviewDisabled";
      PlaceCentered(pButton, 0.46f, 300.0f, 64.0f);
      pButton->m_Children.PushBack(MakeLabel("Disabled", m_hStyle));
      pRoot->m_Children.PushBack(pButton);
    }

    // toggle sample (unbound: clicking flips its local state)
    {
      plUiToggle* pToggle = PL_DEFAULT_NEW(plUiToggle);
      pToggle->m_sName = "SampleToggle";
      pToggle->m_hStyle = m_hStyle;
      PlaceCentered(pToggle, 0.60f, 300.0f, 64.0f);
      pToggle->m_Children.PushBack(MakeLabel("Toggle", m_hStyle));
      pRoot->m_Children.PushBack(pToggle);
    }

    // progress bar sample
    {
      plUiProgressBar* pBar = PL_DEFAULT_NEW(plUiProgressBar);
      pBar->m_sName = "SampleBar";
      pBar->m_hStyle = m_hStyle;
      pBar->m_fFillAmount = 0.66f;
      PlaceCentered(pBar, 0.74f, 520.0f, 30.0f);
      pRoot->m_Children.PushBack(pBar);
    }

    // slider sample (drag it in the preview)
    {
      plUiSlider* pSlider = PL_DEFAULT_NEW(plUiSlider);
      pSlider->m_sName = "SampleSlider";
      pSlider->m_hStyle = m_hStyle;
      PlaceCentered(pSlider, 0.86f, 520.0f, 36.0f);
      pRoot->m_Children.PushBack(pSlider);
    }

    desc.m_pRoot = pRoot;

    plStringBuilder sShowcaseId;
    sShowcaseId.SetFormat("UiStyleShowcase-{}", sStyleGuid);
    m_hShowcaseDocument = plResourceManager::CreateResource<plUiDocumentResource>(sShowcaseId, std::move(desc), "UI Style showcase");
  }

  // ————— preview object + canvas —————
  {
    plGameObjectDesc obj;
    obj.m_sName.Assign("UiStylePreview");
    obj.m_bDynamic = true;
    pWorld->CreateObject(obj, m_pMainObject);

    plUiCanvasComponent* pComponent = nullptr;
    m_hCanvasComponent = plUiCanvasComponent::CreateComponent(m_pMainObject, pComponent);

    pComponent->SetUiDocument(m_hShowcaseDocument);

    // the disabled sample is driven by a preview-only blackboard entry
    m_pBlackboard = plBlackboard::Create();
    m_pBlackboard->SetName("StylePreview");
    m_pBlackboard->SetEntryValue("PreviewDisabled", true);
    pComponent->RegisterBlackboard(m_pBlackboard);

    // the preview is always interactive (hover/pressed/checked states, slider drag)
    pComponent->SetInputInjectionEnabled(true);
  }
}

void plUiStyleContext::HandleMessage(const plEditorEngineDocumentMsg* pMsg)
{
  if (const plDocumentConfigMsgToEngine* pCfgMsg = plDynamicCast<const plDocumentConfigMsgToEngine*>(pMsg))
  {
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
  }

  plEngineProcessDocumentContext::HandleMessage(pMsg);
}

plEngineProcessViewContext* plUiStyleContext::CreateViewContext()
{
  return PL_DEFAULT_NEW(plUiStyleViewContext, this);
}

void plUiStyleContext::DestroyViewContext(plEngineProcessViewContext* pContext)
{
  PL_DEFAULT_DELETE(pContext);
}
