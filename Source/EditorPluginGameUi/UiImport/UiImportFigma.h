#pragma once

#include <EditorFramework/Assets/AssetDocumentGenerator.h>
#include <GuiFoundation/Action/BaseActions.h>

/// \brief Imports an offline Figma file-JSON dump (*.fig.json — the response of Figma's
/// GET /v1/files/:key REST endpoint saved to disk) as UI Document assets, one per
/// top-level frame.
///
/// Mapping (details in Documentation/GameUI/Plan_ImportExport.html):
/// frames/groups -> Panel (auto-layout -> Stack Panel), constraints -> anchors, TEXT ->
/// Text, solid/gradient fills, strokes -> borders, corner radii, drop shadows, name
/// conventions Btn_/Tgl_/Sld_/Bar_/Inp_ promote nodes to widgets. Image fills and vector
/// shapes need the REST importer (Project > Import UI from Figma...) — offline they
/// import as placeholders with a report entry.
class plUiFigmaDocumentGenerator : public plAssetDocumentGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plUiFigmaDocumentGenerator, plAssetDocumentGenerator);

public:
  plUiFigmaDocumentGenerator();
  ~plUiFigmaDocumentGenerator();

  virtual void GetImportModes(plStringView sAbsInputFile, plDynamicArray<plAssetDocumentGenerator::ImportMode>& out_modes) const override;
  virtual plStringView GetDocumentExtension() const override { return "plUiDocumentAsset"; }
  virtual plStringView GetGeneratorGroup() const override { return "UiDocuments"; }
  virtual plStatus Generate(plStringView sInputFileAbs, plStringView sMode, plDynamicArray<plDocument*>& out_generatedDocuments) override;
};

/// \brief Opens the modal "Import UI from Figma" dialog (drawn by the engine): file URL +
/// personal access token (stored per-user), frame
/// picker, image-fill/vector download via the Figma REST API, then imports the selected
/// frames as UI Document assets into the chosen folder.
void plUiOpenFigmaImportDialog();

/// \brief Project-menu action that opens the Figma import dialog.
class plUiFigmaImportAction : public plButtonAction
{
  PL_ADD_DYNAMIC_REFLECTION(plUiFigmaImportAction, plButtonAction);

public:
  plUiFigmaImportAction(const plActionContext& context, const char* szName, plInt32 iUnused);
  virtual void Execute(const plVariant& value) override;
};
