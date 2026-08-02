#include <GameUiPlugin/GameUiPluginPCH.h>

#include <GameUiPlugin/Elements/UiLayout.h>

namespace
{
  /// the size a stack panel consumes for a child on its main axis:
  /// spacers use their explicit Width/Height, everything else Offsets.z/w
  float GetPreferredMainSize(const plUiElement* pChild, bool bHorizontal)
  {
    if (const plUiSpacer* pSpacer = plDynamicCast<const plUiSpacer*>(pChild))
    {
      return plMath::Max(bHorizontal ? pSpacer->m_fWidth : pSpacer->m_fHeight, 0.0f);
    }

    return plMath::Max(bHorizontal ? pChild->m_vOffsets.z : pChild->m_vOffsets.w, 0.0f);
  }

  float GetPreferredCrossSize(const plUiElement* pChild, bool bHorizontal)
  {
    if (const plUiSpacer* pSpacer = plDynamicCast<const plUiSpacer*>(pChild))
      return plMath::Max(bHorizontal ? pSpacer->m_fHeight : pSpacer->m_fWidth, 0.0f);

    return plMath::Max(bHorizontal ? pChild->m_vOffsets.w : pChild->m_vOffsets.z, 0.0f);
  }

  plRectFloat GetContentRect(const plRectFloat& rect, const plVec4& vPadding)
  {
    return plRectFloat(
      rect.x + vPadding.x,
      rect.y + vPadding.y,
      plMath::Max(rect.width - vPadding.x - vPadding.z, 0.0f),
      plMath::Max(rect.height - vPadding.y - vPadding.w, 0.0f));
  }

  void ComputeStackRects(const plUiStackPanel* pStack, const plRectFloat& parentRect, plDynamicArray<plRectFloat>& out_rects)
  {
    const plRectFloat content = GetContentRect(parentRect, pStack->m_vPadding);
    const bool bHorizontal = pStack->m_Direction == plUiStackDirection::Horizontal;

    plUInt32 uiVisibleChildren = 0;
    plUInt32 uiExpandingSpacers = 0;
    float fOccupied = 0.0f;
    for (const plUiElement* pChild : pStack->m_Children)
    {
      if (pChild == nullptr || !pChild->m_bVisible)
        continue;

      const plVec4& margin = pChild->m_vLayoutMargin;
      fOccupied += GetPreferredMainSize(pChild, bHorizontal);
      fOccupied += bHorizontal ? margin.x + margin.z : margin.y + margin.w;
      if (const plUiSpacer* pSpacer = plDynamicCast<const plUiSpacer*>(pChild))
      {
        if (pSpacer->m_bExpand)
          ++uiExpandingSpacers;
      }
      ++uiVisibleChildren;
    }

    float fSpacing = pStack->m_fSpacing;
    if (uiVisibleChildren > 1)
      fOccupied += (uiVisibleChildren - 1) * fSpacing;

    const float fAvailable = bHorizontal ? content.width : content.height;
    const float fExtra = plMath::Max(fAvailable - fOccupied, 0.0f);
    const float fExpandShare = uiExpandingSpacers > 0 ? fExtra / uiExpandingSpacers : 0.0f;
    float fStartOffset = 0.0f;
    if (uiExpandingSpacers == 0 && pStack->m_Justification == plUiStackJustification::Center)
      fStartOffset = fExtra * 0.5f;
    else if (uiExpandingSpacers == 0 && pStack->m_Justification == plUiStackJustification::End)
      fStartOffset = fExtra;
    else if (uiExpandingSpacers == 0 && pStack->m_Justification == plUiStackJustification::SpaceBetween && uiVisibleChildren > 1)
      fSpacing += fExtra / (uiVisibleChildren - 1);

    float fPen = (bHorizontal ? content.x : content.y) + fStartOffset;

    for (plUInt32 i = 0; i < pStack->m_Children.GetCount(); ++i)
    {
      const plUiElement* pChild = pStack->m_Children[i];

      if (pChild == nullptr || !pChild->m_bVisible)
      {
        // collapsed: zero-size slot, no spacing
        out_rects.PushBack(plRectFloat(content.x, content.y, 0, 0));
        continue;
      }

      // the child's preferred size on the main axis (spacers: explicit Width/Height)
      float fSize = GetPreferredMainSize(pChild, bHorizontal);
      if (const plUiSpacer* pSpacer = plDynamicCast<const plUiSpacer*>(pChild))
      {
        if (pSpacer->m_bExpand)
          fSize += fExpandShare;
      }
      const plVec4& margin = pChild->m_vLayoutMargin;
      const float fCrossAvailable = plMath::Max(
        (bHorizontal ? content.height - margin.y - margin.w : content.width - margin.x - margin.z), 0.0f);
      float fCrossSize = fCrossAvailable;
      float fCrossOffset = 0.0f;

      if (pStack->m_Alignment != plUiStackAlignment::Stretch)
      {
        fCrossSize = plMath::Min(GetPreferredCrossSize(pChild, bHorizontal), fCrossAvailable);
        if (pStack->m_Alignment == plUiStackAlignment::Center)
          fCrossOffset = (fCrossAvailable - fCrossSize) * 0.5f;
        else if (pStack->m_Alignment == plUiStackAlignment::End)
          fCrossOffset = fCrossAvailable - fCrossSize;
      }

      if (bHorizontal)
      {
        out_rects.PushBack(plRectFloat(
          fPen + margin.x,
          content.y + margin.y + fCrossOffset,
          fSize,
          fCrossSize));
        fPen += margin.x + fSize + margin.z + fSpacing;
      }
      else
      {
        out_rects.PushBack(plRectFloat(
          content.x + margin.x + fCrossOffset,
          fPen + margin.y,
          fCrossSize,
          fSize));
        fPen += margin.y + fSize + margin.w + fSpacing;
      }
    }
  }

  void ComputeGridRects(const plUiGridPanel* pGrid, const plRectFloat& parentRect, plDynamicArray<plRectFloat>& out_rects)
  {
    const plRectFloat content = GetContentRect(parentRect, pGrid->m_vPadding);
    const plUInt32 uiColumns = plMath::Max(pGrid->m_uiColumns, 1u);

    const float fCellWidth = plMath::Max((content.width - (uiColumns - 1) * pGrid->m_vSpacing.x) / uiColumns, 0.0f);
    const float fCellHeight = pGrid->m_fRowHeight;

    plUInt32 uiCell = 0;

    for (plUInt32 i = 0; i < pGrid->m_Children.GetCount(); ++i)
    {
      const plUiElement* pChild = pGrid->m_Children[i];

      if (pChild == nullptr || !pChild->m_bVisible)
      {
        out_rects.PushBack(plRectFloat(content.x, content.y, 0, 0));
        continue;
      }

      const plUInt32 uiCol = uiCell % uiColumns;
      const plUInt32 uiRow = uiCell / uiColumns;
      ++uiCell;

      out_rects.PushBack(plRectFloat(
        content.x + uiCol * (fCellWidth + pGrid->m_vSpacing.x),
        content.y + uiRow * (fCellHeight + pGrid->m_vSpacing.y),
        fCellWidth, fCellHeight));
    }
  }
} // namespace

float plUiLayout::ComputeStackContentExtent(const plUiStackPanel* pStack, const plRectFloat& parentRect)
{
  // mirrors the fPen accumulation of ComputeStackRects, without the padding-relative origin
  const bool bHorizontal = pStack->m_Direction == plUiStackDirection::Horizontal;

  float fExtent = bHorizontal ? (pStack->m_vPadding.x + pStack->m_vPadding.z) : (pStack->m_vPadding.y + pStack->m_vPadding.w);
  bool bAny = false;

  for (const plUiElement* pChild : pStack->m_Children)
  {
    if (pChild == nullptr || !pChild->m_bVisible)
      continue;

    const plVec4& margin = pChild->m_vLayoutMargin;
    fExtent += GetPreferredMainSize(pChild, bHorizontal);
    fExtent += bHorizontal ? margin.x + margin.z : margin.y + margin.w;

    if (bAny)
    {
      fExtent += pStack->m_fSpacing;
    }

    bAny = true;
  }

  PL_IGNORE_UNUSED(parentRect);
  return fExtent;
}

void plUiLayout::ComputeChildRects(const plUiElement* pParent, const plRectFloat& parentRect, plDynamicArray<plRectFloat>& out_rects)
{
  out_rects.Clear();

  if (const plUiStackPanel* pStack = plDynamicCast<const plUiStackPanel*>(pParent))
  {
    ComputeStackRects(pStack, parentRect, out_rects);
    return;
  }

  if (const plUiGridPanel* pGrid = plDynamicCast<const plUiGridPanel*>(pParent))
  {
    ComputeGridRects(pGrid, parentRect, out_rects);
    return;
  }

  PL_ASSERT_DEV(pParent != nullptr, "ComputeChildRects requires a parent element; use ComputeChildRect for pseudo-root children");

  for (const plUiElement* pChild : pParent->m_Children)
  {
    out_rects.PushBack(pChild != nullptr ? pChild->ComputeRect(parentRect) : plRectFloat(0, 0, 0, 0));
  }
}

plRectFloat plUiLayout::ComputeChildRect(const plUiElement* pParent, const plUiElement* pChild, const plRectFloat& parentRect, plUInt32 uiChildIndex)
{
  if (pChild == nullptr)
    return plRectFloat(0, 0, 0, 0);

  if (pParent == nullptr || (plDynamicCast<const plUiStackPanel*>(pParent) == nullptr && plDynamicCast<const plUiGridPanel*>(pParent) == nullptr))
  {
    return pChild->ComputeRect(parentRect);
  }

  plDynamicArray<plRectFloat> tmp;
  ComputeChildRects(pParent, parentRect, tmp);

  if (uiChildIndex < tmp.GetCount())
    return tmp[uiChildIndex];

  return plRectFloat(0, 0, 0, 0);
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Elements_Implementation_UiLayout);
