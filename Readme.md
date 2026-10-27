# Game UI (`plasma.gameui`)

The editor for Plasma Engine's game UI: the UI Document canvas editor, UI Style and UI Font assets, importers for
HTML, XAML and Figma, and the editor-side engine plugin that previews documents live.

The runtime, `plGameUiPlugin` (elements, layout, the UI Canvas component, rendering, shaders), is part of the
engine in `Code/EnginePlugins/GameUiPlugin`, because the editor's own UI is drawn with it. The manifest still lists
it under `RuntimePlugins`, which is what loads it in an exported game.

## What is in it

| Asset / tool | What it does |
| --- | --- |
| UI Document | Canvas editor: palette, hierarchy, rect transform, Size (fit to content), Text Fit, styles, bindings, states, animations; language strip with pseudo-localisation |
| UI Style | Shared looks for elements (colours, brush, gradients, text) with a live preview |
| UI Font | Bakes a font file (plus fallback fonts) into a distance-field atlas; can take every character the project's string tables use |
| Importers | UI Documents from HTML, XAML and Figma JSON |
| Transform checks | Text fit warnings per language when a UI Document is transformed |

The manual is in `Docs/` (start at `Docs/index.html`).

## Layout

```
Source/EditorPluginGameUi/     editor plugin (plEditorPluginGameUi)
  UiDocumentAsset/             UI Document asset, window, panels, Text Fit, language strip
  UiFontAsset/                 UI Font asset and its preview
  UiStyleAsset/                UI Style asset
  UiImport/                    HTML, XAML and Figma importers
Source/EnginePluginGameUi/     editor-engine plugin (plEnginePluginGameUi): live preview contexts
Source/GameUiTest/             headless test program (GameUiTest)
EditorIcons/                   icons of the editor panels and element types
Docs/                          user manual
```

## Building

The package builds against an engine checkout:

```bash
PLASMA_SDK_ROOT=<engine checkout> plasmabuild build EditorPluginGameUi
PLASMA_SDK_ROOT=<engine checkout> plasmabuild build EnginePluginGameUi
PLASMA_SDK_ROOT=<engine checkout> plasmabuild build GameUiTest
```

Build the engine's `GameUiPlugin` (or the whole `PlasmaEditor`) first; the package links it.

## Testing

`GameUiTest.exe` opens no window. Run it with the engine's binaries on `PATH`:

```bash
PATH="<engine>/Binaries/Windows_Development:$PATH" Bin/Tests/Windows_x64_Development/GameUiTest.exe -noGui -all -close -noSave
```

It checks the text layout (wrapping, letter spacing, shrink to fit, pseudo-localisation), fit to content (point
anchored and stacked), the Text fit analysis, path fill rules (even-odd holes), world-space panel placement and
pointer rays, and the UI flow.

## Versions

| Version | Engine | Notes |
| --- | --- | --- |
| 1.4.0 | 0.11.0 | Fit to content, Text Fit panel and transform warnings, language strip and pseudo-localisation, world-space and billboard canvases, fallback fonts and string-table characters, text outline and shadow, angled/radial gradients, backdrop blur, vector shape holes and strokes. The runtime moved to the engine; the test target is part of the package. |
| 1.3.1 | 0.11.0 | Previous release. |
