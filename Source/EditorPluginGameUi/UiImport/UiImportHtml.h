#pragma once

#include <EditorFramework/Assets/AssetDocumentGenerator.h>

/// \brief Imports an .html/.htm file as a UI Document asset.
///
/// Supported subset (everything else lands in the import report, never silently dropped):
/// - structure: div/section/main/header/footer/nav (flex -> Stack Panel, overflow -> Scroll
///   View, absolute-positioned children -> Panel with anchored children), p/h1-h6/span/label
///   -> Text, img -> Image (+ Texture asset), button -> Button, input text/checkbox/range ->
///   TextInput/Toggle/Slider, progress -> ProgressBar
/// - CSS: inline style, <style> blocks and same-folder linked .css files with single-compound
///   element/class/id selectors; background(-color/linear-gradient/url), border, border-radius,
///   box-shadow, opacity, color, font-size, text-align, white-space, text-overflow, padding,
///   gap, width/height, position:absolute with left/right/top/bottom
///
/// Import is one-shot: the result is an editable native element tree, the source file is
/// not referenced afterwards. See Documentation/GameUI/Plan_ImportExport.html.
class plUiHtmlDocumentGenerator : public plAssetDocumentGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plUiHtmlDocumentGenerator, plAssetDocumentGenerator);

public:
  plUiHtmlDocumentGenerator();
  ~plUiHtmlDocumentGenerator();

  virtual void GetImportModes(plStringView sAbsInputFile, plDynamicArray<plAssetDocumentGenerator::ImportMode>& out_modes) const override;
  virtual plStringView GetDocumentExtension() const override { return "plUiDocumentAsset"; }
  virtual plStringView GetGeneratorGroup() const override { return "UiDocuments"; }
  virtual plStatus Generate(plStringView sInputFileAbs, plStringView sMode, plDynamicArray<plDocument*>& out_generatedDocuments) override;
};
