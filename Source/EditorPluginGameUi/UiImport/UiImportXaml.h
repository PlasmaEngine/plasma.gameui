#pragma once

#include <EditorFramework/Assets/AssetDocumentGenerator.h>

/// \brief Imports a NoesisGUI / WPF .xaml file as a UI Document asset.
///
/// Supported subset (everything else lands in the import report, never silently dropped):
/// - containers: Canvas (-> Panel + point anchors), StackPanel (-> Stack Panel), Grid with
///   row/column definitions (-> uniform Grid Panel, star/auto approximated), Grid without
///   definitions (-> Panel + alignment anchors), Border (-> Panel visuals), ScrollViewer
/// - widgets: TextBlock, Image, Button, ToggleButton/CheckBox, Slider, ProgressBar, TextBox,
///   ItemsControl/ListBox + DataTemplate (-> Item List with the template as its own document)
/// - layout: Margin + Horizontal/VerticalAlignment + Width/Height -> anchors/offsets,
///   Canvas.Left/Top, Visibility
/// - data: {Binding Path} on Text/Value/Visibility/IsChecked/ItemsSource -> binding entries
/// - resources: SolidColorBrush/LinearGradientBrush/RadialGradientBrush by key,
///   Style/Setter flattening (property setters apply to the element; template layout
///   hints are retained while template visuals/triggers report)
/// - animation: Storyboard translation/opacity/uniform-scale/size/rotation tracks become
///   GameUI Animation Clips; matching data triggers become clip-driving element states
/// - UserControl references resolve against same-folder .xaml files and import as
///   Sub-Documents (deduplicated; existing child assets are reused by GUID)
///
/// See Documentation/GameUI/Plan_ImportExport.html.
class plUiXamlDocumentGenerator : public plAssetDocumentGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plUiXamlDocumentGenerator, plAssetDocumentGenerator);

public:
  plUiXamlDocumentGenerator();
  ~plUiXamlDocumentGenerator();

  virtual void GetImportModes(plStringView sAbsInputFile, plDynamicArray<plAssetDocumentGenerator::ImportMode>& out_modes) const override;
  virtual plStringView GetDocumentExtension() const override { return "plUiDocumentAsset"; }
  virtual plStringView GetGeneratorGroup() const override { return "UiDocuments"; }
  virtual plStatus Generate(plStringView sInputFileAbs, plStringView sMode, plDynamicArray<plDocument*>& out_generatedDocuments) override;
};
