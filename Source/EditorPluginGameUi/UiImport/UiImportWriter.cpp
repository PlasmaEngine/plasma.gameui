#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiImport/UiImportWriter.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <Foundation/IO/OSFile.h>
#include <GameUiPlugin/Elements/UiAnimationClip.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <Texture/TexConv/TexConvEnums.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

plStatus plUiImportWriter::WriteDocument(const plUiImportDocument& importDoc, plDynamicArray<plDocument*>& out_generatedDocuments)
{
  if (plOSFile::ExistsFile(importDoc.m_sTargetPathAbs))
  {
    return plStatus(plFmt("Import target '{}' already exists - delete or rename it first.", importDoc.m_sTargetPathAbs));
  }

  plDocument* pDoc = plQtEditorApp::GetSingleton()->CreateDocument(importDoc.m_sTargetPathAbs, plDocumentFlags::None);
  if (pDoc == nullptr)
  {
    return plStatus(plFmt("Could not create UI Document '{}'.", importDoc.m_sTargetPathAbs));
  }

  out_generatedDocuments.PushBack(pDoc);
  m_Report.m_uiDocuments++;

  // plSimpleAssetDocument layout: the properties object is the first child of the root
  const auto& rootChildren = pDoc->GetObjectManager()->GetRootObject()->GetChildren();
  if (rootChildren.IsEmpty())
  {
    return plStatus(plFmt("Generated document '{}' has no properties object.", importDoc.m_sTargetPathAbs));
  }

  const plDocumentObject* pProperties = rootChildren[0];

  plStringBuilder sSourcePath = importDoc.m_Root.m_sName;
  if (sSourcePath.IsEmpty())
    sSourcePath = "root";

  for (const plUiImportNode* pChild : importDoc.m_Root.m_Children)
  {
    WriteElement(*pDoc, pProperties, *pChild, sSourcePath, out_generatedDocuments);
  }

  for (const plUiImportAnimation& animation : importDoc.m_Animations)
  {
    WriteAnimation(*pDoc, pProperties, animation, sSourcePath);
  }

  return plStatus(PL_SUCCESS);
}

void plUiImportWriter::WriteElement(plDocument& ref_doc, const plDocumentObject* pParent, const plUiImportNode& node, plStringView sSourcePath, plDynamicArray<plDocument*>& out_generatedDocuments)
{
  if (node.m_pType == nullptr)
    return;

  plStringBuilder sPath = sSourcePath;
  sPath.AppendPath(node.m_sName.IsEmpty() ? "?" : node.m_sName.GetData());

  plDocumentObjectManager* pManager = ref_doc.GetObjectManager();

  plDocumentObject* pObject = pManager->CreateObject(node.m_pType);
  if (pObject == nullptr)
  {
    m_Report.Error(sPath, "Could not create the element object.");
    return;
  }

  const plInt32 iIndex = pParent->GetTypeAccessor().GetCount("Children");
  pManager->AddObject(pObject, const_cast<plDocumentObject*>(pParent), "Children", iIndex);

  m_Report.m_uiElements++;

  auto& accessor = pObject->GetTypeAccessor();

  if (!node.m_sName.IsEmpty())
    accessor.SetValue("Name", node.m_sName.GetView());

  accessor.SetValue("AnchorMin", node.m_vAnchorMin);
  accessor.SetValue("AnchorMax", node.m_vAnchorMax);
  accessor.SetValue("Offsets", node.m_vOffsets);

  for (const plUiImportProperty& prop : node.m_Properties)
  {
    if (!accessor.SetValue(prop.m_sName, prop.m_Value))
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("Property '{}' could not be set on '{}'.", prop.m_sName, node.m_pType->GetTypeName());
      m_Report.Warning(sPath, sMsg);
    }
  }

  if (!node.m_sTextureFile.IsEmpty())
  {
    plStringBuilder sDocFolder = ref_doc.GetDocumentPath();
    sDocFolder.PathParentDirectory();

    const plString sGuid = GetOrCreateTextureAsset(node.m_sTextureFile, sDocFolder, out_generatedDocuments);
    if (sGuid.IsEmpty())
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("Image '{}' could not be imported as a Texture asset.", node.m_sTextureFile);
      m_Report.Warning(sPath, sMsg);
    }
    else
    {
      accessor.SetValue(node.m_sTextureProperty.GetData(), sGuid.GetView());
    }
  }

  if (!node.m_sFontFile.IsEmpty())
  {
    const plString sGuid = GetOrCreateFontAsset(node.m_sFontFile, out_generatedDocuments);
    if (sGuid.IsEmpty())
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("Font '{}' could not be imported as a UI Font asset.", node.m_sFontFile);
      m_Report.Warning(sPath, sMsg);
    }
    else
    {
      accessor.SetValue("Font", sGuid.GetView());
    }
  }

  for (const plUiImportState& state : node.m_States)
  {
    plDocumentObject* pStateObj = pManager->CreateObject(plGetStaticRTTI<plUiElementState>());
    if (pStateObj == nullptr)
      continue;

    pManager->AddObject(pStateObj, pObject, "States", pObject->GetTypeAccessor().GetCount("States"));

    auto& stateAccessor = pStateObj->GetTypeAccessor();
    stateAccessor.SetValue("Name", state.m_sName.GetView());
    stateAccessor.SetValue("Entry", state.m_sEntry.GetView());
    stateAccessor.SetValue("Condition", state.m_iCondition);
    stateAccessor.SetValue("Value", state.m_fValue);
    stateAccessor.SetValue("ValueText", state.m_sValueText.GetView());
    stateAccessor.SetValue("Visibility", state.m_iVisibility);
    stateAccessor.SetValue("PlayClip", state.m_sPlayClip.GetView());
    stateAccessor.SetValue("ExitClip", state.m_sExitClip.GetView());
    stateAccessor.SetValue("HideDelay", state.m_fHideDelay);
  }

  for (const plUiImportEventReaction& reaction : node.m_Reactions)
  {
    plDocumentObject* pReactionObject = pManager->CreateObject(plGetStaticRTTI<plUiEventReaction>());
    if (pReactionObject == nullptr)
      continue;

    pManager->AddObject(pReactionObject, pObject, "Reactions", pObject->GetTypeAccessor().GetCount("Reactions"));
    pReactionObject->GetTypeAccessor().SetValue("Event", reaction.m_sEvent.GetView());

    for (const plUiImportEventAction& action : reaction.m_Actions)
    {
      plDocumentObject* pActionObject = pManager->CreateObject(plGetStaticRTTI<plUiEventAction>());
      if (pActionObject == nullptr)
        continue;

      pManager->AddObject(pActionObject, pReactionObject, "Actions", pReactionObject->GetTypeAccessor().GetCount("Actions"));
      auto& actionAccessor = pActionObject->GetTypeAccessor();
      actionAccessor.SetValue("Type", action.m_iType);
      actionAccessor.SetValue("Entry", action.m_sEntry.GetView());
      actionAccessor.SetValue("Value", action.m_Value);
      actionAccessor.SetValue("Target", action.m_sTarget.GetView());
      actionAccessor.SetValue("Clip", action.m_sClip.GetView());
    }
  }

  for (const plUiImportNode* pChild : node.m_Children)
  {
    WriteElement(ref_doc, pObject, *pChild, sPath, out_generatedDocuments);
  }
}

void plUiImportWriter::WriteAnimation(plDocument& ref_doc, const plDocumentObject* pProperties, const plUiImportAnimation& animation, plStringView sSourcePath)
{
  plDocumentObjectManager* pManager = ref_doc.GetObjectManager();
  plDocumentObject* pClipObject = pManager->CreateObject(plGetStaticRTTI<plUiAnimationClip>());
  if (pClipObject == nullptr)
  {
    m_Report.Warning(sSourcePath, "Could not create an imported animation clip.");
    return;
  }

  pManager->AddObject(pClipObject, const_cast<plDocumentObject*>(pProperties), "Animations", pProperties->GetTypeAccessor().GetCount("Animations"));

  auto& clipAccessor = pClipObject->GetTypeAccessor();
  clipAccessor.SetValue("Name", animation.m_sName.GetView());
  clipAccessor.SetValue("Duration", animation.m_fDuration);
  clipAccessor.SetValue("PlayOnEvent", animation.m_sPlayOnEvent.GetView());
  clipAccessor.SetValue("EntryBinding", animation.m_sEntryBinding.GetView());
  clipAccessor.SetValue("Mode", animation.m_iMode);
  clipAccessor.SetValue("LoopMode", animation.m_iLoopMode);

  for (const plUiImportAnimTrack& track : animation.m_Tracks)
  {
    plDocumentObject* pTrackObject = pManager->CreateObject(plGetStaticRTTI<plUiAnimTrack>());
    if (pTrackObject == nullptr)
      continue;

    pManager->AddObject(pTrackObject, pClipObject, "Tracks", pClipObject->GetTypeAccessor().GetCount("Tracks"));

    auto& trackAccessor = pTrackObject->GetTypeAccessor();
    trackAccessor.SetValue("Element", track.m_sElement.GetView());
    trackAccessor.SetValue("Target", track.m_iTarget);

    for (const plUiImportAnimKey& key : track.m_Keys)
    {
      plDocumentObject* pKeyObject = pManager->CreateObject(plGetStaticRTTI<plUiAnimKey>());
      if (pKeyObject == nullptr)
        continue;

      pManager->AddObject(pKeyObject, pTrackObject, "Keys", pTrackObject->GetTypeAccessor().GetCount("Keys"));

      auto& keyAccessor = pKeyObject->GetTypeAccessor();
      keyAccessor.SetValue("Time", key.m_fTime);
      keyAccessor.SetValue("Value", key.m_fValue);
      keyAccessor.SetValue("Color", key.m_Color);
      keyAccessor.SetValue("Easing", key.m_iEasing);
    }
  }
}

plString plUiImportWriter::GetOrCreateTextureAsset(plStringView sImageFileAbs, plStringView sDocumentFolderAbs, plDynamicArray<plDocument*>& out_generatedDocuments)
{
  const plString sKey = sImageFileAbs;

  if (const plString* pCached = m_TextureGuids.GetValue(sKey))
    return *pCached;

  if (!plOSFile::ExistsFile(sImageFileAbs))
  {
    m_TextureGuids[sKey] = plString();
    return {};
  }

  // ensure the image lives inside the project (copy next to the document when needed)
  plStringBuilder sImageInProject = sImageFileAbs;
  {
    plStringBuilder sRelTest = sImageFileAbs;
    if (!plQtEditorApp::GetSingleton()->MakePathDataDirectoryRelative(sRelTest))
    {
      plStringBuilder sTarget = sDocumentFolderAbs;
      sTarget.AppendPath("ImportedImages");
      sTarget.AppendPath(plPathUtils::GetFileNameAndExtension(sImageFileAbs));

      plStringBuilder sTargetFolder = sTarget;
      sTargetFolder.PathParentDirectory();
      plOSFile::CreateDirectoryStructure(sTargetFolder).IgnoreResult();

      if (plOSFile::CopyFile(sImageFileAbs, sTarget).Failed())
      {
        m_TextureGuids[sKey] = plString();
        return {};
      }

      sImageInProject = sTarget;
    }
  }

  plStringBuilder sTextureDoc = sImageInProject;
  sTextureDoc.ChangeFileExtension("plTextureAsset");

  // reuse an already-imported texture asset for this image
  if (plOSFile::ExistsFile(sTextureDoc))
  {
    plAssetCurator::plLockedSubAsset existing = plAssetCurator::GetSingleton()->FindSubAsset(sTextureDoc);
    if (existing.isValid())
    {
      plStringBuilder sGuid;
      plConversionUtils::ToString(existing->m_Data.m_Guid, sGuid);
      m_TextureGuids[sKey] = sGuid;
      return m_TextureGuids[sKey];
    }
  }

  plDocument* pDoc = plQtEditorApp::GetSingleton()->CreateDocument(sTextureDoc, plDocumentFlags::None);
  if (pDoc == nullptr)
  {
    m_TextureGuids[sKey] = plString();
    return {};
  }

  out_generatedDocuments.PushBack(pDoc);
  m_Report.m_uiImages++;

  const auto& rootChildren = pDoc->GetObjectManager()->GetRootObject()->GetChildren();
  if (!rootChildren.IsEmpty())
  {
    plStringBuilder sInputRel = sImageInProject;
    plQtEditorApp::GetSingleton()->MakePathDataDirectoryRelative(sInputRel);

    auto& accessor = rootChildren[0]->GetTypeAccessor();
    accessor.SetValue("Input1", sInputRel.GetView());
    accessor.SetValue("Usage", (int)plTexConvUsage::Color);
  }

  plStringBuilder sGuid;
  plConversionUtils::ToString(pDoc->GetGuid(), sGuid);
  m_TextureGuids[sKey] = sGuid;
  return m_TextureGuids[sKey];
}

plString plUiImportWriter::GetOrCreateFontAsset(plStringView sFontFileAbs, plDynamicArray<plDocument*>& out_generatedDocuments)
{
  const plString sKey = sFontFileAbs;

  if (const plString* pCached = m_FontGuids.GetValue(sKey))
    return *pCached;

  if (!plOSFile::ExistsFile(sFontFileAbs))
  {
    m_FontGuids[sKey] = plString();
    return {};
  }

  plStringBuilder sFontDoc = sFontFileAbs;
  sFontDoc.ChangeFileExtension("plUiFontAsset");

  if (plOSFile::ExistsFile(sFontDoc))
  {
    plAssetCurator::plLockedSubAsset existing = plAssetCurator::GetSingleton()->FindSubAsset(sFontDoc);
    if (existing.isValid())
    {
      plStringBuilder sGuid;
      plConversionUtils::ToString(existing->m_Data.m_Guid, sGuid);
      m_FontGuids[sKey] = sGuid;
      return m_FontGuids[sKey];
    }
  }

  plDocument* pDoc = plQtEditorApp::GetSingleton()->CreateDocument(sFontDoc, plDocumentFlags::None);
  if (pDoc == nullptr)
  {
    m_FontGuids[sKey] = plString();
    return {};
  }

  out_generatedDocuments.PushBack(pDoc);

  const auto& rootChildren = pDoc->GetObjectManager()->GetRootObject()->GetChildren();
  if (!rootChildren.IsEmpty())
  {
    plStringBuilder sInputRel = sFontFileAbs;
    plQtEditorApp::GetSingleton()->MakePathDataDirectoryRelative(sInputRel);

    auto& accessor = rootChildren[0]->GetTypeAccessor();
    accessor.SetValue("FontFile", sInputRel.GetView());
  }

  plStringBuilder sGuid;
  plConversionUtils::ToString(pDoc->GetGuid(), sGuid);
  m_FontGuids[sKey] = sGuid;
  return m_FontGuids[sKey];
}

// ═══════════════════════════════════════════════════════════════════════════════════════

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

void plUiImportShowReport(const plUiImportReport& report, plStringView sTitle)
{
  // always mirror to the log
  {
    plStringBuilder sTmp = sTitle;
    PL_LOG_BLOCK("UI Import", sTmp);

    plLog::Info("{} document(s), {} element(s), {} image(s) imported.", report.m_uiDocuments, report.m_uiElements, report.m_uiImages);

    for (const plUiImportReportEntry& e : report.m_Entries)
    {
      switch (e.m_Severity)
      {
        case plUiImportSeverity::Info:
          plLog::Info("{}: {}", e.m_sSourcePath, e.m_sMessage);
          break;
        case plUiImportSeverity::Warning:
          plLog::Warning("{}: {}", e.m_sSourcePath, e.m_sMessage);
          break;
        case plUiImportSeverity::Error:
          plLog::Error("{}: {}", e.m_sSourcePath, e.m_sMessage);
          break;
      }
    }
  }

  if (qApp == nullptr || !report.HasProblems())
    return;

  QDialog dlg(QApplication::activeWindow());
  dlg.setWindowTitle(QString::fromUtf8(plStringBuilder(sTitle).GetData()));
  dlg.resize(720, 420);

  QVBoxLayout* pLayout = new QVBoxLayout(&dlg);

  QLabel* pSummary = new QLabel(&dlg);
  plUInt32 uiWarnings = 0, uiErrors = 0;
  for (const plUiImportReportEntry& e : report.m_Entries)
  {
    if (e.m_Severity == plUiImportSeverity::Warning)
      ++uiWarnings;
    else if (e.m_Severity == plUiImportSeverity::Error)
      ++uiErrors;
  }
  pSummary->setText(QString("%1 document(s), %2 element(s), %3 image(s) imported — %4 warning(s), %5 error(s). Everything listed below was approximated or dropped.")
                      .arg(report.m_uiDocuments)
                      .arg(report.m_uiElements)
                      .arg(report.m_uiImages)
                      .arg(uiWarnings)
                      .arg(uiErrors));
  pSummary->setWordWrap(true);
  pLayout->addWidget(pSummary);

  QTreeWidget* pTree = new QTreeWidget(&dlg);
  pTree->setColumnCount(3);
  pTree->setHeaderLabels({"Severity", "Source", "Message"});
  pTree->setRootIsDecorated(false);
  pTree->setAlternatingRowColors(true);

  for (const plUiImportReportEntry& e : report.m_Entries)
  {
    const char* szSev = e.m_Severity == plUiImportSeverity::Error ? "Error" : (e.m_Severity == plUiImportSeverity::Warning ? "Warning" : "Info");

    QTreeWidgetItem* pItem = new QTreeWidgetItem(pTree);
    pItem->setText(0, szSev);
    pItem->setText(1, QString::fromUtf8(e.m_sSourcePath.GetData()));
    pItem->setText(2, QString::fromUtf8(e.m_sMessage.GetData()));

    if (e.m_Severity == plUiImportSeverity::Error)
      pItem->setForeground(0, QColor(255, 100, 100));
    else if (e.m_Severity == plUiImportSeverity::Warning)
      pItem->setForeground(0, QColor(252, 196, 25));
  }

  pTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  pTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  pTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  pLayout->addWidget(pTree, 1);

  QDialogButtonBox* pButtons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  QPushButton* pCopy = pButtons->addButton("Copy to Clipboard", QDialogButtonBox::ActionRole);

  QObject::connect(pButtons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pCopy, &QPushButton::clicked, [&report]() {
      QString sText;
      for (const plUiImportReportEntry& e : report.m_Entries)
      {
        const char* szSev = e.m_Severity == plUiImportSeverity::Error ? "Error" : (e.m_Severity == plUiImportSeverity::Warning ? "Warning" : "Info");
        sText += QString("%1\t%2\t%3\n").arg(szSev).arg(QString::fromUtf8(e.m_sSourcePath.GetData())).arg(QString::fromUtf8(e.m_sMessage.GetData()));
      }
      QApplication::clipboard()->setText(sText); });

  pLayout->addWidget(pButtons);

  dlg.exec();
}
