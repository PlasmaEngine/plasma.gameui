#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiPaletteView.h>
#include <EditorUi/DragDrop.h>
#include <EditorUi/SearchField.h>
#include <EditorUi/Surface.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Elements/UiElements.h>

namespace
{
  constexpr float s_fGroupHeaderHeight = 24.0f;
  constexpr float s_fEntryHeight = 52.0f;
  constexpr float s_fEntryGap = 4.0f;

  plString EntryName(plUInt32 uiGroup, plUInt32 uiType)
  {
    plStringBuilder sName;
    sName.SetFormat("Palette.type.{}.{}", uiGroup, uiType);
    return sName;
  }

  plString GroupName(plUInt32 uiGroup)
  {
    plStringBuilder sName;
    sName.SetFormat("Palette.group.{}", uiGroup);
    return sName;
  }
} // namespace

plUiPaletteView::plUiPaletteView()
  : plEditorUiContent("GameUi.Palette")
{
  auto addGroup = [this](const char* szTitleKey, std::initializer_list<const plRTTI*> types)
  {
    Group& group = m_Groups.ExpandAndGetRef();
    group.m_szTitleKey = szTitleKey;
    for (const plRTTI* pType : types)
      group.m_Types.PushBack(pType);
  };

  addGroup("GameUi.Palette.GroupContainers", {plGetStaticRTTI<plUiPanel>(), plGetStaticRTTI<plUiStackPanel>(), plGetStaticRTTI<plUiGridPanel>(), plGetStaticRTTI<plUiScrollView>()});
  addGroup("GameUi.Palette.GroupVisual", {plGetStaticRTTI<plUiImage>(), plGetStaticRTTI<plUiText>(), plGetStaticRTTI<plUiPath>()});
  addGroup("GameUi.Palette.GroupControls", {plGetStaticRTTI<plUiButton>(), plGetStaticRTTI<plUiToggle>(), plGetStaticRTTI<plUiSlider>(), plGetStaticRTTI<plUiTextInput>(), plGetStaticRTTI<plUiProgressBar>(), plGetStaticRTTI<plUiItemList>()});
  addGroup("GameUi.Palette.GroupAdvanced", {plGetStaticRTTI<plUiSubDocument>(), plGetStaticRTTI<plUiSpacer>()});

  if (!IsReady())
    return;

  m_pSearch = PL_DEFAULT_NEW(plEditorUiSearchField, *GetSurface(), "Palette.Search");
  m_pSearch->m_Events.AddEventHandler(plMakeDelegate(&plUiPaletteView::OnSearch, this));
  m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plUiPaletteView::OnPointer, this));
}

plUiPaletteView::~plUiPaletteView()
{
  if (!IsReady())
    return;

  m_pSearch->m_Events.RemoveEventHandler(plMakeDelegate(&plUiPaletteView::OnSearch, this));
  GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
}

bool plUiPaletteView::Matches(const plRTTI* pType) const
{
  if (m_sFilter.IsEmpty())
    return true;

  return plStringView(plTranslate(pType->GetTypeName())).FindSubString_NoCase(m_sFilter) != nullptr;
}

void plUiPaletteView::OnSearch(const plEditorUiSearchFieldEvent& event)
{
  if (event.m_Type != plEditorUiSearchFieldEvent::Type::TextChanged)
    return;

  plStringBuilder sFilter = m_pSearch->GetText();
  sFilter.Trim(" \t");
  if (sFilter == m_sFilter)
    return;

  // the entries re-flow, so the document is rebuilt; the field keeps the keyboard
  m_sFilter = sFilter;
  m_bRebuild = true;
  m_bRefocusSearch = true;
}

void plUiPaletteView::OnUiUpdate()
{
  if (!IsReady())
    return;

  if (!m_bRebuild && GetSurface()->GetLogicalSize() == m_vBuiltSize)
    return;

  BuildDocument();

  if (m_bRefocusSearch)
  {
    m_bRefocusSearch = false;
    GetSurface()->Update();
    m_pSearch->Focus();
  }
}

void plUiPaletteView::BuildDocument()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();
  auto& board = GetSurface()->GetBlackboard();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  // the search row
  {
    plUiPanel* pRow = plGameUiEditor::ToolRow(pRoot, 0.0f);
    plUiPanel* pField = Panel(pRow);
    Fill(pField, plVec4(6, 3, 6, 3));
    pField->m_Color = plColorGammaUB(0, 0, 0, 0);
    pField->m_bClipChildren = false;
    m_pSearch->Build(pField, plTranslate("GameUi.Palette.Search"));
  }

  plUiScrollView* pScroll = PL_DEFAULT_NEW(plUiScrollView);
  Fill(pScroll, plVec4(0, plGameUiEditor::ToolRowHeight, 0, 0));
  pScroll->m_sName = "Palette.Scroll";
  pScroll->m_fSpacing = 0.0f;
  pScroll->m_vPadding = plVec4(0, 4, 0, 4);
  pScroll->m_Color = Surface();
  pRoot->m_Children.PushBack(pScroll);

  bool bAnyMatch = false;
  for (plUInt32 g = 0; g < m_Groups.GetCount(); ++g)
  {
    const Group& group = m_Groups[g];

    plHybridArray<plUInt32, 8> matches;
    for (plUInt32 t = 0; t < group.m_Types.GetCount(); ++t)
    {
      if (Matches(group.m_Types[t]))
        matches.PushBack(t);
    }

    if (matches.IsEmpty())
      continue;

    bAnyMatch = true;

    // the group header: a disclosure chevron, the title and a hairline to the right edge
    {
      auto* pHeader = Button(pScroll, plStringView(), GroupName(g));
      pHeader->m_vOffsets.w = s_fGroupHeaderHeight;
      pHeader->m_Color = plColorGammaUB(0, 0, 0, 0);
      pHeader->m_HoverColor = plColorGammaUB(0, 0, 0, 0);
      pHeader->m_PressedColor = plColorGammaUB(0, 0, 0, 0);
      pHeader->m_hStyle = GetGhostStyle();
      pHeader->ClearChildren();

      auto* pChevron = SvgImage(pHeader, IconPath(group.m_bCollapsed ? Icon::Next : Icon::Expand));
      pChevron->m_vAnchorMin = pChevron->m_vAnchorMax = plVec2(0, 0.5f);
      pChevron->m_vOffsets = plVec4(12, -5.5f, 11, 11);
      pChevron->m_Color = Dim();

      const plStringView sTitle = plTranslate(group.m_szTitleKey);
      const float fTitle = plMath::Ceil(MeasureText(sTitle, MinFontSize)) + 2.0f;
      auto* pTitle = Text(pHeader, sTitle);
      pTitle->m_vAnchorMax.x = 0;
      pTitle->m_vOffsets = plVec4(29, 0, fTitle, 0);
      pTitle->m_uiFontSize = MinFontSize;
      pTitle->m_Color = Dim();

      auto* pRule = Panel(pHeader);
      pRule->m_vAnchorMin = plVec2(0, 0.5f);
      pRule->m_vAnchorMax = plVec2(1, 0.5f);
      pRule->m_vOffsets = plVec4(29 + fTitle + 6, 0, -8, 1);
      pRule->m_Color = Line();
    }

    if (group.m_bCollapsed)
      continue;

    // two entries per row: the type's icon in the UI colour over its name
    for (plUInt32 m = 0; m < matches.GetCount(); m += 2)
    {
      auto* pRow = Panel(pScroll);
      pRow->m_vOffsets.w = s_fEntryHeight + s_fEntryGap;
      pRow->m_Color = plColorGammaUB(0, 0, 0, 0);
      pRow->m_bClipChildren = false;

      for (plUInt32 c = 0; c < 2 && m + c < matches.GetCount(); ++c)
      {
        const plUInt32 t = matches[m + c];
        const plRTTI* pType = group.m_Types[t];
        const plStringView sLabel = plTranslate(pType->GetTypeName());
        const plString sName = EntryName(g, t);

        auto* pEntry = Button(pRow, plStringView(), sName);
        pEntry->m_vAnchorMin = plVec2(c == 0 ? 0.0f : 0.5f, 0);
        pEntry->m_vAnchorMax = plVec2(c == 0 ? 0.5f : 1.0f, 0);
        pEntry->m_vOffsets = plVec4(c == 0 ? 8.0f : s_fEntryGap * 0.5f, 0, c == 0 ? -s_fEntryGap * 0.5f : -8.0f, s_fEntryHeight);
        pEntry->m_Color = Plate();
        pEntry->m_HoverColor = Raised();
        pEntry->m_PressedColor = Selection();
        pEntry->m_fBorderWidth = 1.0f;
        pEntry->m_BorderColor = Line();
        pEntry->m_vCornerRadius = plVec4(5);
        pEntry->ClearChildren();

        auto* pIcon = SvgImage(pEntry, MonochromeIconPath(plGameUiEditor::TypeIconPath(pType)));
        pIcon->m_vAnchorMin = pIcon->m_vAnchorMax = plVec2(0.5f, 0);
        pIcon->m_vOffsets = plVec4(-10, 8, 20, 20);
        pIcon->m_Color = plGameUiEditor::UiColor();

        auto* pText = Text(pEntry, sLabel);
        pText->m_vAnchorMin = plVec2(0, 0);
        pText->m_vAnchorMax = plVec2(1, 0);
        pText->m_vOffsets = plVec4(4, 30, -4, 18);
        pText->m_HAlign = plUiTextHAlign::Center;
        pText->m_uiFontSize = FontSize - 1;
        pText->m_Color = Muted();

        plStringBuilder sTooltip;
        sTooltip.SetFormat("{}: {}", sLabel, plTranslate("GameUi.Palette.AddTooltip"));
        SetToolTip(board, sName, sTooltip);
      }
    }
  }

  if (!bAnyMatch)
  {
    plUiPanel* pEmpty = plGameUiEditor::EmptyState(pRoot, IconPath(Icon::Search), plTranslate("GameUi.Palette.NoMatch"));
    Fill(pEmpty, plVec4(0, plGameUiEditor::ToolRowHeight, 0, 0));
  }

  SetDocument(Finish(pRoot));
}

const plRTTI* plUiPaletteView::GetTypeOfElement(plStringView sElement) const
{
  if (!sElement.StartsWith("Palette.type."))
    return nullptr;

  for (plUInt32 g = 0; g < m_Groups.GetCount(); ++g)
  {
    for (plUInt32 t = 0; t < m_Groups[g].m_Types.GetCount(); ++t)
    {
      if (sElement == EntryName(g, t).GetView())
        return m_Groups[g].m_Types[t];
    }
  }

  return nullptr;
}

void plUiPaletteView::OnUiEvent(const plMsgUiEvent& event)
{
  const plStringView sId = event.m_sIdentifier.GetView();

  for (plUInt32 g = 0; g < m_Groups.GetCount(); ++g)
  {
    if (sId == GroupName(g).GetView())
    {
      m_Groups[g].m_bCollapsed = !m_Groups[g].m_bCollapsed;
      m_bRebuild = true;
      return;
    }
  }

  if (const plRTTI* pType = GetTypeOfElement(sId))
  {
    if (m_OnAdd.IsValid())
      m_OnAdd(pType);
  }
}

void plUiPaletteView::OnPointer(const plEditorUiPointerEvent& event)
{
  if (event.m_Type != plEditorUiPointerEvent::Type::DragStart)
    return;

  const plRTTI* pType = GetTypeOfElement(event.m_sDragElement);
  if (pType == nullptr)
    return;

  // the type travels by name, as the canvas expects it
  plEditorUiDragPayload payload;
  payload.m_sLabel = plTranslate(pType->GetTypeName());
  payload.SetText(s_szMimeType, pType->GetTypeName());
  plEditorUiDragDrop::Begin(*GetSurface(), payload);
}
