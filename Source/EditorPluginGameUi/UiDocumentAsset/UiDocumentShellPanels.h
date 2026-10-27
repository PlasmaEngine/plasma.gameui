#pragma once

/// \brief The UI document's panels in the engine shell.
///
/// The shell shows a UI document's canvas, its properties and the panels and rows its window declares by itself
/// (plEngineAssetDocumentContentProvider); this provider puts the canvas's rulers and status row along the edges of the
/// view and arranges the panels: Palette above Hierarchy on the left of the canvas, Rect Transform above the properties
/// with Style as a tab beside them, and Bindings, States and Animations as tabs below the canvas. The UI font window's
/// preview goes below its properties.
namespace plUiDocumentShellPanels
{
  void Register();
  void Unregister();
} // namespace plUiDocumentShellPanels
