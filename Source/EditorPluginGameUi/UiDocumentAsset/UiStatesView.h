#pragma once

#include <EditorPluginGameUi/UiDocumentAsset/UiStatesPanel.h>
#include <Foundation/Types/UniquePtr.h>

class plEditorUiContent;
class plUiStatesSurface;

/// \brief The engine-drawn states panel: the selected element's state chain as chips (Normal, Hover and Pressed for
/// buttons, each state with its condition, Disabled), then the state table (State, When Entry, Condition, Value,
/// Style, Clip, Visibility, Preview) with the Disabled row of buttons, and Add State / Remove State.
///
/// Names, entries and values edit in place (double-click or F2); the entry cells also offer the template entries,
/// the choice cells open menus, the style cell opens the asset browser.
///
/// The view keeps the panel's state; the content that draws it comes from CreateContent, and the document window shows
/// it as a panel.
class plUiStatesView : public plUiStatesPanelBase
{
public:
  plUiStatesView();
  ~plUiStatesView();

  /// The content that draws the panel; it must go before the view.
  plUniquePtr<plEditorUiContent> CreateContent();

  void SetTemplateEntries(const plDynamicArray<plString>& entries) override;
  void SetClipNames(const plDynamicArray<plString>& clipNames) override;
  void SetElement(const ElementStateInfo* pInfo) override;

private:
  friend class plUiStatesSurface;

  /// Reports StateEdited with the row's values.
  void EmitStateEdited(const StateRow& row);

  /// The content clears its previews and ends an open edit; nothing without a content.
  void ResetSurface(bool bNewElement);

  ElementStateInfo m_Info;
  bool m_bHasElement = false;
  plDynamicArray<plString> m_TemplateEntries;
  plDynamicArray<plString> m_ClipNames;

  plUiStatesSurface* m_pSurface = nullptr;
};
