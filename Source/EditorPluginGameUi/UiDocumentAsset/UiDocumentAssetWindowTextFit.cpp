#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetWindow.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontAsset.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Elements/UiLayout.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  /// The native element at a path below the document's top-level elements, or nullptr.
  const plUiElement* FindElement(const plUiDocumentAssetProperties* pProps, plArrayPtr<const plUInt32> path)
  {
    const plUiElement* pElement = nullptr;
    const auto* pChildren = &pProps->m_Children;

    for (plUInt32 uiIndex : path)
    {
      if (uiIndex >= pChildren->GetCount() || (*pChildren)[uiIndex] == nullptr)
        return nullptr;

      pElement = (*pChildren)[uiIndex];
      pChildren = &pElement->m_Children;
    }

    return pElement;
  }

  /// The control a text belongs to: a text inside a button is the button's label.
  plUInt32 GetControlPathLength(const plUiDocumentAssetProperties* pProps, plArrayPtr<const plUInt32> path)
  {
    if (path.GetCount() >= 2)
    {
      const plUiElement* pParent = FindElement(pProps, path.GetSubArray(0, path.GetCount() - 1));
      if (plDynamicCast<const plUiButton*>(pParent) != nullptr)
        return path.GetCount() - 1;
    }

    return path.GetCount();
  }

  bool IsPlainButton(const plRTTI* pType)
  {
    // the buttons Fit all buttons sizes: buttons and toggles, not the colour wheel or the direction dial
    return pType == plGetStaticRTTI<plUiButton>() || pType == plGetStaticRTTI<plUiToggle>();
  }

  void CollectButtons(const plDocumentObject* pObject, plDynamicArray<const plDocumentObject*>& out_buttons)
  {
    for (const plDocumentObject* pChild : pObject->GetChildren())
    {
      if (pChild->GetParentProperty() != "Children")
        continue;

      if (IsPlainButton(pChild->GetType()))
        out_buttons.PushBack(pChild);

      CollectButtons(pChild, out_buttons);
    }
  }

  plUInt32 CountFittingElements(plArrayPtr<plUiElement* const> elements)
  {
    plUInt32 uiCount = 0;
    for (const plUiElement* pElement : elements)
    {
      if (pElement == nullptr)
        continue;

      if (pElement->m_SizeMode == plUiSizeMode::FitContent)
        ++uiCount;

      uiCount += CountFittingElements(pElement->m_Children);
    }
    return uiCount;
  }

  /// The characters as the editor font can draw them, else as code points.
  plString FormatCharacters(plArrayPtr<const plUInt32> characters, plUInt32 uiMax)
  {
    plResourceLock<plUiFontResource> pFont(plEditorUi::GetDefaultFont(), plResourceAcquireMode::BlockTillLoaded_NeverFail);
    const bool bFont = pFont.GetAcquireResult() == plResourceAcquireResult::Final;

    plStringBuilder sResult;
    for (plUInt32 i = 0; i < characters.GetCount() && i < uiMax; ++i)
    {
      if (!sResult.IsEmpty())
        sResult.Append(" ");

      if (bFont && pFont->GetGlyph(characters[i]) != nullptr)
        sResult.Append(characters[i]);
      else
        sResult.AppendFormat("U+{}", plArgU(characters[i], 4, true, 16, true));
    }

    if (characters.GetCount() > uiMax)
      sResult.Append(" \xE2\x80\xA6");

    return sResult;
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// panels

void plUiDocumentAssetDocumentWindow::CreateTextFitPanels()
{
  m_pSizeFitView = AddPanel<plUiSizeFitView>({"SizeFit", plTranslate("GameUi.Panel.SizeFit")});
  m_pSizeFitView->m_OnSizeModeChanged = [this](bool bFit)
  { OnSizeModeChanged(bFit); };
  m_pSizeFitView->m_OnWidthLimitsChanged = [this](float fMin, float fMax, bool bFinal)
  { OnWidthLimitsChanged(fMin, fMax, bFinal); };
  m_pSizeFitView->m_OnOverflowChanged = [this](plInt32 iOverflow)
  { OnOverflowChanged(iOverflow); };

  m_pTextFitView = AddPanel<plUiTextFitView>({"TextFit", plTranslate("GameUi.Panel.TextFit")});
  m_pTextFitView->m_OnIssueActivated = [this](plUInt32 uiIndex)
  { OnTextFitIssueActivated(uiIndex); };
  m_pTextFitView->m_OnAddCharacters = [this](plUInt32 uiIndex)
  { OnAddCharacters(uiIndex); };
}

void plUiDocumentAssetDocumentWindow::ParseTextFit(plStringView sPayload)
{
  m_TextFitIssues.Clear();

  // records "\x1E", fields "\x1F": kind, path, needed, available, shrunk size, font, text, missing characters
  plHybridArray<plStringView, 16> records;
  sPayload.Split(false, records, "\x1E");

  for (plStringView sRecord : records)
  {
    plHybridArray<plStringView, 8> fields;
    sRecord.Split(true, fields, "\x1F");
    if (fields.GetCount() < 8)
      continue;

    TextFitIssue& issue = m_TextFitIssues.ExpandAndGetRef();

    plUInt32 uiKind = 0;
    plConversionUtils::StringToUInt(fields[0], uiKind).IgnoreResult();
    issue.m_Kind = static_cast<plUiTextFitIssue::Kind>(uiKind);

    plHybridArray<plStringView, 8> indices;
    fields[1].Split(false, indices, "/");
    for (plStringView sIndex : indices)
    {
      plUInt32 uiIndex = 0;
      if (plConversionUtils::StringToUInt(sIndex, uiIndex).Succeeded())
        issue.m_Path.PushBack(uiIndex);
    }

    double fValue = 0.0;
    plConversionUtils::StringToFloat(fields[2], fValue).IgnoreResult();
    issue.m_fNeeded = static_cast<float>(fValue);
    plConversionUtils::StringToFloat(fields[3], fValue).IgnoreResult();
    issue.m_fAvailable = static_cast<float>(fValue);
    plConversionUtils::StringToFloat(fields[4], fValue).IgnoreResult();
    issue.m_fShrunkSize = static_cast<float>(fValue);

    issue.m_sFont = fields[5];
    issue.m_sText = fields[6];

    for (auto it = fields[7].GetIteratorFront(); it.IsValid(); ++it)
      issue.m_MissingGlyphs.PushBack(it.GetCharacter());
  }
}

plString plUiDocumentAssetDocumentWindow::GetFontName(plStringView sFontResource) const
{
  if (plConversionUtils::IsStringUuid(sFontResource))
  {
    auto pSubAsset = plAssetCurator::GetSingleton()->GetSubAsset(plConversionUtils::ConvertStringToUuid(sFontResource));
    if (pSubAsset.isValid())
      return pSubAsset->GetName();
  }

  return plTranslate("GameUi.TextFit.TheFont");
}

plString plUiDocumentAssetDocumentWindow::GetIssueTitle(const TextFitIssue& issue) const
{
  plStringBuilder sFormat;
  plStringBuilder sTitle;

  if (issue.m_Kind == plUiTextFitIssue::Kind::MissingGlyphs)
  {
    sFormat = plTranslate("GameUi.TextFit.MissingGlyphs");
    sTitle.SetFormat(sFormat.GetData(), issue.m_MissingGlyphs.GetCount(), GetFontName(issue.m_sFont));
    return sTitle;
  }

  // the control's name: a button for its label, else the text itself
  const plUiDocumentAssetProperties* pProps = m_pAssetDoc->GetProperties();
  const plUInt32 uiLength = GetControlPathLength(pProps, issue.m_Path);
  const plUiElement* pElement = FindElement(pProps, plArrayPtr<const plUInt32>(issue.m_Path.GetData(), uiLength));
  plStringBuilder sName = pElement != nullptr ? pElement->m_sName.GetView() : plStringView();
  if (sName.IsEmpty() && pElement != nullptr)
    sName = plTranslate(pElement->GetDynamicRTTI()->GetTypeName());

  switch (issue.m_Kind)
  {
    case plUiTextFitIssue::Kind::Overflow:
      sFormat = plTranslate("GameUi.TextFit.Overflow");
      break;
    case plUiTextFitIssue::Kind::Clipped:
      sFormat = plTranslate("GameUi.TextFit.Clipped");
      break;
    case plUiTextFitIssue::Kind::Ellipsized:
      sFormat = plTranslate("GameUi.TextFit.Ellipsized");
      break;
    default:
      sFormat = plTranslate("GameUi.TextFit.Shrunk");
      break;
  }

  sTitle.SetFormat(sFormat.GetData(), sName, static_cast<plInt32>(issue.m_fShrunkSize));
  return sTitle;
}

plString plUiDocumentAssetDocumentWindow::GetIssueDetail(const TextFitIssue& issue) const
{
  if (issue.m_Kind == plUiTextFitIssue::Kind::MissingGlyphs)
    return FormatCharacters(issue.m_MissingGlyphs, 14);

  if (issue.m_Kind == plUiTextFitIssue::Kind::Shrunk)
    return plTranslate("GameUi.TextFit.FitsWithShrink");

  plStringBuilder sFormat = plTranslate("GameUi.TextFit.Needs");
  plStringBuilder sDetail;
  sDetail.SetFormat(sFormat.GetData(), static_cast<plInt32>(plMath::Round(issue.m_fNeeded)), static_cast<plInt32>(plMath::Round(issue.m_fAvailable)));
  return sDetail;
}

void plUiDocumentAssetDocumentWindow::UpdateTextFitPanel()
{
  if (m_pTextFitView == nullptr)
    return;

  // the selected control, to highlight its row
  plHybridArray<plUInt32, 8> selectedPath;
  if (const plDocumentObject* pSelected = GetDocument()->GetSelectionManager()->GetCurrentObject())
    ComputeElementPath(pSelected, selectedPath);

  const plUiDocumentAssetProperties* pProps = m_pAssetDoc->GetProperties();

  plHybridArray<plUiTextFitView::Issue, 16> issues;
  plUInt32 uiOverflowing = 0;

  for (const TextFitIssue& source : m_TextFitIssues)
  {
    plUiTextFitView::Issue& issue = issues.ExpandAndGetRef();
    issue.m_sTitle = GetIssueTitle(source);
    issue.m_sDetail = GetIssueDetail(source);
    issue.m_bAddCharacters = source.m_Kind == plUiTextFitIssue::Kind::MissingGlyphs;

    if (source.m_Kind == plUiTextFitIssue::Kind::MissingGlyphs)
      issue.m_Severity = plUiTextFitView::Issue::Severity::Error;
    else if (source.m_Kind == plUiTextFitIssue::Kind::Shrunk)
      issue.m_Severity = plUiTextFitView::Issue::Severity::Info;
    else
      issue.m_Severity = plUiTextFitView::Issue::Severity::Warning;

    if (issue.m_Severity == plUiTextFitView::Issue::Severity::Warning)
      ++uiOverflowing;

    const plUInt32 uiLength = GetControlPathLength(pProps, source.m_Path);
    issue.m_bSelected = source.m_Kind != plUiTextFitIssue::Kind::MissingGlyphs && !selectedPath.IsEmpty() && plArrayPtr<const plUInt32>(source.m_Path.GetData(), uiLength) == selectedPath.GetArrayPtr();
  }

  // the scope: the language or the pseudo-localization the preview shows
  plString sScope;
  if (m_bPseudoLocalization)
    sScope = plTranslate("GameUi.Toolbar.Pseudo");
  else if (!m_sLanguage.IsEmpty())
    sScope = GetLanguageLabel(m_sLanguage);
  else
    sScope = plTranslate("GameUi.TextFit.ScopeAuthored");

  m_pTextFitView->SetIssues(sScope, issues);
  m_pCanvasStatusRow->SetTextFitSummary(uiOverflowing, CountFittingElements(pProps->m_Children));
}

//////////////////////////////////////////////////////////////////////////
// the size section

const plDocumentObject* plUiDocumentAssetDocumentWindow::FindLabelObject(const plDocumentObject* pObject) const
{
  if (pObject == nullptr)
    return nullptr;

  if (pObject->GetType()->IsDerivedFrom<plUiText>())
    return pObject;

  for (const plDocumentObject* pChild : pObject->GetChildren())
  {
    if (pChild->GetParentProperty() == "Children" && pChild->GetType()->IsDerivedFrom<plUiText>())
      return pChild;
  }

  return nullptr;
}

void plUiDocumentAssetDocumentWindow::UpdateSizeFitPanel()
{
  if (m_pSizeFitView == nullptr)
    return;

  plUiSizeFitView::State state;

  HitResult selected;
  if (!GetSelectedElementRect(selected) || selected.m_pElement == nullptr)
  {
    m_pSizeFitView->SetState(state);
    return;
  }

  const plUiElement* pElement = selected.m_pElement;
  state.m_bHasElement = true;
  state.m_pType = pElement->GetDynamicRTTI();
  state.m_sTypeName = plTranslate(state.m_pType->GetTypeName());
  state.m_bFit = pElement->m_SizeMode == plUiSizeMode::FitContent;
  state.m_vMinSize = pElement->m_vMinSize;
  state.m_vMaxSize = pElement->m_vMaxSize;
  state.m_fWidth = pElement->m_vOffsets.z;

  // the label: the element itself when it is a text, else its first text child
  plUInt32 uiLabelIndex = plInvalidIndex;
  const plUiText* pLabel = plDynamicCast<const plUiText*>(pElement);
  for (plUInt32 i = 0; pLabel == nullptr && i < pElement->m_Children.GetCount(); ++i)
  {
    if ((pLabel = plDynamicCast<const plUiText*>(pElement->m_Children[i])) != nullptr)
      uiLabelIndex = i;
  }

  if (pLabel == nullptr)
  {
    m_pSizeFitView->SetState(state);
    return;
  }

  plStringBuilder sStorage;
  state.m_sText = plUiLayout::ResolveText(pLabel, &m_LayoutContext, sStorage);
  state.m_iOverflow = static_cast<plInt32>(pLabel->m_Overflow.GetValue());

  // what the label needs: the preview's figure when it reported this text, measured here otherwise
  plHybridArray<plUInt32, 8> labelPath = selected.m_Path;
  if (uiLabelIndex != plInvalidIndex)
    labelPath.PushBack(uiLabelIndex);

  for (const TextFitIssue& issue : m_TextFitIssues)
  {
    if (issue.m_Kind != plUiTextFitIssue::Kind::MissingGlyphs && issue.m_Path == labelPath)
    {
      state.m_bHasNeeds = true;
      state.m_fNeeded = issue.m_fNeeded;
      state.m_fAvailable = issue.m_fAvailable;
    }
  }

  if (!state.m_bHasNeeds)
  {
    const plRectFloat labelRect = uiLabelIndex == plInvalidIndex ? selected.m_Rect : plUiLayout::ComputeChildRect(pElement, pLabel, selected.m_Rect, uiLabelIndex, &m_LayoutContext);

    plUiLayoutContext measureContext = m_LayoutContext;
    measureContext.m_bWaitForFonts = true;

    plUiText measured;
    measured.m_sText = state.m_sText;
    measured.m_hFont = pLabel->m_hFont;
    measured.m_hStyle = pLabel->m_hStyle;
    measured.m_uiFontSize = pLabel->m_uiFontSize;
    measured.m_fLetterSpacing = pLabel->m_fLetterSpacing;
    measured.m_fLineHeight = pLabel->m_fLineHeight;

    state.m_bHasNeeds = true;
    state.m_fNeeded = plUiLayout::MeasureContent(&measured, &measureContext).x;
    state.m_fAvailable = labelRect.width;
  }

  m_pSizeFitView->SetState(state);
}

void plUiDocumentAssetDocumentWindow::ApplyFitContent(const plDocumentObject* pObject)
{
  auto pAccessor = GetDocument()->GetObjectAccessor();

  pAccessor->SetValueByName(pObject, "SizeMode", static_cast<plInt32>(plUiSizeMode::FitContent)).IgnoreResult();

  // limits that keep the authored height and let the width follow the text: down to 120 px (or the authored width when
  // it is smaller), up to 2.5 times the authored width (at least 420 px)
  const plVec2 vMin = pObject->GetTypeAccessor().GetValue("MinSize").ConvertTo<plVec2>();
  const plVec2 vMax = pObject->GetTypeAccessor().GetValue("MaxSize").ConvertTo<plVec2>();
  if (vMin.IsZero() && vMax.IsZero())
  {
    const plVec4 vOffsets = pObject->GetTypeAccessor().GetValue("Offsets").ConvertTo<plVec4>();
    const float fWidth = plMath::Max(vOffsets.z, 1.0f);

    pAccessor->SetValueByName(pObject, "MinSize", plVec2(plMath::Min(fWidth, 120.0f), plMath::Max(vOffsets.w, 0.0f))).IgnoreResult();
    pAccessor->SetValueByName(pObject, "MaxSize", plVec2(plMath::Max(420.0f, plMath::Round(fWidth * 2.5f)), 0.0f)).IgnoreResult();
  }
}

void plUiDocumentAssetDocumentWindow::OnSizeModeChanged(bool bFit)
{
  const plDocumentObject* pObject = GetDocument()->GetSelectionManager()->GetCurrentObject();
  plHybridArray<plUInt32, 8> path;
  if (pObject == nullptr || !ComputeElementPath(pObject, path) || path.IsEmpty())
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction(bFit ? "Fit Content" : "Fixed Size");

  if (bFit)
    ApplyFitContent(pObject);
  else
    pAccessor->SetValueByName(pObject, "SizeMode", static_cast<plInt32>(plUiSizeMode::Fixed)).IgnoreResult();

  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::OnWidthLimitsChanged(float fMin, float fMax, bool bFinal)
{
  const plDocumentObject* pObject = GetDocument()->GetSelectionManager()->GetCurrentObject();
  if (pObject == nullptr)
    return;

  // one transaction per drag: opened on the first move, finished when the slider is let go
  auto pAccessor = GetDocument()->GetObjectAccessor();
  if (!m_bWidthTransactionOpen)
  {
    pAccessor->StartTransaction("Edit Size Limits");
    m_bWidthTransactionOpen = true;
  }

  const plVec2 vMin = pObject->GetTypeAccessor().GetValue("MinSize").ConvertTo<plVec2>();
  const plVec2 vMax = pObject->GetTypeAccessor().GetValue("MaxSize").ConvertTo<plVec2>();
  pAccessor->SetValueByName(pObject, "MinSize", plVec2(fMin, vMin.y)).IgnoreResult();
  pAccessor->SetValueByName(pObject, "MaxSize", plVec2(fMax, vMax.y)).IgnoreResult();

  if (bFinal)
  {
    pAccessor->FinishTransaction();
    m_bWidthTransactionOpen = false;
  }
}

void plUiDocumentAssetDocumentWindow::OnOverflowChanged(plInt32 iOverflow)
{
  const plDocumentObject* pLabel = FindLabelObject(GetDocument()->GetSelectionManager()->GetCurrentObject());
  if (pLabel == nullptr)
    return;

  auto pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Text Overflow");
  pAccessor->SetValueByName(pLabel, "Overflow", iOverflow).IgnoreResult();
  pAccessor->FinishTransaction();
}

void plUiDocumentAssetDocumentWindow::FitAllButtons()
{
  plHybridArray<const plDocumentObject*, 32> buttons;
  CollectButtons(GetPropertiesObject(), buttons);

  if (buttons.IsEmpty())
  {
    SetCanvasStatusMessage(plTranslate("GameUi.Status.FitAllNone"));
    return;
  }

  auto pAccessor = GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Fit All Buttons");

  for (const plDocumentObject* pButton : buttons)
    ApplyFitContent(pButton);

  pAccessor->FinishTransaction();

  plStringBuilder sFormat = plTranslate("GameUi.Status.FitAllDone");
  plStringBuilder sMessage;
  sMessage.SetFormat(sFormat.GetData(), buttons.GetCount());
  SetCanvasStatusMessage(sMessage);
}

//////////////////////////////////////////////////////////////////////////
// the Text fit rows

void plUiDocumentAssetDocumentWindow::OnTextFitIssueActivated(plUInt32 uiIndex)
{
  if (uiIndex >= m_TextFitIssues.GetCount())
    return;

  // a button's label selects the button
  const TextFitIssue& issue = m_TextFitIssues[uiIndex];
  const plUInt32 uiLength = GetControlPathLength(m_pAssetDoc->GetProperties(), issue.m_Path);
  const plDocumentObject* pObject = GetElementDocObject(plArrayPtr<const plUInt32>(issue.m_Path.GetData(), uiLength));

  if (pObject != nullptr && !issue.m_Path.IsEmpty())
    GetDocument()->GetSelectionManager()->SetSelection(pObject);
}

void plUiDocumentAssetDocumentWindow::OnAddCharacters(plUInt32 uiIndex)
{
  if (uiIndex >= m_TextFitIssues.GetCount())
    return;

  const TextFitIssue& issue = m_TextFitIssues[uiIndex];

  // the missing characters, and every character the previewed language's tables use
  plHashSet<plUInt32> characters;
  for (plUInt32 uiCp : issue.m_MissingGlyphs)
    characters.Insert(uiCp);

  const plGameUiLocalization::Language* pLanguage = GetPreviewLanguage();
  if (pLanguage != nullptr)
    plGameUiLocalization::CollectCharacters(*pLanguage, characters);

  plUInt32 uiAdded = 0;
  const plStatus status = plUiFontAssetDocument::AddCustomCharacters(issue.m_sFont, characters, uiAdded);
  if (status.Failed())
  {
    SetCanvasStatusMessage(status.m_sMessage);
    return;
  }

  plStringBuilder sFormat = plTranslate(pLanguage != nullptr ? "GameUi.Status.CharactersAddedTable" : "GameUi.Status.CharactersAdded");
  plStringBuilder sMessage;
  sMessage.SetFormat(sFormat.GetData(), uiAdded, GetFontName(issue.m_sFont), pLanguage != nullptr ? GetLanguageLabel(pLanguage->m_sName) : plString());
  SetCanvasStatusMessage(sMessage);
}
