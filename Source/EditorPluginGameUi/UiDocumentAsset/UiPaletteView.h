#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Types/Delegate.h>
#include <Foundation/Types/UniquePtr.h>
#include <EditorUi/Content.h>

class plRTTI;
class plEditorUiSearchField;
struct plEditorUiSearchFieldEvent;

/// \brief The widget palette of the UI document window, engine-drawn: a search field, then the widget types in
/// groups (Containers, Text & Visual, Controls, Advanced), two per row. A click adds the type as a child of the
/// selected element; dragging a type onto the canvas creates it where it is dropped.
class plUiPaletteView : public plEditorUiContent
{
public:
  plUiPaletteView();
  ~plUiPaletteView();

  /// The MIME format a dragged widget type travels in; the data is the type name.
  static constexpr const char* s_szMimeType = "application/x-plasmaui-widget";

  /// A palette entry was clicked.
  plDelegate<void(const plRTTI*)> m_OnAdd;

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  struct Group
  {
    const char* m_szTitleKey = nullptr;
    plHybridArray<const plRTTI*, 8> m_Types;
    bool m_bCollapsed = false;
  };

  void BuildDocument();
  void OnPointer(const struct plEditorUiPointerEvent& event);
  void OnSearch(const plEditorUiSearchFieldEvent& event);
  bool Matches(const plRTTI* pType) const;
  const plRTTI* GetTypeOfElement(plStringView sElement) const;

  plHybridArray<Group, 4> m_Groups;
  plUniquePtr<plEditorUiSearchField> m_pSearch;
  plString m_sFilter;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
  bool m_bRefocusSearch = false;
  plEventSubscriptionID m_PointerSubscription = 0;
};
