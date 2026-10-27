#pragma once

#include <Foundation/Containers/HashTable.h>
#include <Foundation/Types/Delegate.h>
#include <Foundation/Types/UniquePtr.h>
#include <Foundation/Types/Uuid.h>
#include <EditorUi/Content.h>

class plDocument;
class plDocumentObject;
class plEditorUiTree;
class plRTTI;
struct plEditorUiTreeEvent;

/// \brief The element hierarchy of the UI document window, engine-drawn: a header row, then the elements as a tree
/// ("Type: Name") with their type icon in the UI colour, marks for bindings, localization and a UI Style, and the
/// Visible and Lock columns.
///
/// The view reads the document; changes go out through the delegates, which the window applies in transactions.
class plUiHierarchyView : public plEditorUiContent
{
public:
  explicit plUiHierarchyView(plDocument* pDocument);
  ~plUiHierarchyView();

  /// Re-reads the elements; keeps expansion and scroll, expands new elements.
  void Refresh();

  /// Shows the document selection.
  void SetSelection(plArrayPtr<const plUuid> selection);

  plDelegate<void(plArrayPtr<const plUuid>)> m_OnSelectionChanged;

  /// The Visible (bLock false) or Lock column of an element was clicked.
  plDelegate<void(const plUuid&, bool)> m_OnToggle;

  plDelegate<void()> m_OnDelete;
  plDelegate<void(const plRTTI*)> m_OnAdd;

  /// Move the elements under a new parent (invalid = the document root) at an index (-1 = append).
  plDelegate<void(plArrayPtr<const plUuid>, const plUuid&, plInt32)> m_OnMove;

  /// A message for the status row (a refused drop).
  plDelegate<void(plStringView)> m_OnMessage;

protected:
  void OnUiUpdate() override;

private:
  class Adapter;
  friend class Adapter;

  void BuildDocument();
  void OnTreeEvent(const plEditorUiTreeEvent& event);
  void OnPointer(const struct plEditorUiPointerEvent& event);
  void ShowContextMenu(const plVec2& vPosition);
  void CollectItems();
  void Move(plArrayPtr<const plUInt64> items, plUInt64 uiNewParent, plInt32 iIndex);
  const plDocumentObject* GetObjectOfItem(plUInt64 uiItem) const;
  void GetSelectedGuids(plDynamicArray<plUuid>& out_guids) const;

  plDocument* m_pDocument = nullptr;
  plUniquePtr<Adapter> m_pAdapter;
  plUniquePtr<plEditorUiTree> m_pTree;
  plHashTable<plUInt64, plUuid> m_ItemGuids;
  plHashTable<plUInt64, plUInt64> m_ItemParents;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bSyncing = false;
  plEventSubscriptionID m_PointerSubscription = 0;
};
