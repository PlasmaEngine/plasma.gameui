#pragma once

#include <GameUiPlugin/Resources/UiFontResource.h>

/// A font made in code for headless tests: printable ASCII and a few accented letters, every glyph 6 px wide at a
/// 10 px base size, without an atlas (layout only needs the metrics).
plUiFontResourceHandle plGameUiTestCreateFont(plStringView sName);
