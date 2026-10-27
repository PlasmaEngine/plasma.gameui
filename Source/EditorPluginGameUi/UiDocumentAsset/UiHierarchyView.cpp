#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiHierarchyView.h>
#include <EditorUi/Menu.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Tree.h>
#include <Foundation/Algorithm/HashingUtils.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

namespace
{
  constexpr const char* s_szTreeName = "GameUi.HierarchyTree";
  constexpr float s_fHeaderHeight = 24.0f;
  constexpr plUInt32 s_uiDeleteId = 100;

  enum Column : plUInt32
  {
    Bound,
    Localized,
    Styled,
    Visible,
    Locked,
  };

  /// The context menu's Add entries, in menu order.
  struct AddEntry
  {
    const char* m_szLabelKey;
    const plRTTI* (*m_GetType)();
  };

  const AddEntry s_AddEntries[] = {
    {"GameUi.Menu.AddPanel", []() { return plGetStaticRTTI<plUiPanel>(); }},
    {"GameUi.Menu.AddStackPanel", []() { return plGetStaticRTTI<plUiStackPanel>(); }},
    {"GameUi.Menu.AddGridPanel", []() { return plGetStaticRTTI<plUiGridPanel>(); }},
    {"GameUi.Menu.AddImage", []() { return plGetStaticRTTI<plUiImage>(); }},
    {"GameUi.Menu.AddText", []() { return plGetStaticRTTI<plUiText>(); }},
    {"GameUi.Menu.AddButton", []() { return plGetStaticRTTI<plUiButton>(); }},
    {"GameUi.Menu.AddProgressBar", []() { return plGetStaticRTTI<plUiProgressBar>(); }},
    {"GameUi.Menu.AddScrollView", []() { return plGetStaticRTTI<plUiScrollView>(); }},
    {"GameUi.Menu.AddToggle", []() { return plGetStaticRTTI<plUiToggle>(); }},
    {"GameUi.Menu.AddSlider", []() { return plGetStaticRTTI<plUiSlider>(); }},
    {"GameUi.Menu.AddTextInput", []() { return plGetStaticRTTI<plUiTextInput>(); }},
    {"GameUi.Menu.AddItemList", []() { return plGetStaticRTTI<plUiItemList>(); }},
    {"GameUi.Menu.AddSubDocument", []() { return plGetStaticRTTI<plUiSubDocument>(); }},
    {"GameUi.Menu.AddSpacer", []() { return plGetStaticRTTI<plUiSpacer>(); }},
    {"GameUi.Menu.AddPath", []() { return plGetStaticRTTI<plUiPath>(); }},
  };

  plUInt64 IdOf(const plUuid& guid)
  {
    return plHashingUtils::xxHash64(&guid, sizeof(plUuid)) | 1;
  }

  const plDocumentObject* GetPropertiesObject(const plDocument* pDocument)
  {
    return pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0];
  }

  /// The element children of an object, in property-index order.
  void GetElementChildren(const plDocumentObject* pParent, plHybridArray<const plDocumentObject*, 16>& out_children)
  {
    out_children.Clear();
    for (const plDocumentObject* pChild : pParent->GetChildren())
    {
      if (pChild->GetParentProperty() == "Children")
        out_children.PushBack(pChild);
    }

    out_children.Sort([](const plDocumentObject* a, const plDocumentObject* b)
      { return a->GetPropertyIndex().ConvertTo<plInt32>() < b->GetPropertyIndex().ConvertTo<plInt32>(); });
  }

  bool GetBool(const plDocumentObject* pObject, const char* szProperty, bool bDefault)
  {
    const plVariant value = pObject->GetTypeAccessor().GetValue(szProperty);
    return value.IsA<bool>() ? value.Get<bool>() : bDefault;
  }

  bool HasString(const plDocumentObject* pObject, const char* szProperty)
  {
    const plVariant value = pObject->GetTypeAccessor().GetValue(szProperty);
    return value.IsA<plString>() && !value.Get<plString>().IsEmpty();
  }

  bool IsBound(const plDocumentObject* pObject)
  {
    for (const char* szProperty : {"VisibleBinding", "FillAmountBinding", "TextBinding", "ClickEntryBinding"})
    {
      if (HasString(pObject, szProperty))
        return true;
    }

    return false;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Adapter

class plUiHierarchyView::Adapter : public plEditorUiTreeAdapter
{
public:
  explicit Adapter(plUiHierarchyView* pOwner)
    : m_pOwner(pOwner)
  {
  }

  void GetChildren(plUInt64 uiParent, plDynamicArray<plUInt64>& out_children) const override
  {
    out_children.Clear();

    const plDocumentObject* pParent = uiParent == 0 ? GetPropertiesObject(m_pOwner->m_pDocument) : m_pOwner->GetObjectOfItem(uiParent);
    if (pParent == nullptr)
      return;

    plHybridArray<const plDocumentObject*, 16> children;
    GetElementChildren(pParent, children);

    for (const plDocumentObject* pChild : children)
      out_children.PushBack(IdOf(pChild->GetGuid()));
  }

  plUInt64 GetParent(plUInt64 uiItem) const override
  {
    plUInt64 uiParent = InvalidParent;
    m_pOwner->m_ItemParents.TryGetValue(uiItem, uiParent);
    return uiParent;
  }

  void GetRow(plUInt64 uiItem, plEditorUiTreeRow& out_row) const override
  {
    const plDocumentObject* pObject = m_pOwner->GetObjectOfItem(uiItem);
    if (pObject == nullptr)
      return;

    plStringBuilder sText = plTranslate(pObject->GetType()->GetTypeName());
    const plVariant name = pObject->GetTypeAccessor().GetValue("Name");
    if (name.IsA<plString>() && !name.Get<plString>().IsEmpty())
      sText.AppendFormat(": {}", name.Get<plString>());

    out_row.m_sText = sText;
    out_row.m_sIcon = plEditorUi::MonochromeIconPath(plGameUiEditor::TypeIconPath(pObject->GetType()));
    out_row.m_IconColor = plGameUiEditor::UiColor();

    // hidden elements read dimmed and struck through
    if (!GetBool(pObject, "Visible", true))
    {
      out_row.m_TextStyle = plEditorUiTreeTextStyle::Dimmed;
      out_row.m_bStrikeThrough = true;
    }

    if (IsBound(pObject))
      out_row.m_sTooltip = plTranslate("GameUi.Hierarchy.BoundTooltip");
  }

  void GetColumns(plDynamicArray<plEditorUiTreeColumn>& out_columns) const override
  {
    out_columns.Clear();

    // the marks show only while set; the blank icon keeps them invisible otherwise
    const plString sBlank = plGameUiEditor::IconPath("Blank");
    const plColorGammaUB markColor = plEditorUi::Dim();

    auto& bound = out_columns.ExpandAndGetRef();
    bound.m_sIconOn = plEditorUi::MonochromeIconPath(plGameUiEditor::IconPath("Link"));
    bound.m_sIconOff = sBlank;
    bound.m_sTooltipOn = plTranslate("GameUi.Hierarchy.BoundTooltip");
    bound.m_OnColor = markColor;

    auto& localized = out_columns.ExpandAndGetRef();
    localized.m_sIconOn = plEditorUi::MonochromeIconPath(plGameUiEditor::IconPath("Globe"));
    localized.m_sIconOff = sBlank;
    localized.m_sTooltipOn = plTranslate("GameUi.Hierarchy.LocalizedTooltip");
    localized.m_OnColor = markColor;

    auto& styled = out_columns.ExpandAndGetRef();
    styled.m_sIconOn = plEditorUi::MonochromeIconPath(plGameUiEditor::IconPath("Palette"));
    styled.m_sIconOff = sBlank;
    styled.m_sTooltipOn = plTranslate("GameUi.Hierarchy.StyledTooltip");
    styled.m_OnColor = markColor;

    auto& visible = out_columns.ExpandAndGetRef();
    visible.m_sIconOn = plEditorUi::IconPath(plEditorUi::Icon::Visible);
    visible.m_sIconOff = plEditorUi::IconPath(plEditorUi::Icon::Hidden);
    visible.m_sTooltipOn = plTranslate("GameUi.Hierarchy.VisibleTooltip");
    visible.m_sTooltipOff = plTranslate("GameUi.Hierarchy.VisibleTooltip");

    auto& locked = out_columns.ExpandAndGetRef();
    locked.m_sIconOn = plEditorUi::IconPath(plEditorUi::Icon::Locked);
    locked.m_sIconOff = plEditorUi::IconPath(plEditorUi::Icon::Unlocked);
    locked.m_sTooltipOn = plTranslate("GameUi.Hierarchy.LockTooltip");
    locked.m_sTooltipOff = plTranslate("GameUi.Hierarchy.LockTooltip");
  }

  bool GetToggle(plUInt64 uiItem, plUInt32 uiColumn) const override
  {
    const plDocumentObject* pObject = m_pOwner->GetObjectOfItem(uiItem);
    if (pObject == nullptr)
      return false;

    switch (uiColumn)
    {
      case Column::Bound:
        return IsBound(pObject);
      case Column::Localized:
        return GetBool(pObject, "Localized", false);
      case Column::Styled:
        return HasString(pObject, "Style");
      case Column::Visible:
        return GetBool(pObject, "Visible", true);
      case Column::Locked:
        return GetBool(pObject, "EditorLocked", false);
      default:
        return false;
    }
  }

  void SetToggle(plUInt64 uiItem, plUInt32 uiColumn, bool bOn) override
  {
    // the marks only show state; Visible and Lock change the element
    if (uiColumn != Column::Visible && uiColumn != Column::Locked)
      return;

    const plUuid* pGuid = m_pOwner->m_ItemGuids.GetValue(uiItem);
    if (pGuid != nullptr && m_pOwner->m_OnToggle.IsValid())
      m_pOwner->m_OnToggle(*pGuid, uiColumn == Column::Locked);
  }

  void Delete(plArrayPtr<const plUInt64> items) override
  {
    if (m_pOwner->m_OnDelete.IsValid())
      m_pOwner->m_OnDelete();
  }

  bool CanDrag(plArrayPtr<const plUInt64> items) const override { return !items.IsEmpty(); }

  plStatus CanDrop(plArrayPtr<const plUInt64> items, plUInt64 uiTarget, plEditorUiTreeDropPosition position) const override
  {
    const plUInt64 uiNewParent = GetNewParent(uiTarget, position);

    // an element never moves into itself or one of its descendants
    for (plUInt64 uiParent = uiNewParent; uiParent != 0 && uiParent != InvalidParent; uiParent = GetParent(uiParent))
    {
      for (plUInt64 uiItem : items)
      {
        if (uiItem == uiParent)
          return plStatus(plTranslate("GameUi.Hierarchy.DropIntoChild"));
      }
    }

    return plStatus(PL_SUCCESS);
  }

  void Drop(plArrayPtr<const plUInt64> items, plUInt64 uiTarget, plEditorUiTreeDropPosition position) override
  {
    const plUInt64 uiNewParent = GetNewParent(uiTarget, position);

    plInt32 iIndex = -1;
    if (position != plEditorUiTreeDropPosition::Into)
    {
      plDynamicArray<plUInt64> siblings;
      GetChildren(uiNewParent, siblings);

      const plInt32 iTarget = static_cast<plInt32>(siblings.IndexOf(uiTarget));
      if (iTarget >= 0)
        iIndex = iTarget + (position == plEditorUiTreeDropPosition::Below ? 1 : 0);
    }

    m_pOwner->Move(items, uiNewParent, iIndex);
  }

  plUInt64 GetNewParent(plUInt64 uiTarget, plEditorUiTreeDropPosition position) const
  {
    if (position == plEditorUiTreeDropPosition::Into || uiTarget == 0)
      return uiTarget;

    const plUInt64 uiParent = GetParent(uiTarget);
    return uiParent == InvalidParent ? 0 : uiParent;
  }

private:
  plUiHierarchyView* m_pOwner = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// plUiHierarchyView

plUiHierarchyView::plUiHierarchyView(plDocument* pDocument)
  : plEditorUiContent("GameUi.Hierarchy")
  , m_pDocument(pDocument)
{
  m_pAdapter = PL_DEFAULT_NEW(Adapter, this);

  if (!IsReady())
    return;

  m_pTree = PL_DEFAULT_NEW(plEditorUiTree, *GetSurface(), s_szTreeName, *m_pAdapter);
  m_pTree->m_Events.AddEventHandler(plMakeDelegate(&plUiHierarchyView::OnTreeEvent, this));
  m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plUiHierarchyView::OnPointer, this));
}

plUiHierarchyView::~plUiHierarchyView()
{
  if (!m_pTree)
    return;

  m_pTree->m_Events.RemoveEventHandler(plMakeDelegate(&plUiHierarchyView::OnTreeEvent, this));
  GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
}

const plDocumentObject* plUiHierarchyView::GetObjectOfItem(plUInt64 uiItem) const
{
  const plUuid* pGuid = m_ItemGuids.GetValue(uiItem);
  return pGuid != nullptr ? m_pDocument->GetObjectManager()->GetObject(*pGuid) : nullptr;
}

void plUiHierarchyView::CollectItems()
{
  m_ItemGuids.Clear();
  m_ItemParents.Clear();

  auto collect = [&](auto& ref_self, const plDocumentObject* pParent, plUInt64 uiParent) -> void
  {
    plHybridArray<const plDocumentObject*, 16> ordered;
    GetElementChildren(pParent, ordered);

    for (const plDocumentObject* pChild : ordered)
    {
      const plUInt64 uiItem = IdOf(pChild->GetGuid());
      m_ItemGuids[uiItem] = pChild->GetGuid();
      m_ItemParents[uiItem] = uiParent;
      ref_self(ref_self, pChild, uiItem);
    }
  };

  collect(collect, GetPropertiesObject(m_pDocument), 0);
}

void plUiHierarchyView::Refresh()
{
  if (!m_pTree)
    return;

  // elements seen for the first time open, as the hierarchy shows everything expanded
  plHashTable<plUInt64, plUuid> previous = m_ItemGuids;
  CollectItems();

  GetSurface()->GetBlackboard().SetEntryValue("Hierarchy.empty", m_ItemGuids.IsEmpty());

  if (previous.IsEmpty())
  {
    m_pTree->ExpandAll();
    return;
  }

  for (auto it : m_ItemGuids)
  {
    if (!previous.Contains(it.Key()))
      m_pTree->SetExpanded(it.Key(), true);
  }

  m_pTree->Refresh();
}

void plUiHierarchyView::SetSelection(plArrayPtr<const plUuid> selection)
{
  if (!m_pTree)
    return;

  plHybridArray<plUInt64, 16> items;
  for (const plUuid& guid : selection)
  {
    const plUInt64 uiItem = IdOf(guid);
    if (m_ItemGuids.Contains(uiItem))
      items.PushBack(uiItem);
  }

  m_bSyncing = true;
  m_pTree->SetSelection(items);
  m_bSyncing = false;
}

void plUiHierarchyView::GetSelectedGuids(plDynamicArray<plUuid>& out_guids) const
{
  out_guids.Clear();

  plDynamicArray<plUInt64> items;
  m_pTree->GetSelection(items);

  for (plUInt64 uiItem : items)
  {
    if (const plUuid* pGuid = m_ItemGuids.GetValue(uiItem))
      out_guids.PushBack(*pGuid);
  }
}

void plUiHierarchyView::Move(plArrayPtr<const plUInt64> items, plUInt64 uiNewParent, plInt32 iIndex)
{
  if (!m_OnMove.IsValid())
    return;

  plHybridArray<plUuid, 8> guids;
  for (plUInt64 uiItem : items)
  {
    if (const plUuid* pGuid = m_ItemGuids.GetValue(uiItem))
      guids.PushBack(*pGuid);
  }

  plUuid parentGuid;
  if (const plUuid* pParent = m_ItemGuids.GetValue(uiNewParent))
    parentGuid = *pParent;

  m_OnMove(guids, parentGuid, iIndex);
}

void plUiHierarchyView::OnUiUpdate()
{
  if (!m_pTree || GetSurface()->GetLogicalSize() == m_vBuiltSize)
    return;

  BuildDocument();
}

void plUiHierarchyView::BuildDocument()
{
  using namespace plEditorUi;

  m_vBuiltSize = GetSurface()->GetLogicalSize();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  // the header: the element column, and the Visible and Lock icons over their columns
  {
    auto* pHeader = Panel(pRoot);
    pHeader->m_vAnchorMax.y = 0;
    pHeader->m_vOffsets = plVec4(0, 0, 0, s_fHeaderHeight);
    pHeader->m_Color = Surface();

    auto* pLine = Panel(pHeader);
    pLine->m_vAnchorMin.y = 1;
    pLine->m_vOffsets = plVec4(0, -1, 0, 1);
    pLine->m_Color = Line();

    plStringBuilder sTitle(plTranslate("GameUi.Hierarchy.ColElement"));
    sTitle.ToUpper();

    auto* pTitle = Text(pHeader, sTitle);
    pTitle->m_vOffsets = plVec4(10, 0, -60, 0);
    pTitle->m_uiFontSize = MinFontSize;
    pTitle->m_Color = Dim();

    const Icon icons[] = {Icon::Visible, Icon::Locked};
    for (plUInt32 i = 0; i < 2; ++i)
    {
      auto* pIcon = SvgImage(pHeader, IconPath(icons[i]));
      pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(1, 0.5f);
      pIcon->m_vOffsets = plVec4(-plEditorUiTree::ColumnWidth * (2 - i) + (plEditorUiTree::ColumnWidth - 13.0f) * 0.5f, -6.5f, 13, 13);
      pIcon->m_Color = Dim();
    }
  }

  plUiPanel* pList = Panel(pRoot);
  Fill(pList, plVec4(0, s_fHeaderHeight, 0, 0));
  pList->m_Color = Surface();
  m_pTree->Build(pList);

  // a document without elements says how to add one
  plUiPanel* pEmpty = plGameUiEditor::EmptyState(pRoot, plGameUiEditor::IconPath("TypePanel"), plTranslate("GameUi.Hierarchy.Empty"), plTranslate("GameUi.Hierarchy.EmptyLine"));
  Fill(pEmpty, plVec4(0, s_fHeaderHeight, 0, 0));
  pEmpty->m_sVisibleBinding = "Hierarchy.empty";

  SetDocument(Finish(pRoot));
  GetSurface()->GetBlackboard().SetEntryValue("Hierarchy.empty", m_ItemGuids.IsEmpty());
}

void plUiHierarchyView::OnTreeEvent(const plEditorUiTreeEvent& event)
{
  switch (event.m_Type)
  {
    case plEditorUiTreeEvent::Type::SelectionChanged:
    {
      if (m_bSyncing || !m_OnSelectionChanged.IsValid())
        return;

      plHybridArray<plUuid, 16> guids;
      GetSelectedGuids(guids);
      m_OnSelectionChanged(guids);
      return;
    }

    case plEditorUiTreeEvent::Type::ContextMenu:
      ShowContextMenu(event.m_vPosition);
      return;

    case plEditorUiTreeEvent::Type::DropRejected:
      if (m_OnMessage.IsValid())
        m_OnMessage(event.m_sMessage);
      return;

    default:
      return;
  }
}

void plUiHierarchyView::OnPointer(const plEditorUiPointerEvent& event)
{
  const bool bOnTree = event.m_sElement.StartsWith(s_szTreeName);

  // the empty space below the rows: the menu, and drops that move elements to the end of the document root
  if (event.m_Type == plEditorUiPointerEvent::Type::Press && event.m_Button == plEditorUiPointerEvent::Button::Right && !bOnTree)
  {
    ShowContextMenu(event.m_vPosition);
    return;
  }

  if (event.m_Type == plEditorUiPointerEvent::Type::Drop && !bOnTree && event.m_sDragElement.StartsWith(s_szTreeName) && event.m_vPosition.y > s_fHeaderHeight)
  {
    plDynamicArray<plUInt64> items;
    m_pTree->GetSelection(items);
    if (!items.IsEmpty())
      Move(items, 0, -1);
  }
}

void plUiHierarchyView::ShowContextMenu(const plVec2& vPosition)
{
  plEditorUiMenu menu;
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(s_AddEntries); ++i)
    menu.AddAction(plTranslate(s_AddEntries[i].m_szLabelKey), i + 1);

  menu.AddSeparator();

  plDynamicArray<plUInt64> selection;
  m_pTree->GetSelection(selection);

  auto& remove = menu.AddAction(plTranslate("GameUi.Menu.Delete"), s_uiDeleteId, "Del");
  remove.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Remove);
  remove.m_bDanger = true;
  remove.m_bEnabled = !selection.IsEmpty();

  plEditorUiMenuView::Show(*GetSurface(), vPosition, menu, [this](plUInt32 uiId)
    {
      if (uiId == s_uiDeleteId)
      {
        if (m_OnDelete.IsValid())
          m_OnDelete();
        return;
      }

      if (uiId >= 1 && uiId <= PL_ARRAY_SIZE(s_AddEntries) && m_OnAdd.IsValid())
        m_OnAdd(s_AddEntries[uiId - 1].m_GetType()); });
}
