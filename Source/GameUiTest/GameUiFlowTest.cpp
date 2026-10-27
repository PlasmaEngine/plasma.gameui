#include <GameUiTest/GameUiTestPCH.h>

#include <GameUiPlugin/Components/UiBindingState.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>

namespace
{
  class TestUiCanvasComponent : public plUiCanvasComponent
  {
  public:
    using plUiCanvasComponent::ExecuteEventReactions;

    plUiBindingState& GetBindingState() { return m_BindingState; }
    const plSharedPtr<plBlackboard>& GetLocalBlackboard() const { return m_pLocalBlackboard; }
  };

  plUiEventReaction* AddReaction(plUiSubDocument& placement, plStringView sEvent, plUiEventActionType::Enum type)
  {
    plUiEventReaction* pReaction = PL_DEFAULT_NEW(plUiEventReaction);
    pReaction->m_sEvent = sEvent;
    placement.m_Reactions.PushBack(pReaction);

    plUiEventAction* pAction = PL_DEFAULT_NEW(plUiEventAction);
    pAction->m_Type = type;
    pReaction->m_Actions.PushBack(pAction);
    return pReaction;
  }
} // namespace

PL_CREATE_SIMPLE_TEST_GROUP(GameUi);

PL_CREATE_SIMPLE_TEST(GameUi, ParentSubDocumentFlow)
{
  plUiPanel root;
  root.m_sName = "Root";

  plUiElement* pPage = PL_DEFAULT_NEW(plUiElement);
  pPage->m_sName = "Page";
  pPage->m_bVisible = false;
  root.m_Children.PushBack(pPage);

  {
    plUiElementState* pShow = PL_DEFAULT_NEW(plUiElementState);
    pShow->m_sName = "ShowSettings";
    pShow->m_sEntry = "State";
    pShow->m_Condition = plUiStateCondition::Equal;
    pShow->m_sValueText = "Settings";
    pShow->m_Visibility = plUiStateVisibility::Show;
    pPage->m_States.PushBack(pShow);

    plUiElementState* pHide = PL_DEFAULT_NEW(plUiElementState);
    pHide->m_sName = "HideSettings";
    pHide->m_sEntry = "State";
    pHide->m_Condition = plUiStateCondition::NotEqual;
    pHide->m_sValueText = "Settings";
    pHide->m_Visibility = plUiStateVisibility::Hide;
    pHide->m_fHideDelay = 0.6f;
    pPage->m_States.PushBack(pHide);
  }

  plDynamicArray<plUiAnimationClip*> clips;
  plUiBindingState binding;
  binding.SetDocument(&root, clips, 1);

  plSharedPtr<plBlackboard> pExternal = plBlackboard::Create();
  plSharedPtr<plBlackboard> pLocal = plBlackboard::Create();
  plHybridArray<plSharedPtr<plBlackboard>, 2> boards;
  boards.PushBack(pExternal);
  boards.PushBack(pLocal);
  binding.SetBlackboards(boards);

  const plUiElementInstance noInteraction;
  const plUiElementInstance page{pPage, 0};

  PL_TEST_BLOCK(plTestBlock::Enabled, "Missing state preserves authored visibility")
  {
    binding.Update(plTime::MakeZero(), noInteraction, noInteraction);
    const plUiBindingState::ResolvedValues* pResolved = binding.GetResolved(page);
    PL_TEST_BOOL(pResolved != nullptr);
    PL_TEST_INT(pResolved->m_iStateVisibility, -1);
    PL_TEST_BOOL(!pResolved->m_bStateInteractionHidden);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "External entries take priority over local fallback")
  {
    pExternal->SetEntryValue("State", "Settings");
    pLocal->SetEntryValue("State", "Main");
    PL_TEST_BOOL(binding.FindBoardForEntry(plTempHashedString("State")) == pExternal.Borrow());

    binding.Update(plTime::MakeZero(), noInteraction, noInteraction);
    const plUiBindingState::ResolvedValues* pResolved = binding.GetResolved(page);
    PL_TEST_BOOL(pResolved != nullptr);
    PL_TEST_INT(pResolved->m_iStateVisibility, 1);
    PL_TEST_BOOL(!pResolved->m_bStateInteractionHidden);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Outgoing page is non-interactive until delayed hide completes")
  {
    pExternal->SetEntryValue("State", "Main");
    binding.Update(plTime::MakeFromMilliseconds(100), noInteraction, noInteraction);

    const plUiBindingState::ResolvedValues* pResolved = binding.GetResolved(page);
    PL_TEST_BOOL(pResolved != nullptr);
    PL_TEST_INT(pResolved->m_iStateVisibility, 1);
    PL_TEST_BOOL(pResolved->m_bStateInteractionHidden);

    binding.Update(plTime::MakeFromMilliseconds(500), noInteraction, noInteraction);
    pResolved = binding.GetResolved(page);
    PL_TEST_BOOL(pResolved != nullptr);
    PL_TEST_INT(pResolved->m_iStateVisibility, 0);
    PL_TEST_BOOL(pResolved->m_bStateInteractionHidden);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Rapid reversal cancels a pending hide")
  {
    pExternal->SetEntryValue("State", "Settings");
    binding.Update(plTime::MakeZero(), noInteraction, noInteraction);

    const plUiBindingState::ResolvedValues* pResolved = binding.GetResolved(page);
    PL_TEST_BOOL(pResolved != nullptr);
    PL_TEST_INT(pResolved->m_iStateVisibility, 1);
    PL_TEST_BOOL(!pResolved->m_bStateInteractionHidden);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Direct visibility is isolated per placement instance")
  {
    const plUiElementInstance first{pPage, 11};
    const plUiElementInstance second{pPage, 22};
    binding.SetFlowVisibility(first, false);
    binding.SetFlowVisibility(second, true);

    const plUiBindingState::ResolvedValues* pFirst = binding.GetResolved(first);
    const plUiBindingState::ResolvedValues* pSecond = binding.GetResolved(second);
    PL_TEST_BOOL(pFirst != nullptr && pSecond != nullptr);
    PL_TEST_INT(pFirst->m_iFlowVisibility, 0);
    PL_TEST_INT(pSecond->m_iFlowVisibility, 1);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Parent actions and nearest reaction routing")
  {
    plUiAnimationClip* pClip = PL_DEFAULT_NEW(plUiAnimationClip);
    pClip->m_sName = "ParentClip";
    pClip->m_fDuration = 1.0f;

    plUiAnimTrack* pTrack = PL_DEFAULT_NEW(plUiAnimTrack);
    pTrack->m_sElement = "Page";
    pTrack->m_Target = plUiAnimTarget::OffsetX;
    pClip->m_Tracks.PushBack(pTrack);

    plUiAnimKey* pStart = PL_DEFAULT_NEW(plUiAnimKey);
    pStart->m_fTime = 0.0f;
    pStart->m_fValue = 0.0f;
    pTrack->m_Keys.PushBack(pStart);

    plUiAnimKey* pEnd = PL_DEFAULT_NEW(plUiAnimKey);
    pEnd->m_fTime = 1.0f;
    pEnd->m_fValue = 100.0f;
    pTrack->m_Keys.PushBack(pEnd);

    clips.PushBack(pClip);

    plUiSubDocument outerPlacement;
    plUiEventReaction* pOuterRoute = AddReaction(outerPlacement, "Route", plUiEventActionType::SetState);
    pOuterRoute->m_Actions[0]->m_sEntry = "RouteResult";
    pOuterRoute->m_Actions[0]->m_Value = "Outer";

    plUiSubDocument innerPlacement;
    plUiEventReaction* pInnerRoute = AddReaction(innerPlacement, "Route", plUiEventActionType::SetState);
    pInnerRoute->m_Actions[0]->m_sEntry = "RouteResult";
    pInnerRoute->m_Actions[0]->m_Value = "Inner";

    plUiEventReaction* pLocal = AddReaction(innerPlacement, "Local", plUiEventActionType::SetState);
    pLocal->m_Actions[0]->m_sEntry = "LocalOnly";
    pLocal->m_Actions[0]->m_Value = 42;

    plUiEventReaction* pHide = AddReaction(innerPlacement, "Hide", plUiEventActionType::Hide);
    pHide->m_Actions[0]->m_sTarget = "Page";
    plUiEventReaction* pToggle = AddReaction(innerPlacement, "Toggle", plUiEventActionType::ToggleVisibility);
    pToggle->m_Actions[0]->m_sTarget = "Page";
    plUiEventReaction* pShow = AddReaction(innerPlacement, "Show", plUiEventActionType::Show);
    pShow->m_Actions[0]->m_sTarget = "Page";
    plUiEventReaction* pPlay = AddReaction(innerPlacement, "Play", plUiEventActionType::PlayClip);
    pPlay->m_Actions[0]->m_sClip = "ParentClip";

    TestUiCanvasComponent canvas;
    plSharedPtr<plBlackboard> pReactionBoard = plBlackboard::Create();
    pReactionBoard->SetEntryValue("RouteResult", "");

    plHybridArray<plSharedPtr<plBlackboard>, 2> reactionBoards;
    reactionBoards.PushBack(pReactionBoard);
    reactionBoards.PushBack(canvas.GetLocalBlackboard());
    canvas.GetBindingState().SetBlackboards(reactionBoards);
    canvas.GetBindingState().SetDocument(&root, clips, 2);

    plHybridArray<plUiFocusable::RouteEntry, 2> route;
    plUiFocusable::RouteEntry& outerRoute = route.ExpandAndGetRef();
    outerRoute.m_pPlacement = &outerPlacement;
    outerRoute.m_pParentDocumentRoot = &root;
    plUiFocusable::RouteEntry& innerRoute = route.ExpandAndGetRef();
    innerRoute.m_pPlacement = &innerPlacement;
    innerRoute.m_pParentDocumentRoot = &root;

    PL_TEST_BOOL(canvas.ExecuteEventReactions("Route", route));
    PL_TEST_STRING(pReactionBoard->GetEntryValue(plTempHashedString("RouteResult")).ConvertTo<plString>(), "Inner");

    PL_TEST_BOOL(canvas.ExecuteEventReactions("Local", route));
    PL_TEST_INT(canvas.GetLocalBlackboard()->GetEntryValue(plTempHashedString("LocalOnly")).ConvertTo<plInt32>(), 42);

    PL_TEST_BOOL(canvas.ExecuteEventReactions("Hide", route));
    PL_TEST_INT(canvas.GetBindingState().GetResolved(page)->m_iFlowVisibility, 0);
    PL_TEST_BOOL(canvas.ExecuteEventReactions("Toggle", route));
    PL_TEST_INT(canvas.GetBindingState().GetResolved(page)->m_iFlowVisibility, 1);
    PL_TEST_BOOL(canvas.ExecuteEventReactions("Show", route));
    PL_TEST_INT(canvas.GetBindingState().GetResolved(page)->m_iFlowVisibility, 1);

    PL_TEST_BOOL(canvas.ExecuteEventReactions("Play", route));
    canvas.GetBindingState().Update(plTime::MakeFromMilliseconds(500), noInteraction, noInteraction);
    PL_TEST_FLOAT(canvas.GetBindingState().GetResolved(page)->m_vAnimOffset.x, 50.0f, 0.01f);

    canvas.GetBindingState().Clear();
    clips.RemoveAndCopy(pClip);
    PL_DEFAULT_DELETE(pClip);
  }
}
