#pragma once

#include <GameUiPlugin/Elements/UiAnimationClip.h>
#include <GameUiPlugin/Elements/UiElements.h>

/// \brief A named snapshot of the mock-blackboard table (design-time only, not compiled
/// into the resource). Saved on the asset so scenarios are shared with the document.
class plUiMockScenario : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiMockScenario, plReflectedClass);

public:
  plString m_sName;
  plString m_sData; ///< newline-separated name/value pairs (the "UiMockBlackboard" msg format)
};

/// \brief Editable properties of a UI Document asset.
///
/// The document edits the children of an implicit full-canvas root panel;
/// the root itself is created during transform.
class plUiDocumentAssetProperties : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiDocumentAssetProperties, plReflectedClass);

public:
  plUiDocumentAssetProperties();
  ~plUiDocumentAssetProperties();

  /// Mock-blackboard scenario presets (tool-managed via the Bindings dock chips).
  plHybridArray<plUiMockScenario*, 2> m_MockScenarios;

  /// Design-time contract only (NOT used at runtime): declares which blackboard entries
  /// this UI binds to. Drives the binding entry pickers, validation and the mock table.
  /// At runtime the game pairs the canvas with a plBlackboardComponent (typically using
  /// the same template).
  plString m_sBlackboardTemplate;

  plHybridArray<plUiElement*, 4> m_Children;

  /// Named keyframe animation clips, edited in the Animation timeline panel.
  plHybridArray<plUiAnimationClip*, 2> m_Animations;
};
