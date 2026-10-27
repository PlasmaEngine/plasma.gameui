#pragma once

#include <EditorPluginGameUi/UiImport/UiImportModel.h>

#include <Foundation/Containers/Map.h>
#include <Foundation/Types/Status.h>

class plDocument;
class plDocumentObject;

/// \brief Turns import-IR documents into real .plUiDocumentAsset documents.
///
/// - creates the asset document (fails if the target file already exists),
/// - builds the element tree directly through the document object manager
///   (no command history — the document is freshly generated),
/// - creates (or reuses) Texture assets for referenced image files; the texture
///   cache spans the writer's lifetime, so one import session shares textures.
///
/// The caller owns saving/closing the returned documents (the asset-import
/// framework does this for generator-driven imports).
class plUiImportWriter
{
public:
  explicit plUiImportWriter(plUiImportReport& ref_report)
    : m_Report(ref_report)
  {
  }

  plStatus WriteDocument(const plUiImportDocument& importDoc, plDynamicArray<plDocument*>& out_generatedDocuments);

private:
  void WriteElement(plDocument& ref_doc, const plDocumentObject* pParent, const plUiImportNode& node, plStringView sSourcePath, plDynamicArray<plDocument*>& out_generatedDocuments);
  void WriteAnimation(plDocument& ref_doc, const plDocumentObject* pProperties, const plUiImportAnimation& animation, plStringView sSourcePath);

  /// Returns the texture asset's guid as a string, or empty on failure.
  plString GetOrCreateTextureAsset(plStringView sImageFileAbs, plStringView sDocumentFolderAbs, plDynamicArray<plDocument*>& out_generatedDocuments);

  /// Returns the UI Font asset's guid as a string, or empty on failure.
  plString GetOrCreateFontAsset(plStringView sFontFileAbs, plDynamicArray<plDocument*>& out_generatedDocuments);

  plUiImportReport& m_Report;
  plMap<plString, plString> m_TextureGuids; ///< source image path -> texture asset guid
  plMap<plString, plString> m_FontGuids;    ///< source font path -> UI Font asset guid
};

/// \brief Shows the import report dialog (modal, drawn by the engine) when the report contains
/// warnings or errors, and mirrors all entries to plLog. Without the editor UI renderer it only
/// logs.
void plUiImportShowReport(const plUiImportReport& report, plStringView sTitle);
