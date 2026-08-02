#include <GameUiPlugin/GameUiPluginPCH.h>

#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Components/UiBindingState.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>

#include <functional>

namespace
{
  float ApplyEasing(plUiAnimEasing::Enum easing, float fT)
  {
    switch (easing)
    {
      case plUiAnimEasing::EaseIn:
        return fT * fT;
      case plUiAnimEasing::EaseOut:
        return 1.0f - (1.0f - fT) * (1.0f - fT);
      case plUiAnimEasing::EaseInOut:
        return fT < 0.5f ? 2.0f * fT * fT : 1.0f - 2.0f * (1.0f - fT) * (1.0f - fT);
      case plUiAnimEasing::Linear:
      default:
        return fT;
    }
  }

  /// interaction-transition timing from the element's style, or sensible defaults
  float GetStyleTransition(const plUiElement* pElement, plUiAnimEasing::Enum& out_easing)
  {
    if (pElement->m_hStyle.IsValid())
    {
      plResourceLock<plUiStyleResource> pStyle(pElement->m_hStyle, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

      if (pStyle.GetAcquireResult() == plResourceAcquireResult::Final && pStyle->GetStyle() != nullptr)
      {
        out_easing = static_cast<plUiAnimEasing::Enum>(pStyle->GetStyle()->m_TransitionEasing.GetValue());
        return pStyle->GetStyle()->m_fTransitionDuration;
      }
    }

    out_easing = plUiAnimEasing::Linear;
    return 0.12f;
  }

  /// advances a 0..1 blend toward a bool target over the given duration
  void AdvanceBlend(float& ref_fBlend, bool bTarget, float fDeltaTime, float fDuration)
  {
    const float fTargetValue = bTarget ? 1.0f : 0.0f;

    if (fDuration <= 0.0f)
    {
      ref_fBlend = fTargetValue;
      return;
    }

    const float fStep = fDeltaTime / fDuration;
    ref_fBlend = plMath::Clamp(ref_fBlend + (fTargetValue > ref_fBlend ? fStep : -fStep), 0.0f, 1.0f);

    if (plMath::Abs(ref_fBlend - fTargetValue) < 0.001f)
    {
      ref_fBlend = fTargetValue; // snap so idle blends settle
    }
  }
} // namespace

void plUiBindingState::SetDocument(const plUiElement* pRoot, const plDynamicArray<plUiAnimationClip*>& clips, plUInt32 uiDocumentVersion)
{
  // NOTE: the version check is load-bearing — on a resource reload the new tree can land
  // on the SAME allocation addresses, and stale clip/track/key pointers would crash
  if (m_pRoot == pRoot && m_pClipsId == static_cast<const void*>(&clips) && m_uiDocumentVersion == uiDocumentVersion)
    return;

  m_pRoot = pRoot;
  m_pRootClips = &clips;
  m_pClipsId = &clips;
  m_uiDocumentVersion = uiDocumentVersion;

  RebuildDiscovery();

  // ————— clip instances: resolve tracks to elements, sort keys —————

  // resolve state -> clip references (states drive clips like a bool trigger)
  for (StateInstance& state : m_States)
  {
    state.m_iPlayClipIndex = -1;
    state.m_iExitClipIndex = -1;

    if (state.m_pState == nullptr)
      continue;

    for (plUInt32 uiClip = 0; uiClip < m_Clips.GetCount(); ++uiClip)
    {
      ClipInstance& clip = m_Clips[uiClip];
      if (clip.m_pDocumentRoot != state.m_pDocumentRoot || clip.m_uiDocumentSalt != state.m_uiSalt)
        continue;

      if (!state.m_pState->m_sPlayClip.IsEmpty() && clip.m_pClip->m_sName == state.m_pState->m_sPlayClip)
      {
        state.m_iPlayClipIndex = static_cast<plInt32>(uiClip);
        clip.m_bStateDriven = true;
        clip.m_bHasExplicitExit = !state.m_pState->m_sExitClip.IsEmpty();
      }

      if (!state.m_pState->m_sExitClip.IsEmpty() && clip.m_pClip->m_sName == state.m_pState->m_sExitClip)
      {
        state.m_iExitClipIndex = static_cast<plInt32>(uiClip);
        clip.m_bStateDriven = true;
        clip.m_bExplicitExit = true;
      }
    }
  }
}

void plUiBindingState::SetBlackboards(plArrayPtr<const plSharedPtr<plBlackboard>> boards)
{
  m_Boards.Clear();

  for (const auto& pBoard : boards)
  {
    if (pBoard != nullptr)
    {
      BoardState& state = m_Boards.ExpandAndGetRef();
      state.m_pBoard = pBoard;
    }
  }

  // force remap + full resolve on the next update
  for (BoardState& state : m_Boards)
  {
    state.m_uiStructureCounter = 0xFFFFFFFF;
    state.m_uiEntryCounter = 0xFFFFFFFF;
  }
}

void plUiBindingState::Clear()
{
  m_pRoot = nullptr;
  m_pRootClips = nullptr;
  m_pClipsId = nullptr;
  m_uiDocumentVersion = 0xFFFFFFFF;
  m_SubDocuments.Clear();
  m_Lists.Clear();
  m_ItemBindings.Clear();
  m_DynamicInstances.Clear();
  m_Bindings.Clear();
  m_Clips.Clear();
  m_States.Clear();
  m_AnimatedElements.Clear();
  m_Resolved.Clear();
  m_ResolvedInstanced.Clear();
  m_Boards.Clear();
  m_InteractionBlends.Clear();
  m_iPreviewClip = -1;
}

const plUiBindingState::ResolvedValues* plUiBindingState::GetResolved(const plUiElement* pElement) const
{
  return m_Resolved.GetValue(pElement);
}

const plUiBindingState::ResolvedValues* plUiBindingState::GetResolved(const plUiElementInstance& instance) const
{
  // per-instance values (item-list rows, dynamic spawns) override the shared element values
  if (instance.m_uiSalt != 0 && !m_ResolvedInstanced.IsEmpty())
  {
    if (const ResolvedValues* pInstanced = m_ResolvedInstanced.GetValue(instance))
    {
      return pInstanced;
    }
  }

  return m_Resolved.GetValue(instance.m_pElement);
}

bool plUiBindingState::IsDisabled(const plUiElement* pElement) const
{
  if (const ResolvedValues* pResolved = m_Resolved.GetValue(pElement))
  {
    return pResolved->m_bDisabled;
  }

  return false;
}

bool plUiBindingState::IsDisabled(const plUiElementInstance& instance) const
{
  if (const ResolvedValues* pResolved = GetResolved(instance))
  {
    return pResolved->m_bDisabled || pResolved->m_bStateInteractionHidden;
  }

  return false;
}

plUiBindingState::ResolvedValues& plUiBindingState::GetMutableResolved(const plUiElement* pElement, plUInt32 uiSalt)
{
  if (uiSalt == 0)
    return m_Resolved[pElement];

  return m_ResolvedInstanced[plUiElementInstance{pElement, uiSalt}];
}

void plUiBindingState::SetFlowVisibility(const plUiElementInstance& instance, bool bVisible)
{
  if (!instance.IsValid())
    return;

  GetMutableResolved(instance.m_pElement, instance.m_uiSalt).m_iFlowVisibility = bVisible ? 1 : 0;
}

void plUiBindingState::ToggleFlowVisibility(const plUiElementInstance& instance)
{
  if (!instance.IsValid())
    return;

  const ResolvedValues* pResolved = GetResolved(instance);
  bool bVisible = instance.m_pElement->m_bVisible;

  if (pResolved != nullptr)
  {
    if (pResolved->m_iFlowVisibility >= 0)
      bVisible = pResolved->m_iFlowVisibility != 0;
    else if (pResolved->m_iStateVisibility >= 0)
      bVisible = pResolved->m_iStateVisibility != 0;
    else if (pResolved->m_uiFlags & ResolvedValues::HasVisible)
      bVisible = pResolved->m_bVisible;
  }

  SetFlowVisibility(instance, !bVisible);
}

plBlackboard* plUiBindingState::FindBoardForEntry(const plTempHashedString& sEntry) const
{
  for (const BoardState& state : m_Boards)
  {
    if (state.m_pBoard->HasEntry(sEntry))
    {
      return state.m_pBoard.Borrow();
    }
  }

  return nullptr;
}

plBlackboard* plUiBindingState::GetPrimaryBoard() const
{
  return m_Boards.IsEmpty() ? nullptr : m_Boards[0].m_pBoard.Borrow();
}

bool plUiBindingState::ReadEntryBool(const plTempHashedString& sEntry) const
{
  if (const plBlackboard* pBoard = FindBoardForEntry(sEntry))
  {
    const plVariant value = pBoard->GetEntryValue(sEntry);
    return value.CanConvertTo<bool>() && value.ConvertTo<bool>();
  }

  return false;
}

const plUiElement* plUiBindingState::FindElementByName(const plUiElement* pElement, plStringView sName) const
{
  if (sName.IsEmpty())
    return nullptr;

  if (pElement->m_sName == sName)
    return pElement;

  for (const plUiElement* pChild : pElement->m_Children)
  {
    if (pChild != nullptr)
    {
      if (const plUiElement* pFound = FindElementByName(pChild, sName))
      {
        return pFound;
      }
    }
  }

  return nullptr;
}

// ————— animation clip control —————

void plUiBindingState::AddDocumentClips(const plUiElement* pDocumentRoot, const plDynamicArray<plUiAnimationClip*>& clips, plUInt32 uiDocumentSalt)
{
  for (const plUiAnimationClip* pClip : clips)
  {
    if (pClip == nullptr)
      continue;

    ClipInstance& instance = m_Clips.ExpandAndGetRef();
    instance.m_pClip = pClip;
    instance.m_pDocumentRoot = pDocumentRoot;
    instance.m_uiDocumentSalt = uiDocumentSalt;

    if (!pClip->m_sEntryBinding.IsEmpty())
    {
      instance.m_sEntry = plTempHashedString(pClip->m_sEntryBinding.GetData());
    }

    for (const plUiAnimTrack* pTrack : pClip->m_Tracks)
    {
      if (pTrack == nullptr || pTrack->m_Keys.IsEmpty())
        continue;

      TrackInstance& track = instance.m_Tracks.ExpandAndGetRef();
      track.m_pTrack = pTrack;
      track.m_pElement = FindElementByName(pDocumentRoot, pTrack->m_sElement);

      for (plUInt32 uiKey = 0; uiKey < pTrack->m_Keys.GetCount(); ++uiKey)
      {
        if (pTrack->m_Keys[uiKey] != nullptr)
        {
          track.m_SortedKeys.PushBack(uiKey);
        }
      }

      track.m_SortedKeys.Sort([pTrack](plUInt32 a, plUInt32 b) { return pTrack->m_Keys[a]->m_fTime < pTrack->m_Keys[b]->m_fTime; });

      const plUiElementInstance animated{track.m_pElement, uiDocumentSalt};
      if (track.m_pElement != nullptr && !m_AnimatedElements.Contains(animated))
      {
        m_AnimatedElements.PushBack(animated);
      }
    }
  }
}

void plUiBindingState::NotifyEvent(plStringView sIdentifier)
{
  NotifyEvent(sIdentifier, m_pRoot, 0);
}

void plUiBindingState::NotifyEvent(plStringView sIdentifier, const plUiElement* pDocumentRoot, plUInt32 uiDocumentSalt)
{
  if (sIdentifier.IsEmpty())
    return;

  for (ClipInstance& instance : m_Clips)
  {
    if (instance.m_pDocumentRoot == pDocumentRoot && instance.m_uiDocumentSalt == uiDocumentSalt && instance.m_pClip->m_sPlayOnEvent == sIdentifier)
    {
      instance.m_bPlaying = true;
      instance.m_fTime = 0.0f;
      instance.m_fDirection = 1.0f;
    }
  }
}

void plUiBindingState::PlayClip(plStringView sName)
{
  PlayClip(sName, m_pRoot, 0);
}

void plUiBindingState::PlayClip(plStringView sName, const plUiElement* pDocumentRoot, plUInt32 uiDocumentSalt)
{
  for (ClipInstance& instance : m_Clips)
  {
    if (instance.m_pDocumentRoot == pDocumentRoot && instance.m_uiDocumentSalt == uiDocumentSalt && instance.m_pClip->m_sName == sName)
    {
      instance.m_bPlaying = true;
      instance.m_fTime = 0.0f;
      instance.m_fDirection = 1.0f;
    }
  }
}

void plUiBindingState::StopClips()
{
  for (ClipInstance& instance : m_Clips)
  {
    instance.m_bPlaying = false;
  }
}

void plUiBindingState::SetClipPreview(plStringView sName, float fTime)
{
  m_iPreviewClip = -1;
  m_fPreviewTime = fTime;

  if (sName.IsEmpty())
    return;

  for (plUInt32 i = 0; i < m_Clips.GetCount(); ++i)
  {
    if (m_Clips[i].m_pClip->m_sName == sName)
    {
      m_iPreviewClip = static_cast<plInt32>(i);
      return;
    }
  }
}

// ————— discovery —————

void plUiBindingState::RebuildDiscovery()
{
  m_Bindings.Clear();
  m_Clips.Clear();
  m_States.Clear();
  m_AnimatedElements.Clear();
  m_Resolved.Clear();
  m_ResolvedInstanced.Clear();
  m_InteractionBlends.Clear();
  m_SubDocuments.Clear();
  m_Lists.Clear();
  m_ItemBindings.Clear();
  m_iPreviewClip = -1;

  if (m_pRoot != nullptr && m_pRootClips != nullptr)
  {
    AddDocumentClips(m_pRoot, *m_pRootClips, 0);
    DiscoverRecursive(m_pRoot, m_pRoot, 0, 0, -1, false);
  }

  // dynamically spawned instances survive a document rebuild (they live in their own
  // resources) but their instanced values were just cleared: re-discover + re-resolve
  for (DynamicInstance& instance : m_DynamicInstances)
  {
    DiscoverDynamicInstance(instance);
    ResolveDynamicInstance(instance, true);
  }

  // fresh bindings have no board mapping yet: invalidate the structure counters so the
  // next Update runs the full remap + resolve pass (also heals reload-while-playing)
  for (BoardState& state : m_Boards)
  {
    state.m_uiStructureCounter = 0xFFFFFFFF;
  }

  for (StateInstance& state : m_States)
  {
    state.m_iPlayClipIndex = -1;
    state.m_iExitClipIndex = -1;

    if (state.m_pState == nullptr)
      continue;

    for (plUInt32 uiClip = 0; uiClip < m_Clips.GetCount(); ++uiClip)
    {
      ClipInstance& clip = m_Clips[uiClip];
      if (clip.m_pDocumentRoot != state.m_pDocumentRoot || clip.m_uiDocumentSalt != state.m_uiSalt)
        continue;

      if (!state.m_pState->m_sPlayClip.IsEmpty() && clip.m_pClip->m_sName == state.m_pState->m_sPlayClip)
      {
        state.m_iPlayClipIndex = static_cast<plInt32>(uiClip);
        clip.m_bStateDriven = true;
        clip.m_bHasExplicitExit = !state.m_pState->m_sExitClip.IsEmpty();
      }

      if (!state.m_pState->m_sExitClip.IsEmpty() && clip.m_pClip->m_sName == state.m_pState->m_sExitClip)
      {
        state.m_iExitClipIndex = static_cast<plInt32>(uiClip);
        clip.m_bStateDriven = true;
        clip.m_bExplicitExit = true;
      }
    }
  }
}

bool plUiBindingState::RefreshSubDocuments()
{
  bool bChanged = false;

  for (const SubDocumentRef& ref : m_SubDocuments)
  {
    plResourceLock<plUiDocumentResource> pResource(ref.m_hDocument, plResourceAcquireMode::PointerOnly);

    if (pResource->GetCurrentResourceChangeCounter() != ref.m_uiChangeCounter)
    {
      bChanged = true;
      break;
    }
  }

  // spawned instances hold their own documents: a reload there strands their pointers too
  if (!bChanged)
  {
    for (const DynamicInstance& instance : m_DynamicInstances)
    {
      plResourceLock<plUiDocumentResource> pResource(instance.m_hDocument, plResourceAcquireMode::PointerOnly);

      if (pResource->GetCurrentResourceChangeCounter() != instance.m_uiChangeCounter)
      {
        bChanged = true;
        break;
      }
    }
  }

  if (bChanged)
  {
    RebuildDiscovery();
  }

  return bChanged;
}

void plUiBindingState::AddDiscoveredBinding(plInt32 iListContext, const plUiElement* pElement, plUInt32 uiSalt, Kind kind, plStringView sEntry, float fMin /*= 0.0f*/, float fMax /*= 1.0f*/)
{
  if (iListContext >= 0)
  {
    // inside an item template: the "entry" is a key into each item's data dictionary
    ItemBinding& binding = m_ItemBindings.ExpandAndGetRef();
    binding.m_uiList = static_cast<plUInt32>(iListContext);
    binding.m_pElement = pElement;
    binding.m_Kind = kind;
    binding.m_sKey = sEntry;
    binding.m_fMin = fMin;
    binding.m_fMax = fMax;
    return;
  }

  Binding& binding = m_Bindings.ExpandAndGetRef();
  binding.m_pElement = pElement;
  binding.m_uiSalt = uiSalt;
  binding.m_Kind = kind;
  binding.m_sEntry.Assign(sEntry);
  binding.m_fMin = fMin;
  binding.m_fMax = fMax;
}

void plUiBindingState::DiscoverRecursive(const plUiElement* pElement, const plUiElement* pDocumentRoot, plUInt32 uiDepth, plUInt32 uiChainSalt, plInt32 iListContext, bool bSkipShared)
{
  if (!pElement->m_sVisibleBinding.IsEmpty())
  {
    AddDiscoveredBinding(iListContext, pElement, uiChainSalt, Kind::Visible, pElement->m_sVisibleBinding);
  }

  if (const plUiProgressBar* pProgressBar = plDynamicCast<const plUiProgressBar*>(pElement))
  {
    if (!pProgressBar->m_sFillAmountBinding.IsEmpty())
    {
      AddDiscoveredBinding(iListContext, pElement, uiChainSalt, Kind::Fill, pProgressBar->m_sFillAmountBinding, pProgressBar->m_fBindingMin, pProgressBar->m_fBindingMax);
    }
  }

  // a slider's read side reuses the fill remap ([Min, Max] -> 0..1); the write side is the drag
  if (const plUiSlider* pSlider = plDynamicCast<const plUiSlider*>(pElement))
  {
    if (!pSlider->m_sValueBinding.IsEmpty())
    {
      AddDiscoveredBinding(iListContext, pElement, uiChainSalt, Kind::Fill, pSlider->m_sValueBinding, pSlider->m_fMin, pSlider->m_fMax);
    }
  }

  // a toggle's checked state mirrors its click entry
  if (const plUiToggle* pToggle = plDynamicCast<const plUiToggle*>(pElement))
  {
    if (!pToggle->m_sClickEntryBinding.IsEmpty())
    {
      AddDiscoveredBinding(iListContext, pElement, uiChainSalt, Kind::Checked, pToggle->m_sClickEntryBinding);
    }
  }

  // a text input's read side: the bound entry is the displayed text (write side = typing)
  if (const plUiTextInput* pTextInput = plDynamicCast<const plUiTextInput*>(pElement))
  {
    if (!pTextInput->m_sTextBinding.IsEmpty())
    {
      AddDiscoveredBinding(iListContext, pElement, uiChainSalt, Kind::Text, pTextInput->m_sTextBinding);
    }
  }

  if (const plUiText* pText = plDynamicCast<const plUiText*>(pElement))
  {
    if (!pText->m_sTextBinding.IsEmpty())
    {
      AddDiscoveredBinding(iListContext, pElement, uiChainSalt, Kind::Text, pText->m_sTextBinding);
    }
    else if (pText->m_bLocalized && !pText->m_sText.IsEmpty() && !bSkipShared)
    {
      // resolved via plTranslate on the main thread (extraction threads must not translate);
      // resolve immediately, it never changes until the tree is rebuilt
      ResolvedValues& resolved = GetMutableResolved(pElement, uiChainSalt);
      resolved.m_uiFlags |= ResolvedValues::HasText;
      resolved.m_sText = plTranslate(pText->m_sText);

      // highlight-untranslated: a MISSING key falls through the translators unchanged and
      // undecorated — mark it like plTranslatorFromFiles marks its own entries
      if (plTranslator::GetHighlightUntranslated() && resolved.m_sText == pText->m_sText)
      {
        plStringBuilder sMarked("# ", resolved.m_sText);
        resolved.m_sText = sMarked;
      }
    }
  }

  // shared (element-keyed) registrations happen ONCE per element even when a template
  // is descended again for a second list (bSkipShared) — states would double-advance
  if (!bSkipShared)
  {
    // the element's visual state machine (states with empty entries never activate)
    for (const plUiElementState* pState : pElement->m_States)
    {
      if (pState != nullptr && !pState->m_sEntry.IsEmpty())
      {
        StateInstance& instance = m_States.ExpandAndGetRef();
        instance.m_pElement = pElement;
        instance.m_pDocumentRoot = pDocumentRoot;
        instance.m_uiSalt = uiChainSalt;
        instance.m_pState = pState;
        instance.m_sEntry = plTempHashedString(pState->m_sEntry.GetData());
      }
    }

    if (const plUiButton* pButton = plDynamicCast<const plUiButton*>(pElement))
    {
      if (!pButton->m_sDisabledBinding.IsEmpty())
      {
        StateInstance& instance = m_States.ExpandAndGetRef();
        instance.m_pElement = pElement;
        instance.m_pDocumentRoot = pDocumentRoot;
        instance.m_uiSalt = uiChainSalt;
        instance.m_pState = nullptr;
        instance.m_sEntry = plTempHashedString(pButton->m_sDisabledBinding.GetData());
      }
    }
  }

  // data-driven lists: register the items entry and discover the template's bindings as
  // per-item KEYS. Nested lists inside item templates are not supported (skipped).
  if (const plUiItemList* pList = plDynamicCast<const plUiItemList*>(pElement))
  {
    constexpr plUInt32 uiMaxSubDocumentDepth = 8;

    if (iListContext < 0 && !pList->m_sItemsBinding.IsEmpty() && pList->m_hItemTemplate.IsValid() && uiDepth < uiMaxSubDocumentDepth)
    {
      const plInt32 iNewList = static_cast<plInt32>(m_Lists.GetCount());

      ListBinding& list = m_Lists.ExpandAndGetRef();
      list.m_pList = pList;
      list.m_uiChainSalt = uiChainSalt;
      list.m_sEntry.Assign(pList->m_sItemsBinding);

      // the template joins the sub-document reload tracking; shared registrations are
      // skipped when another list/sub-document already discovered this document
      bool bAlreadyDiscovered = false;

      for (const SubDocumentRef& ref : m_SubDocuments)
      {
        if (ref.m_hDocument == pList->m_hItemTemplate)
        {
          bAlreadyDiscovered = true;
          break;
        }
      }

      plResourceLock<plUiDocumentResource> pTemplate(pList->m_hItemTemplate, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

      if (!bAlreadyDiscovered)
      {
        SubDocumentRef& ref = m_SubDocuments.ExpandAndGetRef();
        ref.m_hDocument = pList->m_hItemTemplate;
        ref.m_uiChangeCounter = pTemplate->GetCurrentResourceChangeCounter();
      }

      if (pTemplate.GetAcquireResult() == plResourceAcquireResult::Final && pTemplate->GetRoot() != nullptr)
      {
        DiscoverRecursive(pTemplate->GetRoot(), pTemplate->GetRoot(), uiDepth + 1, plUiElementInstance::CombineSalt(uiChainSalt, pList), iNewList, bAlreadyDiscovered);
      }
    }
  }

  // User widgets: every placement is discovered with its chain salt, so bindings, states
  // and clips in duplicate placements keep independent runtime values. Resource reload
  // tracking remains unique per handle (see RefreshSubDocuments).
  if (const plUiSubDocument* pSubDocument = plDynamicCast<const plUiSubDocument*>(pElement))
  {
    constexpr plUInt32 uiMaxSubDocumentDepth = 8; // caps accidental self-references (mirrors the draw walk)

    if (pSubDocument->m_hDocument.IsValid() && uiDepth < uiMaxSubDocumentDepth)
    {
      // Track each resource handle once for reload detection, while still descending
      // into every placement below with its own instance salt.
      bool bAlreadyDiscovered = false;

      for (const SubDocumentRef& ref : m_SubDocuments)
      {
        if (ref.m_hDocument == pSubDocument->m_hDocument)
        {
          bAlreadyDiscovered = true;
          break;
        }
      }

      if (!bAlreadyDiscovered)
      {
        plResourceLock<plUiDocumentResource> pSubResource(pSubDocument->m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

        SubDocumentRef& ref = m_SubDocuments.ExpandAndGetRef();
        ref.m_hDocument = pSubDocument->m_hDocument;
        ref.m_uiChangeCounter = pSubResource->GetCurrentResourceChangeCounter();

        if (pSubResource.GetAcquireResult() == plResourceAcquireResult::Final && pSubResource->GetRoot() != nullptr)
        {
          const plUInt32 uiSubSalt = plUiElementInstance::CombineSalt(uiChainSalt, pElement);
          AddDocumentClips(pSubResource->GetRoot(), pSubResource->GetClips(), uiSubSalt);
          DiscoverRecursive(pSubResource->GetRoot(), pSubResource->GetRoot(), uiDepth + 1, uiSubSalt, iListContext, false);
        }
      }

      if (bAlreadyDiscovered)
      {
        plResourceLock<plUiDocumentResource> pPlacedResource(pSubDocument->m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
        if (pPlacedResource.GetAcquireResult() == plResourceAcquireResult::Final && pPlacedResource->GetRoot() != nullptr)
        {
          const plUInt32 uiSubSalt = plUiElementInstance::CombineSalt(uiChainSalt, pElement);
          AddDocumentClips(pPlacedResource->GetRoot(), pPlacedResource->GetClips(), uiSubSalt);
          DiscoverRecursive(pPlacedResource->GetRoot(), pPlacedResource->GetRoot(), uiDepth + 1, uiSubSalt, iListContext, false);
        }
      }
    }
  }

  for (const plUiElement* pChild : pElement->m_Children)
  {
    if (pChild != nullptr)
    {
      DiscoverRecursive(pChild, pDocumentRoot, uiDepth, uiChainSalt, iListContext, bSkipShared);
    }
  }
}

void plUiBindingState::RemapEntries()
{
  for (Binding& binding : m_Bindings)
  {
    binding.m_uiBoardIndex = plInvalidIndex;
    binding.m_uiLastEntryChangeCounter = 0xFFFFFFFF;

    for (plUInt32 uiBoard = 0; uiBoard < m_Boards.GetCount(); ++uiBoard)
    {
      if (m_Boards[uiBoard].m_pBoard->HasEntry(plTempHashedString(binding.m_sEntry)))
      {
        binding.m_uiBoardIndex = uiBoard;
        break;
      }
    }

    if (binding.m_uiBoardIndex == plInvalidIndex)
    {
      // unbound: clear the resolved flag so rendering falls back to the authored value
      ResolvedValues* pResolved = binding.m_uiSalt == 0
                                    ? m_Resolved.GetValue(binding.m_pElement)
                                    : m_ResolvedInstanced.GetValue(plUiElementInstance{binding.m_pElement, binding.m_uiSalt});
      if (pResolved != nullptr)
      {
        switch (binding.m_Kind)
        {
          case Kind::Visible:
            pResolved->m_uiFlags &= ~ResolvedValues::HasVisible;
            break;
          case Kind::Fill:
            pResolved->m_uiFlags &= ~ResolvedValues::HasFill;
            break;
          case Kind::Text:
            pResolved->m_uiFlags &= ~ResolvedValues::HasText;
            break;
          case Kind::Checked:
            pResolved->m_uiFlags &= ~ResolvedValues::HasChecked;
            break;
          default:
            break;
        }
      }
    }
  }

  // item lists map their items entry the same way
  for (ListBinding& list : m_Lists)
  {
    list.m_uiBoardIndex = plInvalidIndex;
    list.m_uiLastEntryChangeCounter = 0xFFFFFFFF;

    for (plUInt32 uiBoard = 0; uiBoard < m_Boards.GetCount(); ++uiBoard)
    {
      if (m_Boards[uiBoard].m_pBoard->HasEntry(plTempHashedString(list.m_sEntry)))
      {
        list.m_uiBoardIndex = uiBoard;
        break;
      }
    }
  }
}

void plUiBindingState::ResolveBinding(Binding& ref_binding, bool bForce)
{
  if (ref_binding.m_uiBoardIndex == plInvalidIndex)
    return;

  const plBlackboard& board = *m_Boards[ref_binding.m_uiBoardIndex].m_pBoard;

  const plBlackboard::Entry* pEntry = board.GetEntry(plTempHashedString(ref_binding.m_sEntry));
  if (pEntry == nullptr)
    return; // structure counter will trigger a remap

  if (!bForce && pEntry->m_uiChangeCounter == ref_binding.m_uiLastEntryChangeCounter)
    return;

  ref_binding.m_uiLastEntryChangeCounter = pEntry->m_uiChangeCounter;

  ApplyValue(ref_binding, pEntry->m_Value);
}

void plUiBindingState::ApplyValueTo(ResolvedValues& ref_resolved, Kind kind, float fMin, float fMax, const plVariant& value)
{
  switch (kind)
  {
    case Kind::Visible:
    {
      if (value.CanConvertTo<bool>())
      {
        ref_resolved.m_bVisible = value.ConvertTo<bool>();
        ref_resolved.m_uiFlags |= ResolvedValues::HasVisible;
      }
      else
      {
        ref_resolved.m_uiFlags &= ~ResolvedValues::HasVisible;
      }
      break;
    }

    case Kind::Checked:
    {
      if (value.CanConvertTo<bool>())
      {
        ref_resolved.m_bChecked = value.ConvertTo<bool>();
        ref_resolved.m_uiFlags |= ResolvedValues::HasChecked;
      }
      else
      {
        ref_resolved.m_uiFlags &= ~ResolvedValues::HasChecked;
      }
      break;
    }

    case Kind::Fill:
    {
      if (value.CanConvertTo<double>())
      {
        const double fRange = static_cast<double>(fMax) - static_cast<double>(fMin);

        if (plMath::Abs(fRange) < 1e-6)
        {
          ref_resolved.m_fFill = 0.0f;
        }
        else
        {
          ref_resolved.m_fFill = plMath::Clamp(static_cast<float>((value.ConvertTo<double>() - fMin) / fRange), 0.0f, 1.0f);
        }

        ref_resolved.m_uiFlags |= ResolvedValues::HasFill;
      }
      else
      {
        ref_resolved.m_uiFlags &= ~ResolvedValues::HasFill;
      }
      break;
    }

    case Kind::Text:
    {
      if (value.CanConvertTo<plString>())
      {
        plString sNewText = value.ConvertTo<plString>();

        if (ref_resolved.m_sText != sNewText)
        {
          ref_resolved.m_sText = std::move(sNewText);
        }

        ref_resolved.m_uiFlags |= ResolvedValues::HasText;
      }
      else
      {
        ref_resolved.m_uiFlags &= ~ResolvedValues::HasText;
      }
      break;
    }

    default:
      break;
  }
}

void plUiBindingState::ApplyValue(const Binding& binding, const plVariant& value)
{
  ApplyValueTo(GetMutableResolved(binding.m_pElement, binding.m_uiSalt), binding.m_Kind, binding.m_fMin, binding.m_fMax, value);
}

void plUiBindingState::RegisterDynamicInstance(plUInt32 uiId, plUInt32 uiSalt, const plUiDocumentResourceHandle& hDocument, const plSharedPtr<plBlackboard>& pInstanceBoard)
{
  DynamicInstance& instance = m_DynamicInstances.ExpandAndGetRef();
  instance.m_uiId = uiId;
  instance.m_uiSalt = uiSalt;
  instance.m_hDocument = hDocument;
  instance.m_pBoard = pInstanceBoard;

  DiscoverDynamicInstance(instance);
  ResolveDynamicInstance(instance, true);
}

void plUiBindingState::UnregisterDynamicInstance(plUInt32 uiId)
{
  for (plUInt32 i = 0; i < m_DynamicInstances.GetCount(); ++i)
  {
    if (m_DynamicInstances[i].m_uiId != uiId)
      continue;

    // drop the per-instance resolved values
    for (const DynamicInstance::Entry& binding : m_DynamicInstances[i].m_Bindings)
    {
      m_ResolvedInstanced.Remove(plUiElementInstance{binding.m_pElement, m_DynamicInstances[i].m_uiSalt});
    }

    m_DynamicInstances.RemoveAtAndCopy(i);
    return;
  }
}

void plUiBindingState::DiscoverDynamicInstance(DynamicInstance& ref_instance)
{
  ref_instance.m_Bindings.Clear();
  ref_instance.m_uiBoardEntryCounter = 0xFFFFFFFF;
  ref_instance.m_uiBoardStructureCounter = 0xFFFFFFFF;

  plResourceLock<plUiDocumentResource> pResource(ref_instance.m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
  ref_instance.m_uiChangeCounter = pResource->GetCurrentResourceChangeCounter();

  if (pResource.GetAcquireResult() != plResourceAcquireResult::Final || pResource->GetRoot() == nullptr)
    return;

  // collect the tree's bindings (states/clips inside spawned instances are not supported)
  std::function<void(const plUiElement*)> collect = [&](const plUiElement* pElement) {
    auto addEntry = [&](Kind kind, plStringView sEntry, float fMin = 0.0f, float fMax = 1.0f) {
      DynamicInstance::Entry& binding = ref_instance.m_Bindings.ExpandAndGetRef();
      binding.m_pElement = pElement;
      binding.m_Kind = kind;
      binding.m_sEntry.Assign(sEntry);
      binding.m_fMin = fMin;
      binding.m_fMax = fMax;
    };

    if (!pElement->m_sVisibleBinding.IsEmpty())
      addEntry(Kind::Visible, pElement->m_sVisibleBinding);

    if (const plUiProgressBar* pProgressBar = plDynamicCast<const plUiProgressBar*>(pElement))
    {
      if (!pProgressBar->m_sFillAmountBinding.IsEmpty())
        addEntry(Kind::Fill, pProgressBar->m_sFillAmountBinding, pProgressBar->m_fBindingMin, pProgressBar->m_fBindingMax);
    }

    if (const plUiSlider* pSlider = plDynamicCast<const plUiSlider*>(pElement))
    {
      if (!pSlider->m_sValueBinding.IsEmpty())
        addEntry(Kind::Fill, pSlider->m_sValueBinding, pSlider->m_fMin, pSlider->m_fMax);
    }

    if (const plUiToggle* pToggle = plDynamicCast<const plUiToggle*>(pElement))
    {
      if (!pToggle->m_sClickEntryBinding.IsEmpty())
        addEntry(Kind::Checked, pToggle->m_sClickEntryBinding);
    }

    if (const plUiTextInput* pTextInput = plDynamicCast<const plUiTextInput*>(pElement))
    {
      if (!pTextInput->m_sTextBinding.IsEmpty())
        addEntry(Kind::Text, pTextInput->m_sTextBinding);
    }

    if (const plUiText* pText = plDynamicCast<const plUiText*>(pElement))
    {
      if (!pText->m_sTextBinding.IsEmpty())
        addEntry(Kind::Text, pText->m_sTextBinding);
    }

    for (const plUiElement* pChild : pElement->m_Children)
    {
      if (pChild != nullptr)
        collect(pChild);
    }
  };

  collect(pResource->GetRoot());
}

void plUiBindingState::ResolveDynamicInstance(DynamicInstance& ref_instance, bool bForce)
{
  // change detection: the instance's own board (structure + entry counters)
  if (ref_instance.m_pBoard != nullptr)
  {
    const plUInt32 uiStructure = ref_instance.m_pBoard->GetBlackboardChangeCounter();
    const plUInt32 uiEntries = ref_instance.m_pBoard->GetBlackboardEntryChangeCounter();

    if (uiStructure != ref_instance.m_uiBoardStructureCounter || uiEntries != ref_instance.m_uiBoardEntryCounter)
    {
      ref_instance.m_uiBoardStructureCounter = uiStructure;
      ref_instance.m_uiBoardEntryCounter = uiEntries;
      bForce = true;
    }
  }

  if (!bForce)
    return;

  for (const DynamicInstance::Entry& binding : ref_instance.m_Bindings)
  {
    const plTempHashedString sEntry(binding.m_sEntry);

    // instance board first, canvas boards as fallback
    plVariant value;

    if (ref_instance.m_pBoard != nullptr && ref_instance.m_pBoard->HasEntry(sEntry))
    {
      value = ref_instance.m_pBoard->GetEntryValue(sEntry);
    }
    else if (const plBlackboard* pBoard = FindBoardForEntry(sEntry))
    {
      value = pBoard->GetEntryValue(sEntry);
    }

    ApplyValueTo(m_ResolvedInstanced[plUiElementInstance{binding.m_pElement, ref_instance.m_uiSalt}], binding.m_Kind, binding.m_fMin, binding.m_fMax, value);
  }
}

plUInt32 plUiBindingState::GetListItemCount(const plUiItemList* pList) const
{
  for (const ListBinding& list : m_Lists)
  {
    if (list.m_pList == pList)
    {
      return list.m_uiCount;
    }
  }

  return 0;
}

void plUiBindingState::ResolveList(plUInt32 uiList, bool bForce)
{
  ListBinding& list = m_Lists[uiList];

  if (list.m_uiBoardIndex == plInvalidIndex)
    return;

  const plBlackboard& board = *m_Boards[list.m_uiBoardIndex].m_pBoard;

  const plBlackboard::Entry* pEntry = board.GetEntry(plTempHashedString(list.m_sEntry));
  if (pEntry == nullptr)
    return; // structure counter will trigger a remap

  if (!bForce && pEntry->m_uiChangeCounter == list.m_uiLastEntryChangeCounter)
    return;

  list.m_uiLastEntryChangeCounter = pEntry->m_uiChangeCounter;

  // data contract: a plVariantArray = one item per element; a plain number = N placeholders
  const plVariant& value = pEntry->m_Value;

  const plVariantArray* pItems = nullptr;
  plUInt32 uiCount = 0;

  if (value.IsA<plVariantArray>())
  {
    pItems = &value.Get<plVariantArray>();
    uiCount = pItems->GetCount();
  }
  else if (value.CanConvertTo<double>())
  {
    uiCount = static_cast<plUInt32>(plMath::Max(0.0, value.ConvertTo<double>()));
  }

  const plUInt32 uiListSalt = plUiElementInstance::CombineSalt(list.m_uiChainSalt, list.m_pList);

  // drop the instanced values of rows beyond the new count
  for (plUInt32 i = uiCount; i < list.m_uiCount; ++i)
  {
    const plUInt32 uiItemSalt = plUiElementInstance::CombineSaltIndex(uiListSalt, i);

    for (const ItemBinding& binding : m_ItemBindings)
    {
      if (binding.m_uiList == uiList)
      {
        m_ResolvedInstanced.Remove(plUiElementInstance{binding.m_pElement, uiItemSalt});
      }
    }
  }

  list.m_uiCount = uiCount;

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    const plUInt32 uiItemSalt = plUiElementInstance::CombineSaltIndex(uiListSalt, i);

    const plVariantDictionary* pDict = nullptr;
    plVariant plainValue;

    if (pItems != nullptr && i < pItems->GetCount())
    {
      const plVariant& item = (*pItems)[i];

      if (item.IsA<plVariantDictionary>())
      {
        pDict = &item.Get<plVariantDictionary>();
      }
      else
      {
        plainValue = item; // non-dictionary items are exposed under the key "Item"
      }
    }

    for (const ItemBinding& binding : m_ItemBindings)
    {
      if (binding.m_uiList != uiList)
        continue;

      plVariant v;

      if (pDict != nullptr)
      {
        if (const plVariant* pValue = pDict->GetValue(binding.m_sKey))
        {
          v = *pValue;
        }
      }
      else if (plainValue.IsValid() && binding.m_sKey == "Item")
      {
        v = plainValue;
      }

      ApplyValueTo(m_ResolvedInstanced[plUiElementInstance{binding.m_pElement, uiItemSalt}], binding.m_Kind, binding.m_fMin, binding.m_fMax, v);
    }
  }
}

// ————— animation clip playback —————

namespace
{
  float EvaluateTrackFloat(const plUiAnimTrack& track, plArrayPtr<const plUInt32> sortedKeys, float fTime)
  {
    const plUiAnimKey* pPrev = track.m_Keys[sortedKeys[0]];

    if (fTime <= pPrev->m_fTime)
      return pPrev->m_fValue;

    for (plUInt32 i = 1; i < sortedKeys.GetCount(); ++i)
    {
      const plUiAnimKey* pNext = track.m_Keys[sortedKeys[i]];

      if (fTime < pNext->m_fTime)
      {
        const float fSegment = pNext->m_fTime - pPrev->m_fTime;
        const float fT = fSegment > 0.0f ? (fTime - pPrev->m_fTime) / fSegment : 1.0f;
        return plMath::Lerp(pPrev->m_fValue, pNext->m_fValue, ApplyEasing(pPrev->m_Easing, fT));
      }

      pPrev = pNext;
    }

    return pPrev->m_fValue;
  }

  plColor EvaluateTrackColor(const plUiAnimTrack& track, plArrayPtr<const plUInt32> sortedKeys, float fTime)
  {
    const plUiAnimKey* pPrev = track.m_Keys[sortedKeys[0]];

    if (fTime <= pPrev->m_fTime)
      return pPrev->m_Color;

    for (plUInt32 i = 1; i < sortedKeys.GetCount(); ++i)
    {
      const plUiAnimKey* pNext = track.m_Keys[sortedKeys[i]];

      if (fTime < pNext->m_fTime)
      {
        const float fSegment = pNext->m_fTime - pPrev->m_fTime;
        const float fT = fSegment > 0.0f ? (fTime - pPrev->m_fTime) / fSegment : 1.0f;
        const float fEased = ApplyEasing(pPrev->m_Easing, fT);
        return pPrev->m_Color + (pNext->m_Color - pPrev->m_Color) * fEased;
      }

      pPrev = pNext;
    }

    return pPrev->m_Color;
  }

  /// advances a playing clip time according to its loop mode; returns false when a Once clip finished
  bool AdvanceClipTime(const plUiAnimationClip& clip, float fDeltaTime, float& ref_fTime, float& ref_fDirection)
  {
    const float fDuration = plMath::Max(clip.m_fDuration, 0.0001f);

    switch (clip.m_LoopMode)
    {
      case plUiClipLoopMode::Loop:
        ref_fTime += fDeltaTime;
        while (ref_fTime > fDuration)
        {
          ref_fTime -= fDuration;
        }
        return true;

      case plUiClipLoopMode::PingPong:
        ref_fTime += fDeltaTime * ref_fDirection;
        while (ref_fTime < 0.0f || ref_fTime > fDuration)
        {
          if (ref_fTime > fDuration)
          {
            ref_fTime = 2.0f * fDuration - ref_fTime;
            ref_fDirection = -1.0f;
          }
          else
          {
            ref_fTime = -ref_fTime;
            ref_fDirection = 1.0f;
          }
        }
        return true;

      case plUiClipLoopMode::Once:
      default:
        ref_fTime += fDeltaTime;
        if (ref_fTime >= fDuration)
        {
          ref_fTime = fDuration; // hold the last pose
          return false;
        }
        return true;
    }
  }
} // namespace

void plUiBindingState::UpdateClips(plTime deltaTime)
{
  if (m_Clips.IsEmpty())
    return;

  const float fDeltaTime = static_cast<float>(deltaTime.GetSeconds());

  // reset the combined outputs, then accumulate all clips per element
  for (const plUiElementInstance& element : m_AnimatedElements)
  {
    ResolvedValues& resolved = GetMutableResolved(element.m_pElement, element.m_uiSalt);
    resolved.m_vAnimOffset.SetZero();
    resolved.m_vAnimSize.SetZero();
    resolved.m_fAnimRotation = 0.0f;
    resolved.m_fAnimOpacity = 1.0f;
    resolved.m_fAnimScale = 1.0f;
    resolved.m_fAnimFill = 1.0f;
    resolved.m_AnimTint = plColor::White;
    resolved.m_uiFlags |= ResolvedValues::HasAnimation;
  }

  for (plUInt32 uiClip = 0; uiClip < m_Clips.GetCount(); ++uiClip)
  {
    ClipInstance& instance = m_Clips[uiClip];
    const plUiAnimationClip& clip = *instance.m_pClip;
    const float fDuration = plMath::Max(clip.m_fDuration, 0.0001f);

    if (static_cast<plInt32>(uiClip) == m_iPreviewClip)
    {
      // editor preview override: force-evaluate at the scrubbed time
      instance.m_fTime = plMath::Clamp(m_fPreviewTime, 0.0f, fDuration);
      instance.m_bPlaying = false;
    }
    else if (!instance.m_sEntry.IsEmpty())
    {
      if (clip.m_Mode == plUiAnimationMode::Value)
      {
        // the clamped 0..1 entry value IS the playback position
        float fPosition = 0.0f;

        if (const plBlackboard* pBoard = FindBoardForEntry(instance.m_sEntry))
        {
          const plVariant value = pBoard->GetEntryValue(instance.m_sEntry);
          fPosition = value.CanConvertTo<double>() ? plMath::Clamp(static_cast<float>(value.ConvertTo<double>()), 0.0f, 1.0f) : 0.0f;
        }

        instance.m_fTime = fPosition * fDuration;
      }
      else // Trigger: plays forward while the bool entry is true, backward when false
      {
        if (ReadEntryBool(instance.m_sEntry))
        {
          AdvanceClipTime(clip, fDeltaTime, instance.m_fTime, instance.m_fDirection);
        }
        else
        {
          instance.m_fTime = plMath::Max(instance.m_fTime - fDeltaTime, 0.0f);
          instance.m_fDirection = 1.0f;
        }
      }
    }
    else if (instance.m_bStateDriven)
    {
      // A design/editor preview commonly has no blackboard. In that case a state-driven
      // import must leave the authored/base pose intact instead of applying its first
      // keyframe (for example a FadeIn starting hundreds of pixels off-screen).
      if (!instance.m_bStateBindingAvailable)
        continue;

      if (instance.m_bExplicitExit)
      {
        if (!instance.m_bExitPlaying)
          continue;

        if (!AdvanceClipTime(clip, fDeltaTime, instance.m_fTime, instance.m_fDirection))
        {
          instance.m_bExitPlaying = false;
        }
      }
      else if (instance.m_bStateActive)
      {
        AdvanceClipTime(clip, fDeltaTime, instance.m_fTime, instance.m_fDirection);
      }
      else if (instance.m_bHasExplicitExit)
      {
        continue;
      }
      else
      {
        instance.m_fTime = plMath::Max(instance.m_fTime - fDeltaTime, 0.0f);
        instance.m_fDirection = 1.0f;
      }
    }
    else if (instance.m_bPlaying)
    {
      // event-triggered playback; never-started clips hold their time-0 pose
      if (!AdvanceClipTime(clip, fDeltaTime, instance.m_fTime, instance.m_fDirection))
      {
        instance.m_bPlaying = false;
      }
    }

    // evaluate all tracks at the instance time
    for (const TrackInstance& track : instance.m_Tracks)
    {
      if (track.m_pElement == nullptr || track.m_SortedKeys.IsEmpty())
        continue;

      ResolvedValues& resolved = GetMutableResolved(track.m_pElement, instance.m_uiDocumentSalt);

      switch (track.m_pTrack->m_Target)
      {
        case plUiAnimTarget::OffsetX:
          resolved.m_vAnimOffset.x += EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime);
          break;
        case plUiAnimTarget::OffsetY:
          resolved.m_vAnimOffset.y += EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime);
          break;
        case plUiAnimTarget::Opacity:
          resolved.m_fAnimOpacity *= plMath::Clamp(EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime), 0.0f, 1.0f);
          break;
        case plUiAnimTarget::Scale:
          resolved.m_fAnimScale *= plMath::Max(EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime), 0.0f);
          break;
        case plUiAnimTarget::Tint:
          resolved.m_AnimTint *= EvaluateTrackColor(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime);
          break;
        case plUiAnimTarget::Width:
          resolved.m_vAnimSize.x += EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime);
          break;
        case plUiAnimTarget::Height:
          resolved.m_vAnimSize.y += EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime);
          break;
        case plUiAnimTarget::Rotation:
          resolved.m_fAnimRotation += EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime);
          break;
        case plUiAnimTarget::Fill:
          resolved.m_fAnimFill *= plMath::Max(EvaluateTrackFloat(*track.m_pTrack, track.m_SortedKeys, instance.m_fTime), 0.0f);
          break;
        default:
          break;
      }
    }
  }
}

// ————— style-state blends —————

namespace
{
  bool EvaluateStateCondition(const plVariant& value, plUiStateCondition::Enum condition, double fOperand, const plString& sTextOperand)
  {
    switch (condition)
    {
      case plUiStateCondition::IsTrue:
        return value.CanConvertTo<bool>() && value.ConvertTo<bool>();
      case plUiStateCondition::IsFalse:
        return !(value.CanConvertTo<bool>() && value.ConvertTo<bool>());
      default:
        break;
    }

    // a non-empty text operand switches Equal/NotEqual to STRING comparison
    // (flow states: "Ui_Active == 'Start'", "Platform == 'XBOX'")
    if (!sTextOperand.IsEmpty() && (condition == plUiStateCondition::Equal || condition == plUiStateCondition::NotEqual))
    {
      plString sValue;
      if (value.IsValid() && value.CanConvertTo<plString>())
        sValue = value.ConvertTo<plString>();

      const bool bEqual = sValue == sTextOperand;
      return condition == plUiStateCondition::Equal ? bEqual : !bEqual;
    }

    if (!value.CanConvertTo<double>())
      return false;

    const double fValue = value.ConvertTo<double>();

    switch (condition)
    {
      case plUiStateCondition::Equal:
        return fValue == fOperand;
      case plUiStateCondition::NotEqual:
        return fValue != fOperand;
      case plUiStateCondition::Less:
        return fValue < fOperand;
      case plUiStateCondition::LessEqual:
        return fValue <= fOperand;
      case plUiStateCondition::Greater:
        return fValue > fOperand;
      case plUiStateCondition::GreaterEqual:
        return fValue >= fOperand;
      default:
        return false;
    }
  }
} // namespace

void plUiBindingState::UpdateStates(plTime deltaTime, const plUiElementInstance& hovered, const plUiElementInstance& pressed)
{
  const float fDeltaTime = static_cast<float>(deltaTime.GetSeconds());

  // state-driven clip flags are recomputed every frame (UpdateClips runs after this)
  for (ClipInstance& clip : m_Clips)
  {
    clip.m_bStateActive = false;
    clip.m_bStateBindingAvailable = false;
  }

  // ————— element state machines + disabled bindings —————
  // instances are discovered in element order, so states of one element are contiguous;
  // the FIRST state whose condition holds is the element's active state (others blend out)
  const plUiElement* pCurrentElement = nullptr;
  plUInt32 uiCurrentSalt = 0;
  bool bFoundActive = false;
  bool bGroupWasShown = false;

  for (StateInstance& instance : m_States)
  {
    ResolvedValues& resolved = GetMutableResolved(instance.m_pElement, instance.m_uiSalt);
    resolved.m_uiFlags |= ResolvedValues::HasStateBlends;

    if (instance.m_pState == nullptr) // disabled binding (buttons)
    {
      const bool bActive = ReadEntryBool(instance.m_sEntry);
      resolved.m_bDisabled = bActive; // gates interaction immediately, the visual blends

      plUiAnimEasing::Enum easing = plUiAnimEasing::Linear;
      const float fDuration = GetStyleTransition(instance.m_pElement, easing);

      AdvanceBlend(instance.m_fBlend, bActive, fDeltaTime, fDuration);
      resolved.m_fDisabledBlend = ApplyEasing(easing, instance.m_fBlend);
      continue;
    }

    if (instance.m_pElement != pCurrentElement || instance.m_uiSalt != uiCurrentSalt)
    {
      // first state of this element: reset the blend list + visibility override
      pCurrentElement = instance.m_pElement;
      uiCurrentSalt = instance.m_uiSalt;
      bFoundActive = false;
      bGroupWasShown = resolved.m_iStateVisibility == 1;
      resolved.m_uiStateBlendCount = 0;
      resolved.m_iStateVisibility = -1;
      resolved.m_bStateInteractionHidden = false;
    }

    // evaluate the condition (only if no earlier state of this element already matched)
    bool bActive = false;

    const plBlackboard* pStateBoard = FindBoardForEntry(instance.m_sEntry);

    if (!bFoundActive && pStateBoard != nullptr)
    {
      const plVariant value = pStateBoard->GetEntryValue(instance.m_sEntry);
      bActive = EvaluateStateCondition(value, static_cast<plUiStateCondition::Enum>(instance.m_pState->m_Condition.GetValue()), instance.m_pState->m_fValue, instance.m_pState->m_sValueText);
      bFoundActive = bFoundActive || bActive;
    }

    if (instance.m_iPlayClipIndex >= 0 && pStateBoard != nullptr)
    {
      m_Clips[instance.m_iPlayClipIndex].m_bStateBindingAvailable = true;
    }
    if (instance.m_iExitClipIndex >= 0 && pStateBoard != nullptr)
    {
      m_Clips[instance.m_iExitClipIndex].m_bStateBindingAvailable = true;
    }

    if (bActive && instance.m_pState->m_Visibility.GetValue() != plUiStateVisibility::NoChange)
    {
      if (instance.m_pState->m_Visibility.GetValue() == plUiStateVisibility::Show)
      {
        resolved.m_iStateVisibility = 1;
      }
      else
      {
        resolved.m_bStateInteractionHidden = true;
        if (!instance.m_bWasActive)
        {
          instance.m_bHideDelayed = bGroupWasShown && instance.m_pState->m_fHideDelay > 0.0f;
          instance.m_fHideElapsed = 0.0f;
        }

        if (instance.m_bHideDelayed)
          instance.m_fHideElapsed += fDeltaTime;

        if (instance.m_bHideDelayed && instance.m_fHideElapsed < instance.m_pState->m_fHideDelay)
          resolved.m_iStateVisibility = 1;
        else
          resolved.m_iStateVisibility = 0;
      }
    }
    else
    {
      instance.m_fHideElapsed = 0.0f;
      instance.m_bHideDelayed = false;
    }

    if (bActive && instance.m_iPlayClipIndex >= 0)
    {
      ClipInstance& enterClip = m_Clips[instance.m_iPlayClipIndex];
      if (!instance.m_bWasActive && instance.m_iExitClipIndex >= 0)
      {
        enterClip.m_fTime = 0.0f;
        enterClip.m_fDirection = 1.0f;
      }
      enterClip.m_bStateActive = true;
    }

    if (bActive && instance.m_iExitClipIndex >= 0)
    {
      ClipInstance& exitClip = m_Clips[instance.m_iExitClipIndex];
      exitClip.m_bExitPlaying = false;
      exitClip.m_fTime = 0.0f;
      exitClip.m_fDirection = 1.0f;
    }
    else if (!bActive && instance.m_bWasActive && instance.m_iExitClipIndex >= 0)
    {
      ClipInstance& exitClip = m_Clips[instance.m_iExitClipIndex];
      exitClip.m_bExitPlaying = true;
      exitClip.m_fTime = 0.0f;
      exitClip.m_fDirection = 1.0f;
    }

    instance.m_bWasActive = bActive;

    AdvanceBlend(instance.m_fBlend, bActive, fDeltaTime, instance.m_pState->m_fEnterDuration);

    float fEffective = ApplyEasing(static_cast<plUiAnimEasing::Enum>(instance.m_pState->m_Easing.GetValue()), instance.m_fBlend);

    if (instance.m_pState->m_bPulse)
    {
      if (bActive)
      {
        instance.m_fPulsePhase += fDeltaTime;
      }
      else
      {
        instance.m_fPulsePhase = 0.0f;
      }

      // triangle wave starting at 1: 1 -> 0 -> 1 over the pulse period
      const float fPeriod = plMath::Max(instance.m_pState->m_fPulsePeriod, 0.05f);
      const float fPhase = plMath::Mod(instance.m_fPulsePhase, fPeriod) / fPeriod;
      const float fPulse = 1.0f - (fPhase < 0.5f ? fPhase * 2.0f : 2.0f - fPhase * 2.0f);
      fEffective *= fPulse;
    }

    // expose for the draw-time style cross-fade (only states with a style change visuals)
    if (fEffective > 0.0f && instance.m_pState->m_hStyle.IsValid() && resolved.m_uiStateBlendCount < PL_ARRAY_SIZE(resolved.m_StateBlends))
    {
      ResolvedValues::StateBlendInfo& blendInfo = resolved.m_StateBlends[resolved.m_uiStateBlendCount++];
      blendInfo.m_pState = instance.m_pState;
      blendInfo.m_fBlend = fEffective;
    }
  }

  // interaction blends: buttons with a style blend hover/pressed over its transition
  // duration; unstyled buttons keep the instant color swap
  {
    auto ensureTracked = [&](const plUiElementInstance& element) {
      if (element.m_pElement != nullptr && element.m_pElement->m_hStyle.IsValid() && !m_InteractionBlends.Contains(element) &&
          plDynamicCast<const plUiButton*>(element.m_pElement) != nullptr)
      {
        m_InteractionBlends[element] = plVec2::MakeZero();
      }
    };

    ensureTracked(hovered);
    ensureTracked(pressed);

    plHybridArray<plUiElementInstance, 4> toRemove;

    for (auto it : m_InteractionBlends)
    {
      const plUiElementInstance element = it.Key();
      plVec2& blends = it.Value();

      const bool bHovered = element == hovered;
      const bool bPressed = element == pressed;

      plUiAnimEasing::Enum easing = plUiAnimEasing::Linear;
      const float fDuration = GetStyleTransition(element.m_pElement, easing);

      AdvanceBlend(blends.x, bHovered || bPressed, fDeltaTime, fDuration);
      AdvanceBlend(blends.y, bPressed, fDeltaTime, fDuration);

      ResolvedValues& resolved = GetMutableResolved(element.m_pElement, element.m_uiSalt);
      resolved.m_uiFlags |= ResolvedValues::HasStateBlends;
      resolved.m_fHoverBlend = ApplyEasing(easing, blends.x);
      resolved.m_fPressedBlend = ApplyEasing(easing, blends.y);
      resolved.m_uiHoverSalt = element.m_uiSalt;
      resolved.m_uiPressedSalt = element.m_uiSalt;

      if (blends.x <= 0.0f && blends.y <= 0.0f && !bHovered && !bPressed)
      {
        toRemove.PushBack(element); // settled: stop tracking
      }
    }

    for (const plUiElementInstance& element : toRemove)
    {
      m_InteractionBlends.Remove(element);
    }
  }
}

void plUiBindingState::Update(plTime deltaTime, const plUiElementInstance& hovered, const plUiElementInstance& pressed)
{
  // states first: they compute the state-driven clip flags UpdateClips consumes
  UpdateStates(deltaTime, hovered, pressed);
  UpdateClips(deltaTime);

  // spawned instances resolve against their OWN boards even without canvas boards
  if (m_Boards.IsEmpty())
  {
    for (DynamicInstance& instance : m_DynamicInstances)
    {
      ResolveDynamicInstance(instance, false);
    }

    return;
  }

  if (m_Bindings.IsEmpty() && m_Lists.IsEmpty() && m_DynamicInstances.IsEmpty())
    return;

  // tier 1: entry added/removed on any board -> remap all bindings + full resolve
  bool bRemap = false;

  for (BoardState& state : m_Boards)
  {
    if (state.m_uiStructureCounter != state.m_pBoard->GetBlackboardChangeCounter())
    {
      state.m_uiStructureCounter = state.m_pBoard->GetBlackboardChangeCounter();
      bRemap = true;
    }
  }

  if (bRemap)
  {
    RemapEntries();

    for (Binding& binding : m_Bindings)
    {
      ResolveBinding(binding, true);
    }

    for (plUInt32 uiList = 0; uiList < m_Lists.GetCount(); ++uiList)
    {
      ResolveList(uiList, true);
    }

    for (DynamicInstance& instance : m_DynamicInstances)
    {
      ResolveDynamicInstance(instance, true);
    }

    for (BoardState& state : m_Boards)
    {
      state.m_uiEntryCounter = state.m_pBoard->GetBlackboardEntryChangeCounter();
    }

    return;
  }

  // tier 2: some value changed on a board -> per-entry counter compare
  bool bAnyBoardChanged = false;

  for (plUInt32 uiBoard = 0; uiBoard < m_Boards.GetCount(); ++uiBoard)
  {
    BoardState& state = m_Boards[uiBoard];

    const plUInt32 uiEntryCounter = state.m_pBoard->GetBlackboardEntryChangeCounter();
    if (state.m_uiEntryCounter == uiEntryCounter)
      continue;

    state.m_uiEntryCounter = uiEntryCounter;
    bAnyBoardChanged = true;

    for (Binding& binding : m_Bindings)
    {
      if (binding.m_uiBoardIndex == uiBoard)
      {
        ResolveBinding(binding, false);
      }
    }

    for (plUInt32 uiList = 0; uiList < m_Lists.GetCount(); ++uiList)
    {
      if (m_Lists[uiList].m_uiBoardIndex == uiBoard)
      {
        ResolveList(uiList, false);
      }
    }
  }

  // spawned instances: own-board counters checked inside; canvas changes force a refresh
  for (DynamicInstance& instance : m_DynamicInstances)
  {
    ResolveDynamicInstance(instance, bAnyBoardChanged);
  }
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Components_Implementation_UiBindingState);
