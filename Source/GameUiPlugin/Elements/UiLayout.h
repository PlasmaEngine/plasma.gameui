#pragma once

#include <GameUiPlugin/Elements/UiElements.h>
#include <GameUiPlugin/GameUiPluginDLL.h>

/// \brief The single source of truth for child-rect placement.
///
/// Every walk over a UI element tree (rendering, hit-testing, editor gizmos) MUST place
/// children through this helper, so all of them stay pixel-identical.
///
/// Layout contract:
/// - Plain parents (or a nullptr parent, e.g. the editor's pseudo-root): the child places
///   itself via plUiElement::ComputeRect (anchors + offsets against the parent rect).
/// - Containers first inset their rect by Padding (left/top/right/bottom) to a content rect.
/// - plUiStackPanel: children are placed sequentially in child order along Direction.
///   The child's main-axis size is its Offsets.z (Horizontal) resp. Offsets.w (Vertical);
///   its anchors and Offsets.x/y are IGNORED. LayoutMargin insets the stretched cross
///   axis and contributes on the main axis. Pen advances by margin + size + Spacing.
///   Content may overflow (no clipping).
/// - plUiGridPanel: children fill uniform cells row-major. Cell width derives from the
///   column count and Spacing.x, cell height is RowHeight. Children stretch to fill
///   their cell (their own rect transform is ignored). Rows grow downward and may overflow.
/// - Children whose AUTHORED Visible flag is false are collapsed (no slot, no spacing).
///   Visibility driven by data BINDINGS deliberately does NOT affect layout (the element
///   keeps its slot, like CSS visibility:hidden) — layout stays a pure function of the
///   shared immutable tree.
class PL_GAMEUIPLUGIN_DLL plUiLayout
{
public:
  /// \brief Computes the rects of all children of pParent. out_rects is indexed like m_Children.
  static void ComputeChildRects(const plUiElement* pParent, const plRectFloat& parentRect, plDynamicArray<plRectFloat>& out_rects);

  /// \brief Rect of a single child. pParent may be nullptr (or a non-container), in which
  /// case pChild places itself against parentRect.
  static plRectFloat ComputeChildRect(const plUiElement* pParent, const plUiElement* pChild, const plRectFloat& parentRect, plUInt32 uiChildIndex);

  /// \brief Total main-axis extent (px, incl. padding + spacing) of a stack panel's
  /// content — what a plUiScrollView clamps its scroll offset against.
  static float ComputeStackContentExtent(const plUiStackPanel* pStack, const plRectFloat& parentRect);
};
