#include <GameUiPlugin/GameUiPluginPCH.h>

#include <GameUiPlugin/Components/UiCanvasComponent.h>
#include <GameUiPlugin/Elements/UiLayout.h>
#include <GameUiPlugin/Rendering/UiRenderData.h>

#include <Core/Input/InputManager.h>
#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Memory/FrameAllocator.h>
#include <Foundation/Strings/UnicodeUtils.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>
#include <RendererCore/Pipeline/RenderData.h>
#include <RendererCore/Pipeline/RenderDataManager.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>

#include <functional>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plUiCanvasScaleMode, 1)
  PL_ENUM_CONSTANTS(plUiCanvasScaleMode::ConstantPixel, plUiCanvasScaleMode::ScaleWithHeight)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_COMPONENT_TYPE(plUiCanvasComponent, 4, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_RESOURCE_ACCESSOR_PROPERTY("UiDocument", GetUiDocument, SetUiDocument)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Ui_Document")),
    PL_ENUM_ACCESSOR_PROPERTY("ScaleMode", plUiCanvasScaleMode, GetScaleMode, SetScaleMode),
    PL_ACCESSOR_PROPERTY("ReferenceHeight", GetReferenceHeight, SetReferenceHeight)->AddAttributes(new plDefaultValueAttribute(1080), new plSuffixAttribute("px")),
    PL_ACCESSOR_PROPERTY("SortOrder", GetSortOrder, SetSortOrder),
    PL_ACCESSOR_PROPERTY("AutobindBlackboards", GetAutobindBlackboards, SetAutobindBlackboards)->AddAttributes(new plDefaultValueAttribute(true)),
    PL_ACCESSOR_PROPERTY("CancelEvent", GetCancelEvent, SetCancelEvent),
    PL_ACCESSOR_PROPERTY("PublishInteractionState", GetPublishInteractionState, SetPublishInteractionState)->AddAttributes(new plDefaultValueAttribute(true)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_MESSAGEHANDLERS
  {
    PL_MESSAGE_HANDLER(plMsgExtractRenderData, OnMsgExtractRenderData),
  }
  PL_END_MESSAGEHANDLERS;
  PL_BEGIN_MESSAGESENDERS
  {
    PL_MESSAGE_SENDER(m_UiEventSender),
  }
  PL_END_MESSAGESENDERS;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("UI/GameUi"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_COMPONENT_TYPE
// clang-format on

namespace
{
  /// One authored post-layout transform. UI geometry remains a flat plane, but X/Y/Z
  /// rotations and Z translation are projected through a configurable focal distance.
  struct UiSubtreeTransform
  {
    plVec3 m_vPivot = plVec3::MakeZero();
    plVec3 m_vTranslation = plVec3::MakeZero();
    plVec3 m_vScale = plVec3(1.0f);
    plVec3 m_vSin = plVec3::MakeZero();
    plVec3 m_vCos = plVec3(1.0f);
    float m_fPerspective = 1000.0f;
  };

  using UiTransformStack = plHybridArray<UiSubtreeTransform, 8>;

  bool HasSubtreeTransform(const plUiElement* pElement)
  {
    return pElement->m_vTransformCenter != plVec3::MakeZero() ||
           pElement->m_vTransformTranslation != plVec3::MakeZero() ||
           pElement->m_vTransformRotation != plVec3::MakeZero() ||
           pElement->m_vTransformScale != plVec3(1.0f);
  }

  UiSubtreeTransform MakeSubtreeTransform(const plUiElement* pElement, const plRectFloat& rect)
  {
    UiSubtreeTransform transform;
    transform.m_vPivot.Set(
      rect.x + rect.width * pElement->m_vTransformOrigin.x + pElement->m_vTransformCenter.x,
      rect.y + rect.height * pElement->m_vTransformOrigin.y + pElement->m_vTransformCenter.y,
      pElement->m_vTransformCenter.z);
    transform.m_vTranslation = pElement->m_vTransformTranslation;
    transform.m_vScale = pElement->m_vTransformScale;
    transform.m_fPerspective = plMath::Max(pElement->m_fTransformPerspective, 1.0f);

    const plAngle angleX = plAngle::MakeFromDegree(pElement->m_vTransformRotation.x);
    const plAngle angleY = plAngle::MakeFromDegree(pElement->m_vTransformRotation.y);
    const plAngle angleZ = plAngle::MakeFromDegree(pElement->m_vTransformRotation.z);
    transform.m_vSin.Set(plMath::Sin(angleX), plMath::Sin(angleY), plMath::Sin(angleZ));
    transform.m_vCos.Set(plMath::Cos(angleX), plMath::Cos(angleY), plMath::Cos(angleZ));
    return transform;
  }

  plVec2 ApplySubtreeTransform(const UiSubtreeTransform& transform, const plVec2& vPoint)
  {
    plVec3 p(
      (vPoint.x - transform.m_vPivot.x) * transform.m_vScale.x,
      (vPoint.y - transform.m_vPivot.y) * transform.m_vScale.y,
      -transform.m_vPivot.z * transform.m_vScale.z);

    // X, then Y, then Z, matching CompositeTransform3D's component order.
    {
      const float y = p.y * transform.m_vCos.x - p.z * transform.m_vSin.x;
      const float z = p.y * transform.m_vSin.x + p.z * transform.m_vCos.x;
      p.y = y;
      p.z = z;
    }
    {
      const float x = p.x * transform.m_vCos.y + p.z * transform.m_vSin.y;
      const float z = -p.x * transform.m_vSin.y + p.z * transform.m_vCos.y;
      p.x = x;
      p.z = z;
    }
    {
      const float x = p.x * transform.m_vCos.z - p.y * transform.m_vSin.z;
      const float y = p.x * transform.m_vSin.z + p.y * transform.m_vCos.z;
      p.x = x;
      p.y = y;
    }

    const float fDepth = p.z + transform.m_vPivot.z + transform.m_vTranslation.z;
    const float fDenominator = plMath::Max(transform.m_fPerspective + fDepth, 1.0f);
    const float fProjection = transform.m_fPerspective / fDenominator;

    return plVec2(
      transform.m_vPivot.x + transform.m_vTranslation.x + p.x * fProjection,
      transform.m_vPivot.y + transform.m_vTranslation.y + p.y * fProjection);
  }

  plVec2 TransformCanvasPoint(const UiTransformStack& transforms, plVec2 vPoint)
  {
    // Child transforms are applied before parent transforms.
    for (plUInt32 i = transforms.GetCount(); i > 0; --i)
    {
      vPoint = ApplySubtreeTransform(transforms[i - 1], vPoint);
    }
    return vPoint;
  }

  void GetTransformedRectCorners(const UiTransformStack& transforms, const plRectFloat& rect, plVec2 out_corners[4])
  {
    out_corners[0] = TransformCanvasPoint(transforms, plVec2(rect.x, rect.y));
    out_corners[1] = TransformCanvasPoint(transforms, plVec2(rect.x + rect.width, rect.y));
    out_corners[2] = TransformCanvasPoint(transforms, plVec2(rect.x + rect.width, rect.y + rect.height));
    out_corners[3] = TransformCanvasPoint(transforms, plVec2(rect.x, rect.y + rect.height));
  }

  plRectFloat GetTransformedRectBounds(const UiTransformStack& transforms, const plRectFloat& rect)
  {
    plVec2 corners[4];
    GetTransformedRectCorners(transforms, rect, corners);

    float x0 = corners[0].x;
    float y0 = corners[0].y;
    float x1 = corners[0].x;
    float y1 = corners[0].y;
    for (plUInt32 i = 1; i < 4; ++i)
    {
      x0 = plMath::Min(x0, corners[i].x);
      y0 = plMath::Min(y0, corners[i].y);
      x1 = plMath::Max(x1, corners[i].x);
      y1 = plMath::Max(y1, corners[i].y);
    }
    return plRectFloat(x0, y0, plMath::Max(x1 - x0, 0.0f), plMath::Max(y1 - y0, 0.0f));
  }

  bool TransformedRectContains(const UiTransformStack& transforms, const plRectFloat& rect, const plVec2& vPoint)
  {
    if (rect.width <= 0.0f || rect.height <= 0.0f)
      return false;

    plVec2 corners[4];
    GetTransformedRectCorners(transforms, rect, corners);

    bool bPositive = false;
    bool bNegative = false;
    for (plUInt32 i = 0; i < 4; ++i)
    {
      const plVec2 edge = corners[(i + 1) % 4] - corners[i];
      const plVec2 toPoint = vPoint - corners[i];
      const float fCross = edge.x * toPoint.y - edge.y * toPoint.x;
      bPositive |= fCross > 0.001f;
      bNegative |= fCross < -0.001f;
      if (bPositive && bNegative)
        return false;
    }
    return true;
  }

  struct plUiDrawContext
  {
    plDynamicArray<plUiVertex> m_Vertices;
    plDynamicArray<plUInt32> m_Indices;
    plDynamicArray<plUiBatch> m_Batches;
    float m_fScale = 1.0f;
    plVec2 m_vOffset = plVec2::MakeZero(); ///< screen-space offset (letterboxed device preview)

    // current rotation state (canvas space): applied to every quad emitted while set;
    // used for the Rotation animation channel on an element's OWN visuals
    float m_fRotationSin = 0.0f;
    float m_fRotationCos = 1.0f;
    bool m_bRotate = false;
    plVec2 m_vRotationPivot = plVec2::MakeZero();
    UiTransformStack m_SubtreeTransforms;

    const plDebugRendererContext* m_pDebugContext = nullptr;
    plUiElementInstance m_HoveredElement;
    plUiElementInstance m_PressedElement;
    plUiElementInstance m_FocusedElement;
    const plUiBindingState* m_pBindings = nullptr;

    // per-instance widget runtime state (written in Update on the main thread, read here)
    const plHashTable<plUiElementInstance, float>* m_pScrollOffsets = nullptr;
    const plHashTable<plUiElementInstance, bool>* m_pLocalToggleState = nullptr;
    const plHashTable<plUiElementInstance, float>* m_pLocalSliderValues = nullptr;
    const plHashTable<plUiElementInstance, plUiTextEditState>* m_pTextEditStates = nullptr;
    const plDynamicArray<plUiDynamicInstance>* m_pDynamicInstances = nullptr; ///< spawned instances (anchored by name)

    // screen-space clip stack; [0] is the full viewport (pushed by the extractor)
    plHybridArray<plRectU32, 8> m_ClipStack;

    plUInt32 m_uiSubDocumentDepth = 0; ///< recursion guard for nested plUiSubDocuments
    plUInt32 m_uiInstanceSalt = 0;     ///< instance salt of the sub-tree currently drawn (0 = canvas document)

    const plRectU32& CurrentClip() const { return m_ClipStack.PeekBack(); }

    /// intersects the canvas-space rect with the current clip; rounds OUTWARD so
    /// fractional device-preset zoom never shimmers away a boundary pixel row
    void PushClip(const plRectFloat& canvasRect)
    {
      const plRectFloat transformedRect = GetTransformedRectBounds(m_SubtreeTransforms, canvasRect);
      const plInt32 x0 = static_cast<plInt32>(plMath::Floor(transformedRect.x * m_fScale + m_vOffset.x));
      const plInt32 y0 = static_cast<plInt32>(plMath::Floor(transformedRect.y * m_fScale + m_vOffset.y));
      const plInt32 x1 = static_cast<plInt32>(plMath::Ceil((transformedRect.x + transformedRect.width) * m_fScale + m_vOffset.x));
      const plInt32 y1 = static_cast<plInt32>(plMath::Ceil((transformedRect.y + transformedRect.height) * m_fScale + m_vOffset.y));

      const plRectU32& current = CurrentClip();

      const plInt32 iClippedX0 = plMath::Max(x0, static_cast<plInt32>(current.x));
      const plInt32 iClippedY0 = plMath::Max(y0, static_cast<plInt32>(current.y));
      const plInt32 iClippedX1 = plMath::Min(x1, static_cast<plInt32>(current.x + current.width));
      const plInt32 iClippedY1 = plMath::Min(y1, static_cast<plInt32>(current.y + current.height));

      plRectU32 result(0, 0, 0, 0);

      if (iClippedX1 > iClippedX0 && iClippedY1 > iClippedY0 && iClippedX1 > 0 && iClippedY1 > 0)
      {
        const plInt32 iFinalX0 = plMath::Max(iClippedX0, 0);
        const plInt32 iFinalY0 = plMath::Max(iClippedY0, 0);
        result = plRectU32(static_cast<plUInt32>(iFinalX0), static_cast<plUInt32>(iFinalY0), static_cast<plUInt32>(iClippedX1 - iFinalX0), static_cast<plUInt32>(iClippedY1 - iFinalY0));
      }

      m_ClipStack.PushBack(result);
    }

    void PopClip() { m_ClipStack.PopBack(); }

    /// SDF brush parameters of one quad (canvas-space px; scaled to screen px on emit)
    struct SdfQuadParams
    {
      float m_fMode = 0.0f; ///< 0 plain, 1 fill, 2 border, 3 shadow
      plVec4 m_vRadii = plVec4::MakeZero();
      float m_fBorderWidth = 0.0f;
      float m_fSoftness = 0.0f;
      plColorLinearUB m_ColorB = plColorLinearUB(255, 255, 255, 255);
    };

    void AddQuadColors(const plRectFloat& rect, const plVec2& vUvMin, const plVec2& vUvMax, const plColorLinearUB* pCornerColors, const plTexture2DResourceHandle* pTexture, plGALTextureHandle hNativeTexture = plGALTextureHandle(), bool bSdfText = false, const SdfQuadParams& sdf = SdfQuadParams())
    {
      if (rect.width <= 0.0f || rect.height <= 0.0f)
        return;

      const plRectU32& clip = CurrentClip();

      if (clip.width == 0 || clip.height == 0)
        return; // fully clipped away

      // continue the current batch if the texture/shader/clip state matches, otherwise start a new one
      if (m_Batches.IsEmpty() || m_Batches.PeekBack().m_pTexture != pTexture || m_Batches.PeekBack().m_hNativeTexture != hNativeTexture || m_Batches.PeekBack().m_bSdfText != bSdfText || m_Batches.PeekBack().m_ScissorRect != clip)
      {
        plUiBatch& batch = m_Batches.ExpandAndGetRef();
        batch.m_uiFirstIndex = m_Indices.GetCount();
        batch.m_uiIndexCount = 0;
        batch.m_pTexture = pTexture;
        batch.m_hNativeTexture = hNativeTexture;
        batch.m_bSdfText = bSdfText;
        batch.m_ScissorRect = clip;
      }

      const plUInt32 uiFirstVertex = m_Vertices.GetCount();

      // canvas-space corners, optionally rotated around the pivot, then mapped to screen
      plVec2 corners[4] = {
        plVec2(rect.x, rect.y),
        plVec2(rect.x + rect.width, rect.y),
        plVec2(rect.x + rect.width, rect.y + rect.height),
        plVec2(rect.x, rect.y + rect.height)};

      if (m_bRotate)
      {
        for (plVec2& corner : corners)
        {
          const plVec2 rel = corner - m_vRotationPivot;
          corner = m_vRotationPivot + plVec2(rel.x * m_fRotationCos - rel.y * m_fRotationSin, rel.x * m_fRotationSin + rel.y * m_fRotationCos);
        }
      }

      for (plVec2& corner : corners)
      {
        corner = TransformCanvasPoint(m_SubtreeTransforms, corner);
      }

      const plVec2 uvs[4] = {
        plVec2(vUvMin.x, vUvMin.y),
        plVec2(vUvMax.x, vUvMin.y),
        plVec2(vUvMax.x, vUvMax.y),
        plVec2(vUvMin.x, vUvMax.y)};

      // local corner UVs of the quad (pre-rotation, so the SDF rotates with the shape)
      const plVec2 localUvs[4] = {plVec2(0, 0), plVec2(1, 0), plVec2(1, 1), plVec2(0, 1)};

      // the SDF works in SCREEN pixels so anti-aliasing stays crisp at any zoom
      plUiVertex v;
      v.m_SdfCoord = plVec4(0, 0, sdf.m_fMode, sdf.m_fSoftness * m_fScale);
      v.m_SdfParams = plVec4(rect.width * 0.5f * m_fScale, rect.height * 0.5f * m_fScale, sdf.m_fBorderWidth * m_fScale, 0);
      v.m_SdfRadii = sdf.m_vRadii * m_fScale;
      v.m_ColorB = sdf.m_ColorB;

      for (plUInt32 i = 0; i < 4; ++i)
      {
        v.m_Position.Set(corners[i].x * m_fScale + m_vOffset.x, corners[i].y * m_fScale + m_vOffset.y, 0);
        v.m_TexCoord.Set(uvs[i].x, uvs[i].y);
        v.m_SdfCoord.x = localUvs[i].x;
        v.m_SdfCoord.y = localUvs[i].y;
        v.m_Color = pCornerColors[i];
        m_Vertices.PushBack(v);
      }

      m_Indices.PushBack(uiFirstVertex + 0);
      m_Indices.PushBack(uiFirstVertex + 1);
      m_Indices.PushBack(uiFirstVertex + 2);
      m_Indices.PushBack(uiFirstVertex + 0);
      m_Indices.PushBack(uiFirstVertex + 2);
      m_Indices.PushBack(uiFirstVertex + 3);

      m_Batches.PeekBack().m_uiIndexCount += 6;
    }

    /// Emits an untextured triangle list (canvas-space points, consecutive triples).
    /// Shares the quad batching (white-texture fallback, current scissor, rotation state).
    void AddTriangles(plArrayPtr<const plVec2> points, const plColorLinearUB& color)
    {
      if (points.GetCount() < 3 || color.a == 0)
        return;

      const plRectU32& clip = CurrentClip();
      if (clip.width == 0 || clip.height == 0)
        return;

      if (m_Batches.IsEmpty() || m_Batches.PeekBack().m_pTexture != nullptr || !m_Batches.PeekBack().m_hNativeTexture.IsInvalidated() || m_Batches.PeekBack().m_bSdfText || m_Batches.PeekBack().m_ScissorRect != clip)
      {
        plUiBatch& batch = m_Batches.ExpandAndGetRef();
        batch.m_uiFirstIndex = m_Indices.GetCount();
        batch.m_uiIndexCount = 0;
        batch.m_pTexture = nullptr;
        batch.m_hNativeTexture = plGALTextureHandle();
        batch.m_bSdfText = false;
        batch.m_ScissorRect = clip;
      }

      const plUInt32 uiFirstVertex = m_Vertices.GetCount();
      const plUInt32 uiPointCount = points.GetCount() - (points.GetCount() % 3);

      plUiVertex v;
      v.m_TexCoord.Set(0, 0);
      v.m_SdfCoord = plVec4(0, 0, 0, 0);
      v.m_SdfParams = plVec4::MakeZero();
      v.m_SdfRadii = plVec4::MakeZero();
      v.m_Color = color;
      v.m_ColorB = color;

      for (plUInt32 i = 0; i < uiPointCount; ++i)
      {
        plVec2 p = points[i];

        if (m_bRotate)
        {
          const plVec2 rel = p - m_vRotationPivot;
          p = m_vRotationPivot + plVec2(rel.x * m_fRotationCos - rel.y * m_fRotationSin, rel.x * m_fRotationSin + rel.y * m_fRotationCos);
        }

        p = TransformCanvasPoint(m_SubtreeTransforms, p);
        v.m_Position.Set(p.x * m_fScale + m_vOffset.x, p.y * m_fScale + m_vOffset.y, 0);
        m_Vertices.PushBack(v);
        m_Indices.PushBack(uiFirstVertex + i);
      }

      m_Batches.PeekBack().m_uiIndexCount += uiPointCount;
    }

    void AddQuad(const plRectFloat& rect, const plVec2& vUvMin, const plVec2& vUvMax, const plColorLinearUB& color, const plTexture2DResourceHandle* pTexture, plGALTextureHandle hNativeTexture = plGALTextureHandle(), bool bSdfText = false, const SdfQuadParams& sdf = SdfQuadParams())
    {
      const plColorLinearUB colors[4] = {color, color, color, color};
      AddQuadColors(rect, vUvMin, vUvMax, colors, pTexture, hNativeTexture, bSdfText, sdf);
    }
  };

  /// resolved brush effects for one draw (final colors with tint/opacity applied; canvas px)
  struct BrushFx
  {
    plVec4 m_vRadii = plVec4::MakeZero(); ///< clamped by DrawElementBrush
    float m_fBorderWidth = 0.0f;
    plColorLinearUB m_BorderColor = plColorLinearUB(255, 255, 255, 0);
    plUiGradientMode::Enum m_GradientMode = plUiGradientMode::None;
    plColorLinearUB m_GradientColor = plColorLinearUB(128, 128, 128, 255);
    float m_fShadowSize = 0.0f;
    plVec2 m_vShadowOffset = plVec2::MakeZero();
    plColorLinearUB m_ShadowColor = plColorLinearUB(0, 0, 0, 0);
    float m_fOpacity = 1.0f; ///< element opacity, applied to the border band
  };

  plVec4 ClampRadii(const plVec4& vRadii, const plRectFloat& rect)
  {
    const float fMax = plMath::Max(plMath::Min(rect.width, rect.height) * 0.5f, 0.0f);
    return plVec4(plMath::Clamp(vRadii.x, 0.0f, fMax), plMath::Clamp(vRadii.y, 0.0f, fMax), plMath::Clamp(vRadii.z, 0.0f, fMax), plMath::Clamp(vRadii.w, 0.0f, fMax));
  }

  /// corner colors (TL TR BR BL) for a PARTIAL fill quad so the gradient spans the FULL
  /// widget: a horizontal fill at t only reveals the gradient up to lerp(A, B, t)
  void ComputeFillGradientCorners(plUiGradientMode::Enum mode, const plColor& colorA, const plColor& colorB, float fFillT, plColorLinearUB out_corners[4])
  {
    if (mode == plUiGradientMode::Horizontal)
    {
      const plColorLinearUB a(colorA);
      const plColorLinearUB right(plMath::Lerp(colorA, colorB, plMath::Clamp(fFillT, 0.0f, 1.0f)));

      out_corners[0] = a;     // TL
      out_corners[1] = right; // TR
      out_corners[2] = right; // BR
      out_corners[3] = a;     // BL
    }
    else
    {
      // vertical: the fill covers the full height, so the gradient is complete
      const plColorLinearUB a(colorA);
      const plColorLinearUB b(colorB);

      out_corners[0] = a; // TL
      out_corners[1] = a; // TR
      out_corners[2] = b; // BR
      out_corners[3] = b; // BL
    }
  }

  /// Draws a plUiPath's tessellated geometry mapped into the element rect.
  void DrawPathElement(const plUiPath* pPath, const plRectFloat& rect, const plColorLinearUB& color, plUiDrawContext& ref_ctx)
  {
    plVec2 vMin, vMax;
    const plDynamicArray<plVec2>& triangles = pPath->GetTriangles(vMin, vMax);

    if (triangles.IsEmpty())
      return;

    const plVec2 vPathSize(plMath::Max(vMax.x - vMin.x, 0.0001f), plMath::Max(vMax.y - vMin.y, 0.0001f));

    plVec2 vScale(1.0f, 1.0f);
    plVec2 vOrigin(rect.x, rect.y);

    switch (pPath->m_Stretch.GetValue())
    {
      case plUiPathStretch::Fill:
        vScale.Set(rect.width / vPathSize.x, rect.height / vPathSize.y);
        break;

      case plUiPathStretch::Uniform:
      {
        const float fScale = plMath::Min(rect.width / vPathSize.x, rect.height / vPathSize.y);
        vScale.Set(fScale, fScale);
        vOrigin.x += (rect.width - vPathSize.x * fScale) * 0.5f;
        vOrigin.y += (rect.height - vPathSize.y * fScale) * 0.5f;
        break;
      }

      case plUiPathStretch::None:
      default:
        break;
    }

    plHybridArray<plVec2, 128> transformed;
    transformed.SetCountUninitialized(triangles.GetCount());

    for (plUInt32 i = 0; i < triangles.GetCount(); ++i)
    {
      transformed[i].Set(
        vOrigin.x + (triangles[i].x - vMin.x) * vScale.x,
        vOrigin.y + (triangles[i].y - vMin.y) * vScale.y);
    }

    ref_ctx.AddTriangles(transformed, color);
  }

  void DrawElementBrush(plUiBrushMode::Enum brushMode, const plVec4& vSliceMargins, const plTexture2DResourceHandle* pTexture, const plRectFloat& rect, const plColorLinearUB& vertexColor, const BrushFx& fx, plUiDrawContext& ref_ctx)
  {
    const plVec4 vRadii = ClampRadii(fx.m_vRadii, rect);
    const bool bRounded = vRadii.x > 0.0f || vRadii.y > 0.0f || vRadii.z > 0.0f || vRadii.w > 0.0f;

    // drop shadow behind the brush (all brush modes): the shape grown by ShadowSize,
    // fading from solid at the original outline to zero at the grown outline
    if (fx.m_fShadowSize > 0.0f && fx.m_ShadowColor.a > 0)
    {
      const float fGrow = fx.m_fShadowSize;
      const plRectFloat shadowRect(rect.x + fx.m_vShadowOffset.x - fGrow, rect.y + fx.m_vShadowOffset.y - fGrow, rect.width + fGrow * 2.0f, rect.height + fGrow * 2.0f);

      plUiDrawContext::SdfQuadParams shadow;
      shadow.m_fMode = 3.0f;
      shadow.m_fSoftness = fGrow;
      shadow.m_vRadii = plVec4(vRadii.x + fGrow, vRadii.y + fGrow, vRadii.z + fGrow, vRadii.w + fGrow);

      ref_ctx.AddQuad(shadowRect, plVec2::MakeZero(), plVec2(1, 1), fx.m_ShadowColor, nullptr, plGALTextureHandle(), false, shadow);
    }

    const bool bTextured = brushMode != plUiBrushMode::Solid && pTexture != nullptr && pTexture->IsValid();

    if (brushMode != plUiBrushMode::NineSlice || !bTextured)
    {
      // Solid / Image (or nine-slice fallback while the texture loads): one quad,
      // optionally SDF-rounded and/or gradient-tinted per corner
      plUiDrawContext::SdfQuadParams sdf;
      sdf.m_fMode = bRounded ? 1.0f : 0.0f;
      sdf.m_vRadii = vRadii;

      const bool bFillVisible = vertexColor.a > 0 || (fx.m_GradientMode != plUiGradientMode::None && fx.m_GradientColor.a > 0);

      if (!bFillVisible)
      {
      }
      else if (fx.m_GradientMode != plUiGradientMode::None)
      {
        // corner order TL, TR, BR, BL
        const plColorLinearUB& c = vertexColor;
        const plColorLinearUB& g = fx.m_GradientColor;
        const bool bHorizontal = fx.m_GradientMode == plUiGradientMode::Horizontal;
        const plColorLinearUB corners[4] = {c, bHorizontal ? g : c, g, bHorizontal ? c : g};
        ref_ctx.AddQuadColors(rect, plVec2::MakeZero(), plVec2(1, 1), corners, bTextured ? pTexture : nullptr, plGALTextureHandle(), false, sdf);
      }
      else
      {
        ref_ctx.AddQuad(rect, plVec2::MakeZero(), plVec2(1, 1), vertexColor, bTextured ? pTexture : nullptr, plGALTextureHandle(), false, sdf);
      }

      // border hugging the (rounded) outline
      if (fx.m_fBorderWidth > 0.0f && fx.m_BorderColor.a > 0)
      {
        plUiDrawContext::SdfQuadParams border;
        border.m_fMode = 2.0f;
        border.m_vRadii = vRadii;
        border.m_fBorderWidth = fx.m_fBorderWidth;
        border.m_ColorB = fx.m_BorderColor;

        const plColorLinearUB opacityCarrier(255, 255, 255, static_cast<plUInt8>(plMath::Clamp(fx.m_fOpacity, 0.0f, 1.0f) * 255.0f));
        ref_ctx.AddQuad(rect, plVec2::MakeZero(), plVec2(1, 1), opacityCarrier, nullptr, plGALTextureHandle(), false, border);
      }

      return;
    }

    // NineSlice: needs the texture dimensions to compute UV splits
    plVec2 vTextureSize = plVec2::MakeZero();
    {
      plResourceLock<plTexture2DResource> pTextureResource(*pTexture, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
      if (pTextureResource.GetAcquireResult() == plResourceAcquireResult::Final)
      {
        vTextureSize.Set(static_cast<float>(pTextureResource->GetWidth()), static_cast<float>(pTextureResource->GetHeight()));
      }
    }

    if (vTextureSize.x <= 0.0f || vTextureSize.y <= 0.0f)
    {
      // texture not loaded yet, fall back to a stretched quad
      ref_ctx.AddQuad(rect, plVec2::MakeZero(), plVec2(1, 1), vertexColor, pTexture);
      return;
    }

    // margins in target pixels, clamped so they never overlap
    const float fMarginL = plMath::Min(vSliceMargins.x, rect.width * 0.5f);
    const float fMarginT = plMath::Min(vSliceMargins.y, rect.height * 0.5f);
    const float fMarginR = plMath::Min(vSliceMargins.z, rect.width * 0.5f);
    const float fMarginB = plMath::Min(vSliceMargins.w, rect.height * 0.5f);

    const float xs[4] = {rect.x, rect.x + fMarginL, rect.x + rect.width - fMarginR, rect.x + rect.width};
    const float ys[4] = {rect.y, rect.y + fMarginT, rect.y + rect.height - fMarginB, rect.y + rect.height};

    const float us[4] = {0.0f, vSliceMargins.x / vTextureSize.x, 1.0f - (vSliceMargins.z / vTextureSize.x), 1.0f};
    const float vs[4] = {0.0f, vSliceMargins.y / vTextureSize.y, 1.0f - (vSliceMargins.w / vTextureSize.y), 1.0f};

    for (plUInt32 row = 0; row < 3; ++row)
    {
      for (plUInt32 col = 0; col < 3; ++col)
      {
        const plRectFloat cell(xs[col], ys[row], xs[col + 1] - xs[col], ys[row + 1] - ys[row]);
        ref_ctx.AddQuad(cell, plVec2(us[col], vs[row]), plVec2(us[col + 1], vs[row + 1]), vertexColor, pTexture);
      }
    }
  }

  /// \brief Flattened visual values of an element after the style + state cross-fades.
  ///
  /// Texture pointers reference handles inside the pinned document tree or inside style
  /// resources pinned by handles in that tree — both outlive the frame (see plUiBatch).
  struct EffectiveVisuals
  {
    plColor m_Color = plColor::White;
    plUiBrushMode::Enum m_BrushMode = plUiBrushMode::Solid;
    const plTexture2DResourceHandle* m_pTexture = nullptr;
    plVec4 m_vSliceMargins = plVec4(16.0f);

    plVec4 m_vCornerRadius = plVec4::MakeZero();
    float m_fBorderWidth = 0.0f;
    plColor m_BorderColor = plColor::White;
    plUiGradientMode::Enum m_GradientMode = plUiGradientMode::None;
    plColor m_GradientColor = plColor(0.5f, 0.5f, 0.5f, 1.0f);
    float m_fShadowSize = 0.0f;
    plVec2 m_vShadowOffset = plVec2(0.0f, 4.0f);
    plColor m_ShadowColor = plColor(0.0f, 0.0f, 0.0f, 0.59f);

    plColor m_HoverColor = plColor::White;
    plColor m_PressedColor = plColor::White;
    plColor m_DisabledColor = plColor(0.43f, 0.43f, 0.43f, 0.78f);
    plColor m_CheckedColor = plColor::White;
    const plTexture2DResourceHandle* m_pHoverTexture = nullptr;
    const plTexture2DResourceHandle* m_pPressedTexture = nullptr;
    const plTexture2DResourceHandle* m_pDisabledTexture = nullptr;
    const plTexture2DResourceHandle* m_pCheckedTexture = nullptr;

    plUiFontResourceHandle m_hFont;
    plColor m_TextColor = plColor::White;
    float m_fFontSize = 16.0f;

    plColor m_FillColor = plColor::White;
    const plTexture2DResourceHandle* m_pFillTexture = nullptr;
    plColor m_HandleColor = plColor::White;
    const plTexture2DResourceHandle* m_pHandleTexture = nullptr;
  };

  /// \brief Keeps the style resources acquired during one element's draw alive.
  class ScopedStyleSet
  {
  public:
    ~ScopedStyleSet()
    {
      for (plUiStyleResource* pResource : m_Resources)
      {
        plResourceManager::EndAcquireResource(pResource);
      }
    }

    /// nullptr while the style is unset or not loaded (graceful: local visuals remain)
    const plUiStyle* Acquire(const plUiStyleResourceHandle& hStyle)
    {
      if (!hStyle.IsValid())
        return nullptr;

      plUiStyleResource* pResource = plResourceManager::BeginAcquireResource(hStyle, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

      if (pResource == nullptr)
        return nullptr;

      m_Resources.PushBack(pResource);
      return pResource->GetLoadingState() == plResourceState::Loaded ? pResource->GetStyle() : nullptr;
    }

  private:
    plHybridArray<plUiStyleResource*, 5> m_Resources;
  };

  void AssignStyle(EffectiveVisuals& ref_visuals, const plUiStyle& style)
  {
    ref_visuals.m_Color = style.m_Color;
    ref_visuals.m_BrushMode = static_cast<plUiBrushMode::Enum>(style.m_BrushMode.GetValue());
    ref_visuals.m_pTexture = &style.m_hTexture;
    ref_visuals.m_vSliceMargins = style.m_vSliceMargins;
    ref_visuals.m_vCornerRadius = style.m_vCornerRadius;
    ref_visuals.m_fBorderWidth = style.m_fBorderWidth;
    ref_visuals.m_BorderColor = style.m_BorderColor;
    ref_visuals.m_GradientMode = static_cast<plUiGradientMode::Enum>(style.m_GradientMode.GetValue());
    ref_visuals.m_GradientColor = style.m_GradientColor;
    ref_visuals.m_fShadowSize = style.m_fShadowSize;
    ref_visuals.m_vShadowOffset = style.m_vShadowOffset;
    ref_visuals.m_ShadowColor = style.m_ShadowColor;
    ref_visuals.m_HoverColor = style.m_HoverColor;
    ref_visuals.m_PressedColor = style.m_PressedColor;
    ref_visuals.m_DisabledColor = style.m_DisabledColor;
    ref_visuals.m_CheckedColor = style.m_CheckedColor;
    ref_visuals.m_pHoverTexture = &style.m_hHoverTexture;
    ref_visuals.m_pPressedTexture = &style.m_hPressedTexture;
    ref_visuals.m_pDisabledTexture = &style.m_hDisabledTexture;
    ref_visuals.m_pCheckedTexture = &style.m_hCheckedTexture;
    ref_visuals.m_hFont = style.m_hFont;
    ref_visuals.m_TextColor = style.m_TextColor;
    ref_visuals.m_fFontSize = static_cast<float>(style.m_uiFontSize);
    ref_visuals.m_FillColor = style.m_FillColor;
    ref_visuals.m_pFillTexture = &style.m_hFillTexture;
    ref_visuals.m_HandleColor = style.m_HandleColor;
    ref_visuals.m_pHandleTexture = &style.m_hHandleTexture;
  }

  /// cross-fades a state's style over the current values: colors/sizes lerp, textures and
  /// fonts (which cannot blend) switch at half blend
  void LerpToStyle(EffectiveVisuals& ref_visuals, const plUiStyle& style, float fBlend)
  {
    ref_visuals.m_Color = plMath::Lerp(ref_visuals.m_Color, plColor(style.m_Color), fBlend);
    ref_visuals.m_HoverColor = plMath::Lerp(ref_visuals.m_HoverColor, plColor(style.m_HoverColor), fBlend);
    ref_visuals.m_PressedColor = plMath::Lerp(ref_visuals.m_PressedColor, plColor(style.m_PressedColor), fBlend);
    ref_visuals.m_DisabledColor = plMath::Lerp(ref_visuals.m_DisabledColor, plColor(style.m_DisabledColor), fBlend);
    ref_visuals.m_CheckedColor = plMath::Lerp(ref_visuals.m_CheckedColor, plColor(style.m_CheckedColor), fBlend);
    ref_visuals.m_TextColor = plMath::Lerp(ref_visuals.m_TextColor, plColor(style.m_TextColor), fBlend);
    ref_visuals.m_FillColor = plMath::Lerp(ref_visuals.m_FillColor, plColor(style.m_FillColor), fBlend);
    ref_visuals.m_HandleColor = plMath::Lerp(ref_visuals.m_HandleColor, plColor(style.m_HandleColor), fBlend);
    ref_visuals.m_fFontSize = plMath::Lerp(ref_visuals.m_fFontSize, static_cast<float>(style.m_uiFontSize), fBlend);
    ref_visuals.m_vSliceMargins = plMath::Lerp(ref_visuals.m_vSliceMargins, style.m_vSliceMargins, fBlend);
    ref_visuals.m_vCornerRadius = plMath::Lerp(ref_visuals.m_vCornerRadius, style.m_vCornerRadius, fBlend);
    ref_visuals.m_fBorderWidth = plMath::Lerp(ref_visuals.m_fBorderWidth, style.m_fBorderWidth, fBlend);
    ref_visuals.m_BorderColor = plMath::Lerp(ref_visuals.m_BorderColor, plColor(style.m_BorderColor), fBlend);
    ref_visuals.m_GradientColor = plMath::Lerp(ref_visuals.m_GradientColor, plColor(style.m_GradientColor), fBlend);
    ref_visuals.m_fShadowSize = plMath::Lerp(ref_visuals.m_fShadowSize, style.m_fShadowSize, fBlend);
    ref_visuals.m_vShadowOffset = plMath::Lerp(ref_visuals.m_vShadowOffset, style.m_vShadowOffset, fBlend);
    ref_visuals.m_ShadowColor = plMath::Lerp(ref_visuals.m_ShadowColor, plColor(style.m_ShadowColor), fBlend);

    if (fBlend >= 0.5f)
    {
      ref_visuals.m_BrushMode = static_cast<plUiBrushMode::Enum>(style.m_BrushMode.GetValue());
      ref_visuals.m_GradientMode = static_cast<plUiGradientMode::Enum>(style.m_GradientMode.GetValue());

      if (style.m_hTexture.IsValid())
        ref_visuals.m_pTexture = &style.m_hTexture;
      if (style.m_hHoverTexture.IsValid())
        ref_visuals.m_pHoverTexture = &style.m_hHoverTexture;
      if (style.m_hPressedTexture.IsValid())
        ref_visuals.m_pPressedTexture = &style.m_hPressedTexture;
      if (style.m_hDisabledTexture.IsValid())
        ref_visuals.m_pDisabledTexture = &style.m_hDisabledTexture;
      if (style.m_hCheckedTexture.IsValid())
        ref_visuals.m_pCheckedTexture = &style.m_hCheckedTexture;
      if (style.m_hFillTexture.IsValid())
        ref_visuals.m_pFillTexture = &style.m_hFillTexture;
      if (style.m_hHandleTexture.IsValid())
        ref_visuals.m_pHandleTexture = &style.m_hHandleTexture;
      if (style.m_hFont.IsValid())
        ref_visuals.m_hFont = style.m_hFont;
    }
  }

  /// \brief Flattens an element's look: local properties, replaced by its Style asset
  /// ("style replaces visuals"), then the active states' styles cross-faded on top.
  EffectiveVisuals ComposeVisuals(const plUiElement* pElement, const plUiBindingState::ResolvedValues* pResolved, ScopedStyleSet& ref_styles)
  {
    EffectiveVisuals visuals;

    // base: the element's own properties
    if (const plUiVisualElement* pVisual = plDynamicCast<const plUiVisualElement*>(pElement))
    {
      visuals.m_Color = pVisual->m_Color;
      visuals.m_BrushMode = static_cast<plUiBrushMode::Enum>(pVisual->m_BrushMode.GetValue());
      visuals.m_pTexture = &pVisual->m_hTexture;
      visuals.m_vSliceMargins = pVisual->m_vSliceMargins;
      visuals.m_vCornerRadius = pVisual->m_vCornerRadius;
      visuals.m_fBorderWidth = pVisual->m_fBorderWidth;
      visuals.m_BorderColor = pVisual->m_BorderColor;
      visuals.m_GradientMode = static_cast<plUiGradientMode::Enum>(pVisual->m_GradientMode.GetValue());
      visuals.m_GradientColor = pVisual->m_GradientColor;
      visuals.m_fShadowSize = pVisual->m_fShadowSize;
      visuals.m_vShadowOffset = pVisual->m_vShadowOffset;
      visuals.m_ShadowColor = pVisual->m_ShadowColor;
    }

    if (const plUiButton* pButton = plDynamicCast<const plUiButton*>(pElement))
    {
      visuals.m_HoverColor = pButton->m_HoverColor;
      visuals.m_PressedColor = pButton->m_PressedColor;
      visuals.m_pHoverTexture = &pButton->m_hHoverTexture;
      visuals.m_pPressedTexture = &pButton->m_hPressedTexture;
    }

    if (const plUiToggle* pToggle = plDynamicCast<const plUiToggle*>(pElement))
    {
      visuals.m_CheckedColor = pToggle->m_CheckedColor;
      visuals.m_pCheckedTexture = &pToggle->m_hCheckedTexture;
    }

    if (const plUiProgressBar* pProgressBar = plDynamicCast<const plUiProgressBar*>(pElement))
    {
      visuals.m_FillColor = pProgressBar->m_FillColor;
      visuals.m_pFillTexture = &pProgressBar->m_hFillTexture;
    }

    if (const plUiSlider* pSlider = plDynamicCast<const plUiSlider*>(pElement))
    {
      visuals.m_FillColor = pSlider->m_FillColor;
      visuals.m_HandleColor = pSlider->m_HandleColor;
      visuals.m_pHandleTexture = &pSlider->m_hHandleTexture;
    }

    if (const plUiText* pText = plDynamicCast<const plUiText*>(pElement))
    {
      visuals.m_hFont = pText->m_hFont;
      visuals.m_TextColor = pText->m_Color;
      visuals.m_fFontSize = static_cast<float>(pText->m_uiFontSize);
    }

    if (const plUiTextInput* pTextInput = plDynamicCast<const plUiTextInput*>(pElement))
    {
      visuals.m_hFont = pTextInput->m_hFont;
      visuals.m_TextColor = plColor(pTextInput->m_TextColor);
      visuals.m_fFontSize = static_cast<float>(pTextInput->m_uiFontSize);
    }

    // the element's style replaces everything
    if (const plUiStyle* pStyle = ref_styles.Acquire(pElement->m_hStyle))
    {
      AssignStyle(visuals, *pStyle);
    }

    // active states cross-fade their styles on top (weakest first = array order)
    if (pResolved != nullptr && (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasStateBlends))
    {
      for (plUInt32 i = 0; i < pResolved->m_uiStateBlendCount; ++i)
      {
        if (const plUiStyle* pStyle = ref_styles.Acquire(pResolved->m_StateBlends[i].m_pState->m_hStyle))
        {
          LerpToStyle(visuals, *pStyle, pResolved->m_StateBlends[i].m_fBlend);
        }
      }
    }

    return visuals;
  }

  constexpr float kTextInputPadding = 8.0f;

  /// nearest caret CHARACTER index for a click at fLocalX px right of the text start
  plInt32 ComputeTextInputCaretFromX(const plUiTextInput* pElement, plStringView sText, float fLocalX)
  {
    plInt32 iCount = 0;

    if (pElement->m_hFont.IsValid())
    {
      plResourceLock<plUiFontResource> pFont(pElement->m_hFont, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

      if (pFont.GetAcquireResult() == plResourceAcquireResult::Final && pFont->HasGlyphs())
      {
        const float fScale = static_cast<float>(plMath::Max<plUInt32>(4, pElement->m_uiFontSize)) / pFont->GetBaseSize();

        float fPen = 0.0f;
        plUInt32 uiPrevCp = 0;
        plInt32 iChar = 0;

        for (auto it = sText.GetIteratorFront(); it.IsValid(); ++it)
        {
          const plUInt32 uiCp = it.GetCharacter();
          float fAdvance = pFont->GetBaseSize() * 0.25f * fScale;

          if (const plUiFontGlyph* pGlyph = pFont->GetGlyph(uiCp))
          {
            if (uiPrevCp != 0)
            {
              fPen += pFont->GetKerning(uiPrevCp, uiCp) * fScale;
            }

            fAdvance = pGlyph->m_fAdvance * fScale;
          }

          if (fLocalX < fPen + fAdvance * 0.5f)
          {
            return iChar; // clicked the left half of this character
          }

          fPen += fAdvance;
          uiPrevCp = uiCp;
          ++iChar;
        }

        return iChar;
      }
    }

    // no usable font metrics: caret to the end
    for (auto it = sText.GetIteratorFront(); it.IsValid(); ++it)
    {
      ++iCount;
    }

    return iCount;
  }

  /// single-line text + blinking caret of a plUiTextInput, clipped to its rect
  void DrawTextInputContent(const plUiTextInput* pElement, const EffectiveVisuals& visuals, const plRectFloat& rect, float fOpacity, const plColor& tint, plUiDrawContext& ref_ctx)
  {
    const plUiElementInstance thisInstance{pElement, ref_ctx.m_uiInstanceSalt};
    const bool bFocused = ref_ctx.m_FocusedElement == thisInstance;

    // display precedence: live edit (focused) > bound entry > per-instance edit memory > authored
    const plUiTextEditState* pEditState = (ref_ctx.m_pTextEditStates != nullptr) ? ref_ctx.m_pTextEditStates->GetValue(thisInstance) : nullptr;

    plStringView sText;
    bool bFromEdit = false;

    if (bFocused && pEditState != nullptr)
    {
      sText = pEditState->m_sText;
      bFromEdit = true;
    }
    else
    {
      const plUiBindingState::ResolvedValues* pResolved = (ref_ctx.m_pBindings != nullptr) ? ref_ctx.m_pBindings->GetResolved(thisInstance) : nullptr;

      if (!pElement->m_sTextBinding.IsEmpty() && pResolved != nullptr && (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasText))
      {
        sText = pResolved->m_sText;
      }
      else if (pEditState != nullptr)
      {
        sText = pEditState->m_sText; // unbound: the edit memory IS the value
        bFromEdit = true;
      }
      else
      {
        sText = pElement->m_sText;
      }
    }

    plColor textColor = visuals.m_TextColor;

    const bool bPlaceholder = sText.IsEmpty() && !pElement->m_sPlaceholder.IsEmpty();
    if (bPlaceholder)
    {
      sText = pElement->m_sPlaceholder;
      textColor.a *= 0.4f;
    }

    textColor *= tint;
    textColor.a *= fOpacity;

    const plUInt32 uiFontSize = plMath::Max<plUInt32>(4, static_cast<plUInt32>(plMath::Round(visuals.m_fFontSize)));

    if (!visuals.m_hFont.IsValid())
    {
      // no SDF font: debug-text fallback (no caret)
      if (ref_ctx.m_pDebugContext != nullptr && !sText.IsEmpty() && textColor.a > 0.0f)
      {
        const plVec2I32 vPos(static_cast<plInt32>(plMath::Round((rect.x + kTextInputPadding) * ref_ctx.m_fScale + ref_ctx.m_vOffset.x)), static_cast<plInt32>(plMath::Round((rect.y + rect.height * 0.5f) * ref_ctx.m_fScale + ref_ctx.m_vOffset.y)));
        plDebugRenderer::Draw2DText(*ref_ctx.m_pDebugContext, plFmt("{}", sText), vPos, textColor, plMath::Max<plUInt32>(4, static_cast<plUInt32>(plMath::Round(uiFontSize * ref_ctx.m_fScale))), plDebugTextHAlign::Left, plDebugTextVAlign::Center);
      }

      return;
    }

    plResourceLock<plUiFontResource> pFont(visuals.m_hFont, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
    if (pFont.GetAcquireResult() != plResourceAcquireResult::Final || !pFont->HasGlyphs())
      return;

    const float fScale = static_cast<float>(uiFontSize) / pFont->GetBaseSize();
    const float fAtlasWidth = static_cast<float>(pFont->GetAtlasWidth());
    const float fAtlasHeight = static_cast<float>(pFont->GetAtlasHeight());
    const float fBaseline = rect.y + (rect.height - pFont->GetLineHeight() * fScale) * 0.5f + pFont->GetAscent() * fScale;
    const float fStartX = rect.x + kTextInputPadding;

    ref_ctx.PushClip(rect);

    const plColorLinearUB vertexColor = plColorLinearUB(textColor);

    float fPen = 0.0f;
    float fCaretPen = 0.0f;
    plInt32 iChar = 0;
    plUInt32 uiPrevCp = 0;
    const plInt32 iCaret = (pEditState != nullptr) ? pEditState->m_iCaret : 0;
    const bool bCaretInText = bFromEdit && !bPlaceholder;

    if (textColor.a > 0.0f || bFocused)
    {
      for (auto it = sText.GetIteratorFront(); it.IsValid(); ++it)
      {
        if (bCaretInText && iChar == iCaret)
        {
          fCaretPen = fPen;
        }

        const plUInt32 uiCp = it.GetCharacter();

        if (const plUiFontGlyph* pGlyph = pFont->GetGlyph(uiCp))
        {
          if (uiPrevCp != 0)
          {
            fPen += pFont->GetKerning(uiPrevCp, uiCp) * fScale;
          }

          if (pGlyph->m_vAtlasSize.x > 0 && pGlyph->m_vAtlasSize.y > 0 && textColor.a > 0.0f)
          {
            const plRectFloat glyphRect(
              fStartX + fPen + pGlyph->m_vBearing.x * fScale,
              fBaseline - pGlyph->m_vBearing.y * fScale,
              pGlyph->m_vAtlasSize.x * fScale,
              pGlyph->m_vAtlasSize.y * fScale);

            const plVec2 vUvMin(pGlyph->m_vAtlasPos.x / fAtlasWidth, pGlyph->m_vAtlasPos.y / fAtlasHeight);
            const plVec2 vUvMax((pGlyph->m_vAtlasPos.x + pGlyph->m_vAtlasSize.x) / fAtlasWidth, (pGlyph->m_vAtlasPos.y + pGlyph->m_vAtlasSize.y) / fAtlasHeight);

            ref_ctx.AddQuad(glyphRect, vUvMin, vUvMax, vertexColor, nullptr, pFont->GetAtlasTexture(), true);
          }

          fPen += pGlyph->m_fAdvance * fScale;
        }
        else
        {
          fPen += pFont->GetBaseSize() * 0.25f * fScale;
        }

        uiPrevCp = uiCp;
        ++iChar;
      }

      if (bCaretInText && iCaret >= iChar)
      {
        fCaretPen = fPen; // caret behind the last character
      }
    }

    // blinking caret while focused
    if (bFocused)
    {
      const float fBlink = (pEditState != nullptr) ? pEditState->m_fBlink : 0.0f;

      if (plMath::Mod(fBlink, 1.0f) < 0.6f)
      {
        plColor caretColor = plColor(pElement->m_CaretColor);
        caretColor *= tint;
        caretColor.a *= fOpacity;

        if (caretColor.a > 0.0f)
        {
          const float fCaretHeight = pFont->GetLineHeight() * fScale;
          const plRectFloat caretRect(fStartX + fCaretPen, rect.y + (rect.height - fCaretHeight) * 0.5f, 1.5f, fCaretHeight);
          ref_ctx.AddQuad(caretRect, plVec2::MakeZero(), plVec2(1, 1), plColorLinearUB(caretColor), nullptr, plGALTextureHandle(), false);
        }
      }
    }

    ref_ctx.PopClip();
  }

  void DrawVisualElement(const plUiVisualElement* pElement, const EffectiveVisuals& visuals, const plRectFloat& rect, float fOpacity, const plColor& tint, plUiDrawContext& ref_ctx)
  {
    plColor color = visuals.m_Color;
    const plTexture2DResourceHandle* pTexture = visuals.m_pTexture;

    // interactive state visuals
    if (plDynamicCast<const plUiButton*>(pElement) != nullptr)
    {
      const plUiElementInstance thisInstance{pElement, ref_ctx.m_uiInstanceSalt};
      const plUiBindingState::ResolvedValues* pResolved = (ref_ctx.m_pBindings != nullptr) ? ref_ctx.m_pBindings->GetResolved(thisInstance) : nullptr;
      const bool bHasBlends = pResolved != nullptr && (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasStateBlends) != 0;

      // hover/pressed blends belong to the instance that was interacted with; disabled
      // is binding-driven and correctly shared by every instance
      const float fHoverBlend = (bHasBlends && pResolved->m_uiHoverSalt == ref_ctx.m_uiInstanceSalt) ? pResolved->m_fHoverBlend : 0.0f;
      const float fPressedBlend = (bHasBlends && pResolved->m_uiPressedSalt == ref_ctx.m_uiInstanceSalt) ? pResolved->m_fPressedBlend : 0.0f;
      const float fDisabledBlend = bHasBlends ? pResolved->m_fDisabledBlend : 0.0f;

      // toggles: the checked look replaces the normal base (hover/pressed still win on top)
      if (plDynamicCast<const plUiToggle*>(pElement) != nullptr)
      {
        bool bChecked = false;

        if (pResolved != nullptr && (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasChecked))
        {
          bChecked = pResolved->m_bChecked;
        }
        else if (ref_ctx.m_pLocalToggleState != nullptr)
        {
          const bool* pLocal = ref_ctx.m_pLocalToggleState->GetValue(thisInstance);
          bChecked = pLocal != nullptr && *pLocal;
        }

        if (bChecked)
        {
          color = visuals.m_CheckedColor;
          if (visuals.m_pCheckedTexture != nullptr && visuals.m_pCheckedTexture->IsValid())
            pTexture = visuals.m_pCheckedTexture;
        }
      }

      if (fHoverBlend > 0.0f || fPressedBlend > 0.0f || fDisabledBlend > 0.0f)
      {
        // styled: blend between the interaction colors over the style transition
        color = plMath::Lerp(color, visuals.m_HoverColor, fHoverBlend);
        color = plMath::Lerp(color, visuals.m_PressedColor, fPressedBlend);
        color = plMath::Lerp(color, visuals.m_DisabledColor, fDisabledBlend);

        // textures cannot blend: the strongest state wins
        if (fDisabledBlend > 0.5f && visuals.m_pDisabledTexture != nullptr && visuals.m_pDisabledTexture->IsValid())
          pTexture = visuals.m_pDisabledTexture;
        else if (fPressedBlend > 0.5f && visuals.m_pPressedTexture != nullptr && visuals.m_pPressedTexture->IsValid())
          pTexture = visuals.m_pPressedTexture;
        else if (fHoverBlend > 0.5f && visuals.m_pHoverTexture != nullptr && visuals.m_pHoverTexture->IsValid())
          pTexture = visuals.m_pHoverTexture;
      }
      else if (thisInstance == ref_ctx.m_PressedElement)
      {
        // unstyled (or not tracked yet): instant swap
        color = visuals.m_PressedColor;
        if (visuals.m_pPressedTexture != nullptr && visuals.m_pPressedTexture->IsValid())
          pTexture = visuals.m_pPressedTexture;
      }
      else if (thisInstance == ref_ctx.m_HoveredElement)
      {
        color = visuals.m_HoverColor;
        if (visuals.m_pHoverTexture != nullptr && visuals.m_pHoverTexture->IsValid())
          pTexture = visuals.m_pHoverTexture;
      }
    }

    color *= tint;
    color.a *= fOpacity;

    // resolved brush effects (final colors carry tint + opacity)
    BrushFx fx;
    fx.m_vRadii = visuals.m_vCornerRadius;
    fx.m_fBorderWidth = visuals.m_fBorderWidth;
    fx.m_GradientMode = visuals.m_GradientMode;
    fx.m_fShadowSize = visuals.m_fShadowSize;
    fx.m_vShadowOffset = visuals.m_vShadowOffset;
    fx.m_fOpacity = fOpacity;

    {
      plColor borderColor = visuals.m_BorderColor * tint;
      fx.m_BorderColor = plColorLinearUB(borderColor);

      plColor gradientColor = visuals.m_GradientColor * tint;
      gradientColor.a *= fOpacity;
      fx.m_GradientColor = plColorLinearUB(gradientColor);

      plColor shadowColor = visuals.m_ShadowColor * tint;
      shadowColor.a *= fOpacity;
      fx.m_ShadowColor = plColorLinearUB(shadowColor);
    }

    if (const plUiPath* pPath = plDynamicCast<const plUiPath*>(pElement))
    {
      // vector shapes replace the rect brush with their tessellated fill
      if (color.a > 0.0f)
      {
        DrawPathElement(pPath, rect, plColorLinearUB(color), ref_ctx);
      }
    }
    else if (color.a > 0.0f || fx.m_ShadowColor.a > 0 || (fx.m_fBorderWidth > 0.0f && fx.m_BorderColor.a > 0))
    {
      DrawElementBrush(visuals.m_BrushMode, visuals.m_vSliceMargins, pTexture, rect, plColorLinearUB(color), fx, ref_ctx);
    }

    // slider: fill up to the value + a square handle at the value position
    if (const plUiSlider* pSlider = plDynamicCast<const plUiSlider*>(pElement))
    {
      // value precedence: resolved binding > local drag state > authored
      const float fRange = pSlider->m_fMax - pSlider->m_fMin;
      float fT = (plMath::Abs(fRange) > 1e-6f) ? plMath::Clamp((pSlider->m_fValue - pSlider->m_fMin) / fRange, 0.0f, 1.0f) : 0.0f;

      if (ref_ctx.m_pLocalSliderValues != nullptr)
      {
        if (const float* pLocal = ref_ctx.m_pLocalSliderValues->GetValue(plUiElementInstance{pElement, ref_ctx.m_uiInstanceSalt}))
        {
          fT = plMath::Clamp(*pLocal, 0.0f, 1.0f);
        }
      }

      if (ref_ctx.m_pBindings != nullptr)
      {
        if (const plUiBindingState::ResolvedValues* pResolved = ref_ctx.m_pBindings->GetResolved(plUiElementInstance{pElement, ref_ctx.m_uiInstanceSalt}))
        {
          if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasFill)
          {
            fT = pResolved->m_fFill; // already remapped [Min, Max] -> 0..1
          }
        }
      }

      plColor fillColor = visuals.m_FillColor;
      fillColor *= tint;
      fillColor.a *= fOpacity;

      if (fillColor.a > 0.0f && fT > 0.0f)
      {
        const plRectFloat fillRect(rect.x, rect.y, rect.width * fT, rect.height);

        plUiDrawContext::SdfQuadParams sdf;
        sdf.m_vRadii = ClampRadii(visuals.m_vCornerRadius, fillRect);
        sdf.m_fMode = (sdf.m_vRadii != plVec4::MakeZero()) ? 1.0f : 0.0f;

        // the element's gradient also shades the fill (revealed across the FULL widget)
        if (visuals.m_GradientMode != plUiGradientMode::None)
        {
          plColor gradientColor = visuals.m_GradientColor * tint;
          gradientColor.a *= fOpacity;

          plColorLinearUB corners[4];
          ComputeFillGradientCorners(static_cast<plUiGradientMode::Enum>(visuals.m_GradientMode), fillColor, gradientColor, fT, corners);
          ref_ctx.AddQuadColors(fillRect, plVec2::MakeZero(), plVec2(fT, 1), corners, nullptr, plGALTextureHandle(), false, sdf);
        }
        else
        {
          ref_ctx.AddQuad(fillRect, plVec2::MakeZero(), plVec2(fT, 1), plColorLinearUB(fillColor), nullptr, plGALTextureHandle(), false, sdf);
        }
      }

      // handle at the value position, clamped inside the rect (rounds with the element)
      const float fHandleSize = rect.height;
      const float fHandleX = rect.x + fT * plMath::Max(rect.width - fHandleSize, 0.0f);

      plColor handleColor = visuals.m_HandleColor;
      handleColor *= tint;
      handleColor.a *= fOpacity;

      if (handleColor.a > 0.0f)
      {
        const plRectFloat handleRect(fHandleX, rect.y, fHandleSize, fHandleSize);

        plUiDrawContext::SdfQuadParams sdf;
        sdf.m_vRadii = ClampRadii(visuals.m_vCornerRadius, handleRect);
        sdf.m_fMode = (sdf.m_vRadii != plVec4::MakeZero()) ? 1.0f : 0.0f;

        if (visuals.m_pHandleTexture != nullptr && visuals.m_pHandleTexture->IsValid())
        {
          ref_ctx.AddQuad(handleRect, plVec2::MakeZero(), plVec2(1, 1), plColorLinearUB(handleColor), visuals.m_pHandleTexture, plGALTextureHandle(), false, sdf);
        }
        else
        {
          ref_ctx.AddQuad(handleRect, plVec2::MakeZero(), plVec2(1, 1), plColorLinearUB(handleColor), nullptr, plGALTextureHandle(), false, sdf);
        }
      }
    }

    // progress bar fill on top of the background brush
    if (const plUiProgressBar* pProgressBar = plDynamicCast<const plUiProgressBar*>(pElement))
    {
      plColor fillColor = visuals.m_FillColor;
      const plTexture2DResourceHandle* pFillTexture = visuals.m_pFillTexture;

      fillColor *= tint;
      fillColor.a *= fOpacity;

      float fFill = plMath::Clamp(pProgressBar->m_fFillAmount, 0.0f, 1.0f);

      if (ref_ctx.m_pBindings != nullptr)
      {
        if (const plUiBindingState::ResolvedValues* pResolved = ref_ctx.m_pBindings->GetResolved(plUiElementInstance{pElement, ref_ctx.m_uiInstanceSalt}))
        {
          if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasFill)
          {
            fFill = pResolved->m_fFill;
          }

          // animated fill factor (clip Fill tracks) on top of the bound/authored fill
          if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasAnimation)
          {
            fFill = plMath::Clamp(fFill * pResolved->m_fAnimFill, 0.0f, 1.0f);
          }
        }
      }

      if (fillColor.a > 0.0f && fFill > 0.0f)
      {
        const plRectFloat fillRect(rect.x, rect.y, rect.width * fFill, rect.height);

        plUiDrawContext::SdfQuadParams sdf;
        sdf.m_vRadii = ClampRadii(visuals.m_vCornerRadius, fillRect);
        sdf.m_fMode = (sdf.m_vRadii != plVec4::MakeZero()) ? 1.0f : 0.0f;

        if (pFillTexture != nullptr && pFillTexture->IsValid())
        {
          // crop the texture horizontally so the image reveals instead of squashing
          ref_ctx.AddQuad(fillRect, plVec2::MakeZero(), plVec2(fFill, 1), plColorLinearUB(fillColor), pFillTexture, plGALTextureHandle(), false, sdf);
        }
        else if (visuals.m_GradientMode != plUiGradientMode::None)
        {
          // the element's gradient also shades the fill (revealed across the FULL widget,
          // like the texture crop — the bar uncovers the gradient as it fills)
          plColor gradientColor = visuals.m_GradientColor * tint;
          gradientColor.a *= fOpacity;

          plColorLinearUB corners[4];
          ComputeFillGradientCorners(static_cast<plUiGradientMode::Enum>(visuals.m_GradientMode), fillColor, gradientColor, fFill, corners);
          ref_ctx.AddQuadColors(fillRect, plVec2::MakeZero(), plVec2(1, 1), corners, nullptr, plGALTextureHandle(), false, sdf);
        }
        else
        {
          ref_ctx.AddQuad(fillRect, plVec2::MakeZero(), plVec2(1, 1), plColorLinearUB(fillColor), nullptr, plGALTextureHandle(), false, sdf);
        }
      }
    }

    // text input: single-line content + blinking caret over the brush
    if (const plUiTextInput* pTextInput = plDynamicCast<const plUiTextInput*>(pElement))
    {
      DrawTextInputContent(pTextInput, visuals, rect, fOpacity, tint, ref_ctx);
    }

    // gamepad/keyboard focus ring: a bright SDF border just outside the widget, following
    // its rounding — works on every widget without extra style fields
    if (ref_ctx.m_FocusedElement.m_pElement == pElement && ref_ctx.m_FocusedElement.m_uiSalt == ref_ctx.m_uiInstanceSalt && fOpacity > 0.0f)
    {
      constexpr float fRingGap = 2.0f;
      constexpr float fRingWidth = 2.0f;

      const plRectFloat ringRect(rect.x - fRingGap - fRingWidth, rect.y - fRingGap - fRingWidth, rect.width + 2.0f * (fRingGap + fRingWidth), rect.height + 2.0f * (fRingGap + fRingWidth));

      plUiDrawContext::SdfQuadParams ring;
      ring.m_fMode = 2.0f; // border band
      ring.m_vRadii = ClampRadii(visuals.m_vCornerRadius + plVec4(fRingGap + fRingWidth), ringRect);
      ring.m_fBorderWidth = fRingWidth;
      ring.m_ColorB = plColorLinearUB(255, 255, 255, 220);

      const plColorLinearUB opacityCarrier(255, 255, 255, static_cast<plUInt8>(plMath::Clamp(fOpacity, 0.0f, 1.0f) * 255.0f));
      ref_ctx.AddQuad(ringRect, plVec2::MakeZero(), plVec2(1, 1), opacityCarrier, nullptr, plGALTextureHandle(), false, ring);
    }
  }

  void DrawTextElementFallback(const plUiText* pElement, plStringView sText, plUInt32 uiFontSize, const plRectFloat& rect, const plColor& color, plUiDrawContext& ref_ctx)
  {
    if (ref_ctx.m_pDebugContext == nullptr)
      return;

    float fX = rect.x;
    plDebugTextHAlign::Enum hAlign = plDebugTextHAlign::Left;
    if (pElement->m_HAlign == plUiTextHAlign::Center)
    {
      fX = rect.x + rect.width * 0.5f;
      hAlign = plDebugTextHAlign::Center;
    }
    else if (pElement->m_HAlign == plUiTextHAlign::Right)
    {
      fX = rect.x + rect.width;
      hAlign = plDebugTextHAlign::Right;
    }

    float fY = rect.y;
    plDebugTextVAlign::Enum vAlign = plDebugTextVAlign::Top;
    if (pElement->m_VAlign == plUiTextVAlign::Center)
    {
      fY = rect.y + rect.height * 0.5f;
      vAlign = plDebugTextVAlign::Center;
    }
    else if (pElement->m_VAlign == plUiTextVAlign::Bottom)
    {
      fY = rect.y + rect.height;
      vAlign = plDebugTextVAlign::Bottom;
    }

    const plVec2I32 vPos(static_cast<plInt32>(plMath::Round(fX * ref_ctx.m_fScale + ref_ctx.m_vOffset.x)), static_cast<plInt32>(plMath::Round(fY * ref_ctx.m_fScale + ref_ctx.m_vOffset.y)));
    const plUInt32 uiSize = plMath::Max<plUInt32>(4, static_cast<plUInt32>(plMath::Round(uiFontSize * ref_ctx.m_fScale)));

    plDebugRenderer::Draw2DText(*ref_ctx.m_pDebugContext, plFmt("{}", sText), vPos, color, uiSize, hAlign, vAlign);
  }

  void DrawTextElement(const plUiText* pElement, const EffectiveVisuals& visuals, const plRectFloat& rect, float fOpacity, const plColor& tint, plUiDrawContext& ref_ctx)
  {
    // displayed string: binding / localization override resolved in the component update
    plStringView sText = pElement->m_sText;

    if (ref_ctx.m_pBindings != nullptr)
    {
      if (const plUiBindingState::ResolvedValues* pResolved = ref_ctx.m_pBindings->GetResolved(plUiElementInstance{pElement, ref_ctx.m_uiInstanceSalt}))
      {
        if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasText)
        {
          sText = pResolved->m_sText;
        }
      }
    }

    if (sText.IsEmpty())
      return;

    // "style replaces visuals": font, color and size come from the composed look
    const plUiFontResourceHandle& hFont = visuals.m_hFont;
    const plUInt32 uiFontSize = plMath::Max<plUInt32>(4, static_cast<plUInt32>(plMath::Round(visuals.m_fFontSize)));

    plColor color = visuals.m_TextColor;
    color *= tint;
    color.a *= fOpacity;

    if (color.a <= 0.0f)
      return;

    // SDF font path
    if (hFont.IsValid())
    {
      plResourceLock<plUiFontResource> pFont(hFont, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

      if (pFont.GetAcquireResult() == plResourceAcquireResult::Final && pFont->HasGlyphs())
      {
        const float fScale = static_cast<float>(uiFontSize) / pFont->GetBaseSize();
        const float fLineHeight = pFont->GetLineHeight() * fScale;
        const float fAtlasWidth = static_cast<float>(pFont->GetAtlasWidth());
        const float fAtlasHeight = static_cast<float>(pFont->GetAtlasHeight());
        const plColorLinearUB vertexColor = plColorLinearUB(color);

        struct GlyphPos
        {
          PL_DECLARE_POD_TYPE();

          const plUiFontGlyph* m_pGlyph;
          float m_fPenX;
        };

        struct Line
        {
          plHybridArray<GlyphPos, 64> m_Glyphs;
          float m_fWidth = 0.0f;
        };

        plHybridArray<Line, 8> lines;
        lines.ExpandAndGetRef();

        const bool bWrap = pElement->m_bWrap && rect.width > 0.0f;

        // layout pass: place glyphs on lines in font units scaled to the requested size
        {
          plUInt32 uiPrevCp = 0;
          float fPen = 0.0f;
          plInt32 iBreakGlyph = -1; // first glyph index after the last space on the current line

          for (auto it = sText.GetIteratorFront(); it.IsValid(); ++it)
          {
            const plUInt32 uiCp = it.GetCharacter();

            if (uiCp == '\n')
            {
              lines.PeekBack().m_fWidth = fPen;
              lines.ExpandAndGetRef();
              fPen = 0.0f;
              uiPrevCp = 0;
              iBreakGlyph = -1;
              continue;
            }

            if (const plUiFontGlyph* pGlyph = pFont->GetGlyph(uiCp))
            {
              float fKern = (uiPrevCp != 0) ? pFont->GetKerning(uiPrevCp, uiCp) * fScale : 0.0f;

              // wrap: the incoming glyph would exceed the rect — break at the last space if any
              if (bWrap && uiCp != ' ' && !lines.PeekBack().m_Glyphs.IsEmpty() && fPen + fKern + pGlyph->m_fAdvance * fScale > rect.width)
              {
                // note: growing the array first, references resolved after (growth invalidates them)
                lines.ExpandAndGetRef();
                Line& next = lines[lines.GetCount() - 1];
                Line& prev = lines[lines.GetCount() - 2];

                if (iBreakGlyph >= 1)
                {
                  // width up to (excluding) the trailing space
                  prev.m_fWidth = prev.m_Glyphs[iBreakGlyph - 1].m_fPenX;

                  if (static_cast<plUInt32>(iBreakGlyph) < prev.m_Glyphs.GetCount())
                  {
                    // move the started word to the new line, re-based to pen 0
                    const float fBasePen = prev.m_Glyphs[iBreakGlyph].m_fPenX;
                    for (plUInt32 g = iBreakGlyph; g < prev.m_Glyphs.GetCount(); ++g)
                    {
                      GlyphPos& moved = next.m_Glyphs.ExpandAndGetRef();
                      moved = prev.m_Glyphs[g];
                      moved.m_fPenX -= fBasePen;
                    }
                    prev.m_Glyphs.SetCount(iBreakGlyph);
                    fPen -= fBasePen; // uiPrevCp stays: kerning continues within the moved word
                  }
                  else
                  {
                    fPen = 0.0f;
                    uiPrevCp = 0;
                  }
                }
                else
                {
                  // no space on this line: hard-break before this glyph
                  prev.m_fWidth = fPen;
                  fPen = 0.0f;
                  uiPrevCp = 0;
                }

                iBreakGlyph = -1;
                fKern = (uiPrevCp != 0) ? pFont->GetKerning(uiPrevCp, uiCp) * fScale : 0.0f;
              }

              fPen += fKern;

              GlyphPos& gp = lines.PeekBack().m_Glyphs.ExpandAndGetRef();
              gp.m_pGlyph = pGlyph;
              gp.m_fPenX = fPen;

              fPen += pGlyph->m_fAdvance * fScale;

              if (uiCp == ' ')
              {
                iBreakGlyph = static_cast<plInt32>(lines.PeekBack().m_Glyphs.GetCount());
              }
            }
            else
            {
              fPen += pFont->GetBaseSize() * 0.25f * fScale; // missing glyph: advance anyway
            }

            uiPrevCp = uiCp;
          }

          lines.PeekBack().m_fWidth = fPen;
        }

        const bool bEllipsis = pElement->m_Overflow == plUiTextOverflow::Ellipsis;

        // ellipsis: drop lines that don't fit vertically, truncate overflowing lines with "..."
        if (bEllipsis)
        {
          bool bCutVertically = false;

          if (rect.height > 0.0f && lines.GetCount() * fLineHeight > rect.height)
          {
            const plUInt32 uiVisible = plMath::Max<plUInt32>(1, static_cast<plUInt32>(rect.height / fLineHeight));
            if (uiVisible < lines.GetCount())
            {
              lines.SetCount(uiVisible);
              bCutVertically = true;
            }
          }

          if (const plUiFontGlyph* pDot = pFont->GetGlyph('.'))
          {
            const float fDotAdv = pDot->m_fAdvance * fScale;
            const float fEllipsisWidth = 3.0f * fDotAdv;

            for (plUInt32 uiLine = 0; uiLine < lines.GetCount(); ++uiLine)
            {
              Line& line = lines[uiLine];
              const bool bLastCutLine = bCutVertically && uiLine == lines.GetCount() - 1;

              if (!bLastCutLine && (rect.width <= 0.0f || line.m_fWidth <= rect.width))
                continue;

              // trim glyphs until the dots fit
              while (!line.m_Glyphs.IsEmpty() && line.m_Glyphs.PeekBack().m_fPenX + fEllipsisWidth > rect.width)
              {
                line.m_Glyphs.PopBack();
              }

              float fPen = line.m_Glyphs.IsEmpty() ? 0.0f : line.m_Glyphs.PeekBack().m_fPenX + line.m_Glyphs.PeekBack().m_pGlyph->m_fAdvance * fScale;

              for (plUInt32 d = 0; d < 3; ++d)
              {
                GlyphPos& gp = line.m_Glyphs.ExpandAndGetRef();
                gp.m_pGlyph = pDot;
                gp.m_fPenX = fPen;
                fPen += fDotAdv;
              }

              line.m_fWidth = fPen;
            }
          }
        }

        // vertical placement
        const float fTotalHeight = lines.GetCount() * fLineHeight;
        float fTop = rect.y;
        if (pElement->m_VAlign == plUiTextVAlign::Center)
        {
          fTop = rect.y + (rect.height - fTotalHeight) * 0.5f;
        }
        else if (pElement->m_VAlign == plUiTextVAlign::Bottom)
        {
          fTop = rect.y + rect.height - fTotalHeight;
        }

        const bool bClip = pElement->m_Overflow == plUiTextOverflow::Clip;
        if (bClip)
        {
          ref_ctx.PushClip(rect);
        }

        for (plUInt32 uiLine = 0; uiLine < lines.GetCount(); ++uiLine)
        {
          const Line& line = lines[uiLine];

          float fStartX = rect.x;
          if (pElement->m_HAlign == plUiTextHAlign::Center)
          {
            fStartX = rect.x + (rect.width - line.m_fWidth) * 0.5f;
          }
          else if (pElement->m_HAlign == plUiTextHAlign::Right)
          {
            fStartX = rect.x + rect.width - line.m_fWidth;
          }

          const float fBaseline = fTop + uiLine * fLineHeight + pFont->GetAscent() * fScale;

          for (const GlyphPos& gp : line.m_Glyphs)
          {
            const plUiFontGlyph& glyph = *gp.m_pGlyph;

            if (glyph.m_vAtlasSize.x == 0 || glyph.m_vAtlasSize.y == 0)
              continue; // e.g. space

            const plRectFloat glyphRect(
              fStartX + gp.m_fPenX + glyph.m_vBearing.x * fScale,
              fBaseline - glyph.m_vBearing.y * fScale,
              glyph.m_vAtlasSize.x * fScale,
              glyph.m_vAtlasSize.y * fScale);

            const plVec2 vUvMin(glyph.m_vAtlasPos.x / fAtlasWidth, glyph.m_vAtlasPos.y / fAtlasHeight);
            const plVec2 vUvMax((glyph.m_vAtlasPos.x + glyph.m_vAtlasSize.x) / fAtlasWidth, (glyph.m_vAtlasPos.y + glyph.m_vAtlasSize.y) / fAtlasHeight);

            ref_ctx.AddQuad(glyphRect, vUvMin, vUvMax, vertexColor, nullptr, pFont->GetAtlasTexture(), true);
          }
        }

        if (bClip)
        {
          ref_ctx.PopClip();
        }

        return;
      }
    }

    // fallback: debug bitmap font
    DrawTextElementFallback(pElement, sText, uiFontSize, rect, color, ref_ctx);
  }

  /// what the pointer currently touches, resolved by one walk over the drawn tree
  struct plUiHitTestResult
  {
    const plUiElement* m_pInteractive = nullptr; ///< topmost interactive element (buttons, toggles, sliders)
    plUInt32 m_uiInteractiveSalt = 0;            ///< instance salt of the interactive hit
    plRectFloat m_InteractiveRect = plRectFloat(0, 0, 0, 0);
    const plUiItemList* m_pItemList = nullptr; ///< list containing the hit, when inside a list row
    plInt32 m_iItemIndex = -1;                 ///< row index of the hit inside that list
    plUInt32 m_uiDynamicInstanceId = 0;        ///< spawned instance the hit is in (0 = none)
    const plUiElement* m_pDocumentRoot = nullptr;
    plHybridArray<plUiFocusable::RouteEntry, 8> m_Route;

    const plUiScrollView* m_pScrollView = nullptr; ///< topmost scroll view (mouse-wheel routing)
    plUInt32 m_uiScrollViewSalt = 0;
    plRectFloat m_ScrollViewRect = plRectFloat(0, 0, 0, 0);

    plDynamicArray<plUiFocusable>* m_pFocusables = nullptr; ///< optional: the walk also collects the nav graph nodes
  };

  // ————— data-driven item lists: fixed-extent stack flow, virtualized —————

  /// the list's inner rect (its stack padding applied)
  plRectFloat GetListContentRect(const plUiItemList* pList, const plRectFloat& rect)
  {
    return plRectFloat(
      rect.x + pList->m_vPadding.x,
      rect.y + pList->m_vPadding.y,
      plMath::Max(rect.width - pList->m_vPadding.x - pList->m_vPadding.z, 0.0f),
      plMath::Max(rect.height - pList->m_vPadding.y - pList->m_vPadding.w, 0.0f));
  }

  /// total scrollable main-axis extent of the list's content
  float GetListContentExtent(const plUiItemList* pList, plUInt32 uiCount)
  {
    const bool bHorizontal = pList->m_Direction == plUiStackDirection::Horizontal;
    float fExtent = bHorizontal ? (pList->m_vPadding.x + pList->m_vPadding.z) : (pList->m_vPadding.y + pList->m_vPadding.w);

    if (uiCount > 0)
    {
      fExtent += uiCount * pList->m_fItemExtent + (uiCount - 1) * pList->m_fSpacing;
    }

    return fExtent;
  }

  /// rect of item i in canvas space, scroll applied (cross axis stretches)
  plRectFloat ComputeListItemRect(const plUiItemList* pList, const plRectFloat& rect, plUInt32 uiIndex, float fScrollOffset)
  {
    const plRectFloat content = GetListContentRect(pList, rect);
    const float fStep = pList->m_fItemExtent + pList->m_fSpacing;
    const float fPos = uiIndex * fStep - fScrollOffset;

    if (pList->m_Direction == plUiStackDirection::Horizontal)
    {
      return plRectFloat(content.x + fPos, content.y, pList->m_fItemExtent, content.height);
    }

    return plRectFloat(content.x, content.y + fPos, content.width, pList->m_fItemExtent);
  }

  /// layout of one spawned instance under its anchor: stack anchors flow it after the
  /// authored children (fPen = running main-axis position), others use the root's anchors
  plRectFloat ComputeDynamicInstanceRect(const plUiElement* pAnchor, const plRectFloat& anchorRect, const plUiElement* pInstanceRoot, float& ref_fPen)
  {
    if (const plUiStackPanel* pStack = plDynamicCast<const plUiStackPanel*>(pAnchor))
    {
      const bool bHorizontal = pStack->m_Direction == plUiStackDirection::Horizontal;
      const float fSize = plMath::Max(bHorizontal ? pInstanceRoot->m_vOffsets.z : pInstanceRoot->m_vOffsets.w, 0.0f);
      const plVec4& margin = pInstanceRoot->m_vLayoutMargin;

      plRectFloat rect;

      if (bHorizontal)
      {
        rect = plRectFloat(anchorRect.x + pStack->m_vPadding.x + ref_fPen + margin.x, anchorRect.y + pStack->m_vPadding.y + margin.y, fSize,
          plMath::Max(anchorRect.height - pStack->m_vPadding.y - pStack->m_vPadding.w - margin.y - margin.w, 0.0f));
        ref_fPen += margin.x + fSize + margin.z + pStack->m_fSpacing;
      }
      else
      {
        rect = plRectFloat(anchorRect.x + pStack->m_vPadding.x + margin.x, anchorRect.y + pStack->m_vPadding.y + ref_fPen + margin.y,
          plMath::Max(anchorRect.width - pStack->m_vPadding.x - pStack->m_vPadding.z - margin.x - margin.z, 0.0f), fSize);
        ref_fPen += margin.y + fSize + margin.w + pStack->m_fSpacing;
      }

      return rect;
    }

    return pInstanceRoot->ComputeRect(anchorRect);
  }

  /// main-axis position where spawned instances start flowing (after the authored children)
  float ComputeDynamicInstancePenStart(const plUiElement* pAnchor, const plRectFloat& anchorRect)
  {
    if (const plUiStackPanel* pStack = plDynamicCast<const plUiStackPanel*>(pAnchor))
    {
      // extent includes both paddings; the pen is relative to the leading padding
      const bool bHorizontal = pStack->m_Direction == plUiStackDirection::Horizontal;
      const float fExtent = plUiLayout::ComputeStackContentExtent(pStack, anchorRect);
      const float fPadding = bHorizontal ? (pStack->m_vPadding.x + pStack->m_vPadding.z) : (pStack->m_vPadding.y + pStack->m_vPadding.w);

      const float fChildrenExtent = plMath::Max(fExtent - fPadding, 0.0f);
      return (fChildrenExtent > 0.0f) ? fChildrenExtent + pStack->m_fSpacing : 0.0f;
    }

    return 0.0f;
  }

  /// index range [first, end) of items intersecting the list's view (virtualization)
  void GetVisibleItemRange(const plUiItemList* pList, const plRectFloat& rect, float fScrollOffset, plUInt32 uiCount, plUInt32& out_uiFirst, plUInt32& out_uiEnd)
  {
    const plRectFloat content = GetListContentRect(pList, rect);
    const float fStep = plMath::Max(pList->m_fItemExtent + pList->m_fSpacing, 0.001f);
    const float fView = (pList->m_Direction == plUiStackDirection::Horizontal) ? content.width : content.height;

    out_uiFirst = (fScrollOffset > 0.0f) ? static_cast<plUInt32>(fScrollOffset / fStep) : 0;
    out_uiEnd = plMath::Min(uiCount, static_cast<plUInt32>((fScrollOffset + fView) / fStep) + 1);
    out_uiFirst = plMath::Min(out_uiFirst, out_uiEnd);
  }

  /// the clamped scroll offset of a scroll view along its direction (0 when never scrolled)
  float GetScrollOffset(const plHashTable<plUiElementInstance, float>* pScrollOffsets, const plUiScrollView* pScrollView, plUInt32 uiInstanceSalt, const plRectFloat& rect, const plUiBindingState* pBindings)
  {
    if (pScrollOffsets == nullptr)
      return 0.0f;

    const float* pValue = pScrollOffsets->GetValue(plUiElementInstance{pScrollView, uiInstanceSalt});
    if (pValue == nullptr)
      return 0.0f;

    float fContent = 0.0f;

    if (const plUiItemList* pList = plDynamicCast<const plUiItemList*>(pScrollView))
    {
      fContent = GetListContentExtent(pList, (pBindings != nullptr) ? pBindings->GetListItemCount(pList) : 0);
    }
    else
    {
      fContent = plUiLayout::ComputeStackContentExtent(pScrollView, rect);
    }

    const float fView = (pScrollView->m_Direction == plUiStackDirection::Horizontal) ? rect.width : rect.height;

    // clamp at every use: live edits can shrink the content below the stored offset
    return plMath::Clamp(*pValue, 0.0f, plMath::Max(fContent - fView, 0.0f));
  }

  /// shifts the child slots of a scroll view along its direction
  void ApplyScrollOffset(const plUiElement* pElement, const plRectFloat& rect, const plHashTable<plUiElementInstance, float>* pScrollOffsets, plUInt32 uiInstanceSalt, const plUiBindingState* pBindings, plDynamicArray<plRectFloat>& ref_childRects)
  {
    const plUiScrollView* pScrollView = plDynamicCast<const plUiScrollView*>(pElement);

    if (pScrollView == nullptr)
      return;

    const float fOffset = GetScrollOffset(pScrollOffsets, pScrollView, uiInstanceSalt, rect, pBindings);

    if (fOffset == 0.0f)
      return;

    const bool bHorizontal = pScrollView->m_Direction == plUiStackDirection::Horizontal;

    for (plRectFloat& childRect : ref_childRects)
    {
      (bHorizontal ? childRect.x : childRect.y) -= fOffset;
    }
  }

  bool IsInteractiveElement(const plUiElement* pElement)
  {
    return plDynamicCast<const plUiButton*>(pElement) != nullptr || plDynamicCast<const plUiSlider*>(pElement) != nullptr || plDynamicCast<const plUiTextInput*>(pElement) != nullptr;
  }

  plRectFloat IntersectRects(const plRectFloat& a, const plRectFloat& b)
  {
    const float x0 = plMath::Max(a.x, b.x);
    const float y0 = plMath::Max(a.y, b.y);
    const float x1 = plMath::Min(a.x + a.width, b.x + b.width);
    const float y1 = plMath::Min(a.y + a.height, b.y + b.height);
    return plRectFloat(x0, y0, plMath::Max(x1 - x0, 0.0f), plMath::Max(y1 - y0, 0.0f));
  }

  void HitTestInRect(const plUiElement* pElement, const plUiElement* pDocumentRoot, const plRectFloat& elementRect, const plRectFloat& clipRect, const plVec2& vPos, const plUiBindingState* pBindings, const plHashTable<plUiElementInstance, float>* pScrollOffsets, plUInt32 uiInstanceSalt, plUInt32 uiSubDocumentDepth, const plUiItemList* pCurrentList, plInt32 iCurrentItem, const plDynamicArray<plUiDynamicInstance>* pDynamicInstances, plUInt32 uiDynamicInstanceId, plHybridArray<plUiFocusable::RouteEntry, 8>& ref_route, UiTransformStack& ref_transforms, plUiHitTestResult& ref_result)
  {
    bool bVisible = pElement->m_bVisible;

    plRectFloat rect = elementRect;

    if (pBindings != nullptr)
    {
      if (const plUiBindingState::ResolvedValues* pResolved = pBindings->GetResolved(plUiElementInstance{pElement, uiInstanceSalt}))
      {
        if (pResolved->m_bStateInteractionHidden)
          return;

        // hidden elements are not interactive; a state-machine override beats both the
        // authored flag and VisibleBinding
        if (pResolved->m_iFlowVisibility >= 0)
          bVisible = pResolved->m_iFlowVisibility != 0;
        else if (pResolved->m_iStateVisibility >= 0)
          bVisible = pResolved->m_iStateVisibility != 0;
        else if ((pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasVisible) && !pResolved->m_bVisible)
          bVisible = false;

        if (!bVisible)
          return;

        // hit-test where the element is drawn (animated offset + size; scale/rotation are visual-only)
        if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasAnimation)
        {
          rect.x += pResolved->m_vAnimOffset.x - pResolved->m_vAnimSize.x * 0.5f;
          rect.y += pResolved->m_vAnimOffset.y - pResolved->m_vAnimSize.y * 0.5f;
          rect.width = plMath::Max(rect.width + pResolved->m_vAnimSize.x, 0.0f);
          rect.height = plMath::Max(rect.height + pResolved->m_vAnimSize.y, 0.0f);
        }
      }
    }

    if (!bVisible)
      return;

    const bool bHasSubtreeTransform = HasSubtreeTransform(pElement);
    if (bHasSubtreeTransform)
      ref_transforms.PushBack(MakeSubtreeTransform(pElement, rect));

    const plRectFloat transformedRect = GetTransformedRectBounds(ref_transforms, rect);
    const bool bPointInElement = TransformedRectContains(ref_transforms, rect, vPos);

    if (IsInteractiveElement(pElement))
    {
      // disabled elements are not interactive (children keep their own hit-testing);
      // a clipped-away part of the element is not clickable either
      const bool bDisabled = pBindings != nullptr && pBindings->IsDisabled(plUiElementInstance{pElement, uiInstanceSalt});

      if (!bDisabled)
      {
        // nav graph: every visible interactive instance is focusable
        if (ref_result.m_pFocusables != nullptr && transformedRect.width > 0.0f && transformedRect.height > 0.0f)
        {
          const plRectFloat visible = IntersectRects(transformedRect, clipRect);

          if (visible.width > 0.0f && visible.height > 0.0f)
          {
            plUiFocusable& focusable = ref_result.m_pFocusables->ExpandAndGetRef();
            focusable.m_pElement = pElement;
            focusable.m_uiSalt = uiInstanceSalt;
            focusable.m_Rect = transformedRect;
            focusable.m_pDocumentRoot = pDocumentRoot;
            focusable.m_Route = ref_route;
            focusable.m_pItemList = pCurrentList;
            focusable.m_iItemIndex = iCurrentItem;
            focusable.m_uiDynamicInstanceId = uiDynamicInstanceId;
          }
        }

        if (bPointInElement && clipRect.Contains(vPos))
        {
          // later (topmost drawn) elements win
          ref_result.m_pInteractive = pElement;
          ref_result.m_uiInteractiveSalt = uiInstanceSalt;
          ref_result.m_InteractiveRect = transformedRect;
          ref_result.m_pItemList = pCurrentList;
          ref_result.m_iItemIndex = iCurrentItem;
          ref_result.m_uiDynamicInstanceId = uiDynamicInstanceId;
          ref_result.m_pDocumentRoot = pDocumentRoot;
          ref_result.m_Route = ref_route;
        }
      }
    }

    const plUiScrollView* pScrollView = plDynamicCast<const plUiScrollView*>(pElement);

    if (pScrollView != nullptr && bPointInElement && clipRect.Contains(vPos))
    {
      ref_result.m_pScrollView = pScrollView; // topmost scroll view receives the wheel
      ref_result.m_uiScrollViewSalt = uiInstanceSalt;
      ref_result.m_ScrollViewRect = rect; // scrolling uses the pre-transform layout extent
    }

    // user widgets: interaction descends into instanced sub-documents (mirrors the draw
    // walk: the instanced tree draws before this element's children, so children hit last)
    if (const plUiSubDocument* pSubDocument = plDynamicCast<const plUiSubDocument*>(pElement))
    {
      constexpr plUInt32 uiMaxSubDocumentDepth = 8;

      if (pSubDocument->m_hDocument.IsValid() && uiSubDocumentDepth < uiMaxSubDocumentDepth)
      {
        plResourceLock<plUiDocumentResource> pSubResource(pSubDocument->m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

        if (pSubResource.GetAcquireResult() == plResourceAcquireResult::Final && pSubResource->GetRoot() != nullptr)
        {
          const plUInt32 uiSubSalt = plUiElementInstance::CombineSalt(uiInstanceSalt, pSubDocument);
          plUiFocusable::RouteEntry& routeEntry = ref_route.ExpandAndGetRef();
          routeEntry.m_pPlacement = pSubDocument;
          routeEntry.m_pParentDocumentRoot = pDocumentRoot;
          routeEntry.m_uiParentSalt = uiInstanceSalt;
          HitTestInRect(pSubResource->GetRoot(), pSubResource->GetRoot(), pSubResource->GetRoot()->ComputeRect(rect), clipRect, vPos, pBindings, pScrollOffsets, uiSubSalt, uiSubDocumentDepth + 1, pCurrentList, iCurrentItem, pDynamicInstances, uiDynamicInstanceId, ref_route, ref_transforms, ref_result);
          ref_route.PopBack();
        }
      }
    }

    // data-driven lists: descend the visible item instances (mirrors the draw walk)
    if (const plUiItemList* pList = plDynamicCast<const plUiItemList*>(pElement))
    {
      constexpr plUInt32 uiMaxSubDocumentDepth = 8;
      const plUInt32 uiCount = (pBindings != nullptr) ? pBindings->GetListItemCount(pList) : 0;

      if (uiCount > 0 && pList->m_hItemTemplate.IsValid() && uiSubDocumentDepth < uiMaxSubDocumentDepth)
      {
        plResourceLock<plUiDocumentResource> pTemplate(pList->m_hItemTemplate, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

        if (pTemplate.GetAcquireResult() == plResourceAcquireResult::Final && pTemplate->GetRoot() != nullptr)
        {
          const float fScrollOffset = GetScrollOffset(pScrollOffsets, pList, uiInstanceSalt, rect, pBindings);
          const plRectFloat listClip = IntersectRects(clipRect, transformedRect); // lists always clip

          plUInt32 uiFirst = 0, uiEnd = 0;
          GetVisibleItemRange(pList, rect, fScrollOffset, uiCount, uiFirst, uiEnd);

          const plUInt32 uiListSalt = plUiElementInstance::CombineSalt(uiInstanceSalt, pList);

          for (plUInt32 i = uiFirst; i < uiEnd; ++i)
          {
            const plRectFloat itemRect = ComputeListItemRect(pList, rect, i, fScrollOffset);
            const plUInt32 uiItemSalt = plUiElementInstance::CombineSaltIndex(uiListSalt, i);

            HitTestInRect(pTemplate->GetRoot(), pTemplate->GetRoot(), pTemplate->GetRoot()->ComputeRect(itemRect), listClip, vPos, pBindings, pScrollOffsets, uiItemSalt, uiSubDocumentDepth + 1, pList, static_cast<plInt32>(i), pDynamicInstances, uiDynamicInstanceId, ref_route, ref_transforms, ref_result);
          }
        }
      }
    }

    plHybridArray<plRectFloat, 16> childRects;
    plUiLayout::ComputeChildRects(pElement, rect, childRects);
    ApplyScrollOffset(pElement, rect, pScrollOffsets, uiInstanceSalt, pBindings, childRects);

    // clipping containers narrow where their children are clickable (scroll views always clip)
    plRectFloat childClip = clipRect;
    const plUiVisualElement* pVisualForClip = plDynamicCast<const plUiVisualElement*>(pElement);
    if (pScrollView != nullptr || (pVisualForClip != nullptr && pVisualForClip->m_bClipChildren))
    {
      childClip = IntersectRects(clipRect, transformedRect);
    }

    for (plUInt32 i = 0; i < pElement->m_Children.GetCount(); ++i)
    {
      if (pElement->m_Children[i] != nullptr)
      {
        HitTestInRect(pElement->m_Children[i], pDocumentRoot, childRects[i], childClip, vPos, pBindings, pScrollOffsets, uiInstanceSalt, uiSubDocumentDepth, pCurrentList, iCurrentItem, pDynamicInstances, uiDynamicInstanceId, ref_route, ref_transforms, ref_result);
      }
    }

    // dynamically spawned instances anchored here hit-test AFTER the children (they draw on top)
    if (uiInstanceSalt == 0 && pDynamicInstances != nullptr && !pDynamicInstances->IsEmpty() && !pElement->m_sName.IsEmpty() && uiSubDocumentDepth < 8)
    {
      float fPen = ComputeDynamicInstancePenStart(pElement, rect);

      for (const plUiDynamicInstance& instance : *pDynamicInstances)
      {
        if (instance.m_sAnchorName != pElement->m_sName || !instance.m_hDocument.IsValid())
          continue;

        plResourceLock<plUiDocumentResource> pResource(instance.m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

        if (pResource.GetAcquireResult() != plResourceAcquireResult::Final || pResource->GetRoot() == nullptr)
          continue;

        const plRectFloat instanceRect = ComputeDynamicInstanceRect(pElement, rect, pResource->GetRoot(), fPen);

        HitTestInRect(pResource->GetRoot(), pResource->GetRoot(), instanceRect, childClip, vPos, pBindings, pScrollOffsets, instance.m_uiSalt, uiSubDocumentDepth + 1, pCurrentList, iCurrentItem, pDynamicInstances, instance.m_uiId, ref_route, ref_transforms, ref_result);
      }
    }

    if (bHasSubtreeTransform)
      ref_transforms.PopBack();
  }

  void DrawElementInRect(const plUiElement* pElement, const plRectFloat& elementRect, plUiDrawContext& ref_ctx)
  {
    bool bVisible = pElement->m_bVisible;

    plRectFloat rect = elementRect;

    // opacity is deliberately NOT inherited: a transparent container still shows its children
    float fOpacity = pElement->m_fOpacity;
    float fVisualScale = 1.0f;
    plColor tint = plColor::White;

    const plUiBindingState::ResolvedValues* pResolved = (ref_ctx.m_pBindings != nullptr) ? ref_ctx.m_pBindings->GetResolved(plUiElementInstance{pElement, ref_ctx.m_uiInstanceSalt}) : nullptr;

    if (pResolved != nullptr)
    {
      // visibility: state-machine override > VisibleBinding > authored flag
      // (binding/state hiding keeps the layout slot, see plUiLayout)
      if (pResolved->m_iFlowVisibility >= 0)
        bVisible = pResolved->m_iFlowVisibility != 0;
      else if (pResolved->m_iStateVisibility >= 0)
        bVisible = pResolved->m_iStateVisibility != 0;
      else if ((pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasVisible) && !pResolved->m_bVisible)
        bVisible = false;
    }

    if (!bVisible)
      return;

    if (pResolved != nullptr)
    {

      if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasAnimation)
      {
        // offset and size change the rect (children follow); opacity/scale/rotation/tint are per-element
        rect.x += pResolved->m_vAnimOffset.x - pResolved->m_vAnimSize.x * 0.5f;
        rect.y += pResolved->m_vAnimOffset.y - pResolved->m_vAnimSize.y * 0.5f;
        rect.width = plMath::Max(rect.width + pResolved->m_vAnimSize.x, 0.0f);
        rect.height = plMath::Max(rect.height + pResolved->m_vAnimSize.y, 0.0f);
        fOpacity *= pResolved->m_fAnimOpacity;
        fVisualScale *= pResolved->m_fAnimScale;
        tint *= pResolved->m_AnimTint;
      }
    }

    const bool bHasSubtreeTransform = HasSubtreeTransform(pElement);
    if (bHasSubtreeTransform)
      ref_ctx.m_SubtreeTransforms.PushBack(MakeSubtreeTransform(pElement, rect));

    if (fOpacity > 0.0f)
    {
      // visual scale affects only this element's own visuals, around the rect center
      plRectFloat visualRect = rect;

      if (fVisualScale != 1.0f)
      {
        const float fCenterX = rect.x + rect.width * 0.5f;
        const float fCenterY = rect.y + rect.height * 0.5f;
        visualRect.width = rect.width * fVisualScale;
        visualRect.height = rect.height * fVisualScale;
        visualRect.x = fCenterX - visualRect.width * 0.5f;
        visualRect.y = fCenterY - visualRect.height * 0.5f;
      }

      // rotation applies only to this element's own quads, around the visual center
      if (pResolved != nullptr && (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasAnimation) && pResolved->m_fAnimRotation != 0.0f)
      {
        const plAngle angle = plAngle::MakeFromDegree(pResolved->m_fAnimRotation);
        ref_ctx.m_bRotate = true;
        ref_ctx.m_fRotationSin = plMath::Sin(angle);
        ref_ctx.m_fRotationCos = plMath::Cos(angle);
        ref_ctx.m_vRotationPivot.Set(visualRect.x + visualRect.width * 0.5f, visualRect.y + visualRect.height * 0.5f);
      }

      // flatten the look: element style, then active state styles cross-faded on top;
      // the scoped set keeps the style resources acquired until the quads are emitted
      ScopedStyleSet styles;
      const EffectiveVisuals visuals = ComposeVisuals(pElement, pResolved, styles);

      if (const plUiVisualElement* pVisual = plDynamicCast<const plUiVisualElement*>(pElement))
      {
        DrawVisualElement(pVisual, visuals, visualRect, fOpacity, tint, ref_ctx);
      }
      else if (const plUiText* pText = plDynamicCast<const plUiText*>(pElement))
      {
        DrawTextElement(pText, visuals, visualRect, fOpacity, tint, ref_ctx);
      }

      ref_ctx.m_bRotate = false; // children are not rotated
    }

    // user-created widgets: render the referenced document's tree inside this rect
    if (const plUiSubDocument* pSubDocument = plDynamicCast<const plUiSubDocument*>(pElement))
    {
      constexpr plUInt32 uiMaxSubDocumentDepth = 8; // caps accidental self-references

      if (pSubDocument->m_hDocument.IsValid() && ref_ctx.m_uiSubDocumentDepth < uiMaxSubDocumentDepth)
      {
        plResourceLock<plUiDocumentResource> pSubResource(pSubDocument->m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

        if (pSubResource.GetAcquireResult() == plResourceAcquireResult::Final && pSubResource->GetRoot() != nullptr)
        {
          // batches may reference textures/styles inside the sub-tree: the handle in the
          // outer (pinned) tree keeps that resource alive beyond the frame
          const plUInt32 uiPrevSalt = ref_ctx.m_uiInstanceSalt;
          ref_ctx.m_uiInstanceSalt = plUiElementInstance::CombineSalt(uiPrevSalt, pSubDocument);

          ++ref_ctx.m_uiSubDocumentDepth;
          DrawElementInRect(pSubResource->GetRoot(), pSubResource->GetRoot()->ComputeRect(rect), ref_ctx);
          --ref_ctx.m_uiSubDocumentDepth;

          ref_ctx.m_uiInstanceSalt = uiPrevSalt;
        }
      }
    }

    plHybridArray<plRectFloat, 16> childRects;
    plUiLayout::ComputeChildRects(pElement, rect, childRects);
    ApplyScrollOffset(pElement, rect, ref_ctx.m_pScrollOffsets, ref_ctx.m_uiInstanceSalt, ref_ctx.m_pBindings, childRects);

    // clipping containers cut their children (own brush stays unclipped, see ClipChildren);
    // scroll views always clip
    const plUiScrollView* pScrollView = plDynamicCast<const plUiScrollView*>(pElement);
    const plUiVisualElement* pVisualForClip = plDynamicCast<const plUiVisualElement*>(pElement);
    const bool bClipChildren = pScrollView != nullptr || (pVisualForClip != nullptr && pVisualForClip->m_bClipChildren);

    if (bClipChildren)
    {
      ref_ctx.PushClip(rect);
    }

    // data-driven lists: draw the VISIBLE item-template instances (virtualized)
    if (const plUiItemList* pList = plDynamicCast<const plUiItemList*>(pElement))
    {
      constexpr plUInt32 uiMaxSubDocumentDepth = 8;
      const plUInt32 uiCount = (ref_ctx.m_pBindings != nullptr) ? ref_ctx.m_pBindings->GetListItemCount(pList) : 0;

      if (uiCount > 0 && pList->m_hItemTemplate.IsValid() && ref_ctx.m_uiSubDocumentDepth < uiMaxSubDocumentDepth)
      {
        plResourceLock<plUiDocumentResource> pTemplate(pList->m_hItemTemplate, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

        if (pTemplate.GetAcquireResult() == plResourceAcquireResult::Final && pTemplate->GetRoot() != nullptr)
        {
          const float fScrollOffset = GetScrollOffset(ref_ctx.m_pScrollOffsets, pList, ref_ctx.m_uiInstanceSalt, rect, ref_ctx.m_pBindings);

          plUInt32 uiFirst = 0, uiEnd = 0;
          GetVisibleItemRange(pList, rect, fScrollOffset, uiCount, uiFirst, uiEnd);

          const plUInt32 uiPrevSalt = ref_ctx.m_uiInstanceSalt;
          const plUInt32 uiListSalt = plUiElementInstance::CombineSalt(uiPrevSalt, pList);

          ++ref_ctx.m_uiSubDocumentDepth;

          for (plUInt32 i = uiFirst; i < uiEnd; ++i)
          {
            const plRectFloat itemRect = ComputeListItemRect(pList, rect, i, fScrollOffset);

            ref_ctx.m_uiInstanceSalt = plUiElementInstance::CombineSaltIndex(uiListSalt, i);
            DrawElementInRect(pTemplate->GetRoot(), pTemplate->GetRoot()->ComputeRect(itemRect), ref_ctx);
          }

          --ref_ctx.m_uiSubDocumentDepth;
          ref_ctx.m_uiInstanceSalt = uiPrevSalt;
        }
      }
    }

    for (plUInt32 i = 0; i < pElement->m_Children.GetCount(); ++i)
    {
      if (pElement->m_Children[i] != nullptr)
      {
        DrawElementInRect(pElement->m_Children[i], childRects[i], ref_ctx);
      }
    }

    // dynamically spawned instances anchored to this element (canvas document only)
    if (ref_ctx.m_uiInstanceSalt == 0 && ref_ctx.m_pDynamicInstances != nullptr && !ref_ctx.m_pDynamicInstances->IsEmpty() && !pElement->m_sName.IsEmpty() && ref_ctx.m_uiSubDocumentDepth < 8)
    {
      float fPen = ComputeDynamicInstancePenStart(pElement, rect);

      for (const plUiDynamicInstance& instance : *ref_ctx.m_pDynamicInstances)
      {
        if (instance.m_sAnchorName != pElement->m_sName || !instance.m_hDocument.IsValid())
          continue;

        plResourceLock<plUiDocumentResource> pResource(instance.m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);

        if (pResource.GetAcquireResult() != plResourceAcquireResult::Final || pResource->GetRoot() == nullptr)
          continue;

        const plRectFloat instanceRect = ComputeDynamicInstanceRect(pElement, rect, pResource->GetRoot(), fPen);

        const plUInt32 uiPrevSalt = ref_ctx.m_uiInstanceSalt;
        ref_ctx.m_uiInstanceSalt = instance.m_uiSalt;

        ++ref_ctx.m_uiSubDocumentDepth;
        DrawElementInRect(pResource->GetRoot(), instanceRect, ref_ctx);
        --ref_ctx.m_uiSubDocumentDepth;

        ref_ctx.m_uiInstanceSalt = uiPrevSalt;
      }
    }

    // minimal scrollbar while the content overflows (track + proportional thumb)
    if (pScrollView != nullptr && pScrollView->m_bShowScrollbar)
    {
      const plUiItemList* pAsList = plDynamicCast<const plUiItemList*>(pScrollView);
      const float fContent = (pAsList != nullptr && ref_ctx.m_pBindings != nullptr)
                               ? GetListContentExtent(pAsList, ref_ctx.m_pBindings->GetListItemCount(pAsList))
                               : plUiLayout::ComputeStackContentExtent(pScrollView, rect);
      const bool bHorizontal = pScrollView->m_Direction == plUiStackDirection::Horizontal;
      const float fView = bHorizontal ? rect.width : rect.height;

      if (fContent > fView && fView > 0.0f)
      {
        const float fOffset = GetScrollOffset(ref_ctx.m_pScrollOffsets, pScrollView, ref_ctx.m_uiInstanceSalt, rect, ref_ctx.m_pBindings);
        const float fThumbSize = plMath::Max(fView * (fView / fContent), 20.0f);
        const float fThumbPos = (fContent - fView > 0.0f) ? (fOffset / (fContent - fView)) * (fView - fThumbSize) : 0.0f;

        constexpr float fBarThickness = 4.0f;
        const plColorLinearUB trackColor(255, 255, 255, 28);
        const plColorLinearUB thumbColor(255, 255, 255, 110);

        // pill-shaped track + thumb
        plUiDrawContext::SdfQuadParams sdf;
        sdf.m_fMode = 1.0f;
        sdf.m_vRadii = plVec4(fBarThickness * 0.5f);

        if (bHorizontal)
        {
          const float fY = rect.y + rect.height - fBarThickness - 2.0f;
          ref_ctx.AddQuad(plRectFloat(rect.x, fY, rect.width, fBarThickness), plVec2::MakeZero(), plVec2(1, 1), trackColor, nullptr, plGALTextureHandle(), false, sdf);
          ref_ctx.AddQuad(plRectFloat(rect.x + fThumbPos, fY, fThumbSize, fBarThickness), plVec2::MakeZero(), plVec2(1, 1), thumbColor, nullptr, plGALTextureHandle(), false, sdf);
        }
        else
        {
          const float fX = rect.x + rect.width - fBarThickness - 2.0f;
          ref_ctx.AddQuad(plRectFloat(fX, rect.y, fBarThickness, rect.height), plVec2::MakeZero(), plVec2(1, 1), trackColor, nullptr, plGALTextureHandle(), false, sdf);
          ref_ctx.AddQuad(plRectFloat(fX, rect.y + fThumbPos, fBarThickness, fThumbSize), plVec2::MakeZero(), plVec2(1, 1), thumbColor, nullptr, plGALTextureHandle(), false, sdf);
        }
      }
    }

    if (bClipChildren)
    {
      ref_ctx.PopClip();
    }

    if (bHasSubtreeTransform)
      ref_ctx.m_SubtreeTransforms.PopBack();
  }
} // namespace

plUiCanvasComponent::plUiCanvasComponent()
{
  m_pLocalBlackboard = plBlackboard::Create();
  m_pLocalBlackboard->SetName("UiCanvasLocal");
}
plUiCanvasComponent::~plUiCanvasComponent() = default;

void plUiCanvasComponent::OnActivated()
{
  SUPER::OnActivated();

  m_bBindingBoardsDirty = true;
}

void plUiCanvasComponent::ResetInteractionState()
{
  m_pCachedRoot = nullptr;
  m_HoveredElement.Invalidate();
  m_PressedElement.Invalidate();
  m_FocusedElement.Invalidate();
  m_pPressedDocumentRoot = nullptr;
  m_pFocusedDocumentRoot = nullptr;
  m_PressedRoute.Clear();
  m_FocusedRoute.Clear();
  m_PressedRect = plRectFloat(0, 0, 0, 0);
  m_ScrollOffsets.Clear();
  m_LocalToggleState.Clear();
  m_LocalSliderValues.Clear();
  m_TextEditStates.Clear();
  m_QueuedNavActions.Clear();
  m_QueuedCharacters.Clear();
  m_QueuedEditKeys.Clear();
  m_pPressedItemList = nullptr;
  m_iPressedItemIndex = -1;
  m_pFocusedItemList = nullptr;
  m_iFocusedItemIndex = -1;
  m_uiPressedUiInstanceId = 0;
  m_uiFocusedUiInstanceId = 0;
}

void plUiCanvasComponent::RegisterBlackboard(const plSharedPtr<plBlackboard>& pBlackboard)
{
  if (pBlackboard == nullptr || m_RegisteredBlackboards.Contains(pBlackboard))
    return;

  m_RegisteredBlackboards.PushBack(pBlackboard);
  m_bBindingBoardsDirty = true;
}

void plUiCanvasComponent::UnregisterBlackboard(const plBlackboard* pBlackboard)
{
  for (plUInt32 i = 0; i < m_RegisteredBlackboards.GetCount(); ++i)
  {
    if (m_RegisteredBlackboards[i].Borrow() == pBlackboard)
    {
      m_RegisteredBlackboards.RemoveAtAndCopy(i);
      m_bBindingBoardsDirty = true;
      return;
    }
  }
}

void plUiCanvasComponent::SetAutobindBlackboards(bool bAutobind)
{
  if (m_bAutobindBlackboards == bAutobind)
    return;

  m_bAutobindBlackboards = bAutobind;
  m_bBindingBoardsDirty = true;
}

void plUiCanvasComponent::UpdateBindingBoards()
{
  m_bBindingBoardsDirty = false;

  plHybridArray<plSharedPtr<plBlackboard>, 4> boards;

  // explicitly registered boards take priority
  for (const auto& pBoard : m_RegisteredBlackboards)
  {
    boards.PushBack(pBoard);
  }

  if (m_bAutobindBlackboards)
  {
    // RmlUi autobind pattern: walk the owner and its parents for blackboard components
    plHybridArray<plBlackboardComponent*, 4> blackboardComponents;

    plGameObject* pObject = GetOwner();
    while (pObject != nullptr)
    {
      pObject->TryGetComponentsOfBaseType(blackboardComponents);

      for (plBlackboardComponent* pBlackboardComponent : blackboardComponents)
      {
        pBlackboardComponent->EnsureInitialized();

        if (pBlackboardComponent->GetBoard() != nullptr && !boards.Contains(pBlackboardComponent->GetBoard()))
        {
          boards.PushBack(pBlackboardComponent->GetBoard());
        }
      }

      pObject = pObject->GetParent();
    }
  }

  if (m_pLocalBlackboard != nullptr && !boards.Contains(m_pLocalBlackboard))
  {
    boards.PushBack(m_pLocalBlackboard);
  }

  m_BindingState.SetBlackboards(boards);
}

plUInt32 plUiCanvasComponent::CreateUiInstance(plStringView sAnchorElementName, const plUiDocumentResourceHandle& hDocument, const plSharedPtr<plBlackboard>& pInstanceBoard /*= nullptr*/)
{
  if (sAnchorElementName.IsEmpty() || !hDocument.IsValid())
    return 0;

  const plUInt32 uiId = m_uiNextUiInstanceId++;

  plUiDynamicInstance& instance = m_DynamicInstances.ExpandAndGetRef();
  instance.m_uiId = uiId;
  // salt from the ID (not the array index): stable when earlier instances are destroyed
  instance.m_uiSalt = plHashingUtils::xxHash32(&uiId, sizeof(uiId), 0xD41AC0DE);
  instance.m_sAnchorName = sAnchorElementName;
  instance.m_hDocument = hDocument;

  m_BindingState.RegisterDynamicInstance(uiId, instance.m_uiSalt, hDocument, pInstanceBoard);

  return uiId;
}

void plUiCanvasComponent::DestroyUiInstance(plUInt32 uiId)
{
  for (plUInt32 i = 0; i < m_DynamicInstances.GetCount(); ++i)
  {
    if (m_DynamicInstances[i].m_uiId != uiId)
      continue;

    m_BindingState.UnregisterDynamicInstance(uiId);
    m_DynamicInstances.RemoveAtAndCopy(i);
    return;
  }
}

void plUiCanvasComponent::ClearUiInstances(plStringView sAnchorElementName /*= {}*/)
{
  for (plUInt32 i = m_DynamicInstances.GetCount(); i-- > 0;)
  {
    if (!sAnchorElementName.IsEmpty() && m_DynamicInstances[i].m_sAnchorName != sAnchorElementName)
      continue;

    m_BindingState.UnregisterDynamicInstance(m_DynamicInstances[i].m_uiId);
    m_DynamicInstances.RemoveAtAndCopy(i);
  }
}

void plUiCanvasComponent::SetInputInjectionEnabled(bool bEnabled)
{
  if (m_bInputInjectionEnabled == bEnabled)
    return;

  m_bInputInjectionEnabled = bEnabled;
  m_bInjectedButtonDown = false;
  m_bPrevInjectedButtonDown = false;
  m_HoveredElement.Invalidate();
  m_PressedElement.Invalidate();
  m_FocusedElement.Invalidate();
  m_pPressedDocumentRoot = nullptr;
  m_pFocusedDocumentRoot = nullptr;
  m_PressedRoute.Clear();
  m_FocusedRoute.Clear();
  m_QueuedNavActions.Clear();
}

void plUiCanvasComponent::InjectNavAction(plUiNavAction::Enum action)
{
  if (m_QueuedNavActions.GetCount() < 8) // a frame's worth; drops runaway repeats
  {
    m_QueuedNavActions.PushBack(static_cast<plUInt8>(action));
  }
}

void plUiCanvasComponent::InjectCharacter(plUInt32 uiCodepoint)
{
  if (uiCodepoint >= 32 && m_QueuedCharacters.GetCount() < 32)
  {
    m_QueuedCharacters.PushBack(uiCodepoint);
  }
}

void plUiCanvasComponent::InjectEditKey(plUiEditKey::Enum key)
{
  if (m_QueuedEditKeys.GetCount() < 16)
  {
    m_QueuedEditKeys.PushBack(static_cast<plUInt8>(key));
  }
}

void plUiCanvasComponent::InjectMouseState(const plVec2& vNormalizedPos, bool bButtonDown, float fWheelTicks /*= 0.0f*/)
{
  m_vInjectedMousePos = vNormalizedPos;
  m_bInjectedButtonDown = bButtonDown;
  m_fInjectedWheelTicks += fWheelTicks; // accumulate: several wheel events may arrive per frame
}

void plUiCanvasComponent::ComputeCanvasTransform(const plVec2& vViewportSize, float& out_fScale, plVec2& out_vOffset, plVec2& out_vCanvasSize) const
{
  if (m_PreviewView.m_vResolution.x > 0 && m_PreviewView.m_vResolution.y > 0)
  {
    // simulate a fixed device resolution, letterboxed into the viewport
    out_vCanvasSize.Set(static_cast<float>(m_PreviewView.m_vResolution.x), static_cast<float>(m_PreviewView.m_vResolution.y));

    const float fFitScale = plMath::Min(vViewportSize.x / out_vCanvasSize.x, vViewportSize.y / out_vCanvasSize.y);
    out_fScale = m_PreviewView.m_fZoom > 0.0f ? m_PreviewView.m_fZoom : fFitScale;

    out_vOffset = (vViewportSize - out_vCanvasSize * out_fScale) * 0.5f + m_PreviewView.m_vPan;
    return;
  }

  out_fScale = 1.0f;
  if (m_ScaleMode == plUiCanvasScaleMode::ScaleWithHeight && m_uiReferenceHeight > 0)
  {
    out_fScale = vViewportSize.y / static_cast<float>(m_uiReferenceHeight);
  }

  out_vOffset = plVec2::MakeZero();
  out_vCanvasSize = vViewportSize / out_fScale;
}

void plUiCanvasComponent::PublishInteractionState()
{
  if (!m_bPublishInteractionState)
    return;

  plBlackboard* pBoard = m_BindingState.GetPrimaryBoard();
  if (pBoard == nullptr)
    return;

  static const plHashedString s_sHovered = plMakeHashedString("Ui_Hovered");
  static const plHashedString s_sFocused = plMakeHashedString("Ui_Focused");
  static const plHashedString s_sActive = plMakeHashedString("Ui_Active");

  const plUiElement* pHover = m_HoveredElement.m_pElement;
  const plUiElement* pFocus = m_FocusedElement.m_pElement;
  const plUiElement* pActive = pFocus != nullptr ? pFocus : pHover;

  auto writeIfChanged = [&](const plHashedString& sEntry, const plUiElement* pElement, plString& ref_sLast) {
    const plStringView sName = pElement != nullptr ? pElement->m_sName.GetView() : plStringView();

    if (ref_sLast == sName)
      return;

    ref_sLast = sName;
    pBoard->SetEntryValue(sEntry, plVariant(ref_sLast));
  };

  writeIfChanged(s_sHovered, pHover, m_sLastPublishedHover);
  writeIfChanged(s_sFocused, pFocus, m_sLastPublishedFocus);
  writeIfChanged(s_sActive, pActive, m_sLastPublishedActive);
}

void plUiCanvasComponent::Update()
{
  if (!m_hDocument.IsValid())
  {
    ResetInteractionState();
    m_BindingState.Clear();
    return;
  }

  plResourceLock<plUiDocumentResource> pDocumentForBindings(m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
  if (pDocumentForBindings.GetAcquireResult() != plResourceAcquireResult::Final || pDocumentForBindings->GetRoot() == nullptr)
  {
    ResetInteractionState();
    m_BindingState.Clear();
    return;
  }

  const plUInt32 uiDocumentVersion = pDocumentForBindings->GetCurrentResourceChangeCounter();
  if (m_pLocalStateRoot != pDocumentForBindings->GetRoot() || m_uiLocalStateDocumentVersion != uiDocumentVersion)
  {
    m_pLocalBlackboard->RemoveAllEntries();
    m_pLocalStateRoot = pDocumentForBindings->GetRoot();
    m_uiLocalStateDocumentVersion = uiDocumentVersion;
    m_bBindingBoardsDirty = true;
  }

  const plTime now = plTime::Now();
  const plTime deltaTime = m_LastUpdateTime.IsZero() ? plTime::MakeZero() : plMath::Min(now - m_LastUpdateTime, plTime::MakeFromMilliseconds(100));
  m_LastUpdateTime = now;

  // ————— data bindings + clips + state blends: always resolved (also in the non-simulated editor preview) —————
  {
    m_BindingState.SetDocument(pDocumentForBindings->GetRoot(), pDocumentForBindings->GetClips(), uiDocumentVersion);

    // a reloaded sub-document strands every element-instance pointer into its old tree —
    // drop them BEFORE they are passed anywhere (same stale-pointer class as the root)
    if (m_BindingState.RefreshSubDocuments())
    {
      ResetInteractionState();
    }

    if (m_bBindingBoardsDirty)
    {
      UpdateBindingBoards();
    }

    // publish hover/focus names to the blackboard BEFORE resolving, so flow states
    // ("Ui_Active == 'Start'") react in the same frame
    PublishInteractionState();

    // interaction blends use last frame's hover/press; the focused element counts as
    // hovered so styled widgets light up under gamepad/keyboard navigation
    m_BindingState.Update(deltaTime, GetEffectiveHover(), m_PressedElement);
  }

  // ————— pointer interaction: only while simulating (or with editor input injection) —————
  if (!m_bInputInjectionEnabled && !GetWorld()->GetWorldSimulationEnabled())
  {
    ResetInteractionState();
    return;
  }

  const plResourceLock<plUiDocumentResource>& pDocument = pDocumentForBindings;

  // element pointers become stale when the resource reloads (the change counter is
  // load-bearing: a reloaded tree can reuse the previous root allocation address)
  if (pDocument->GetRoot() != m_pCachedRoot || pDocument->GetCurrentResourceChangeCounter() != m_uiCachedDocumentVersion)
  {
    ResetInteractionState();
    m_pCachedRoot = pDocument->GetRoot();
    m_uiCachedDocumentVersion = pDocument->GetCurrentResourceChangeCounter();
  }

  plView* pView = plRenderWorld::GetViewByUsageHint(plCameraUsageHint::MainView, plCameraUsageHint::EditorView, GetWorld());
  if (pView == nullptr)
    return;

  const float fViewWidth = pView->GetViewport().width;
  const float fViewHeight = pView->GetViewport().height;
  if (fViewWidth <= 0.0f || fViewHeight <= 0.0f)
    return;

  float fScale = 1.0f;
  plVec2 vOffset = plVec2::MakeZero();
  plVec2 vCanvasSize = plVec2::MakeZero();
  ComputeCanvasTransform(plVec2(fViewWidth, fViewHeight), fScale, vOffset, vCanvasSize);

  // pointer state: injected (editor interact mode) or from the input manager (game)
  plVec2 vMousePos = plVec2::MakeZero();
  plKeyState::Enum buttonState = plKeyState::Up;
  float fWheelTicks = 0.0f;

  if (m_bInputInjectionEnabled)
  {
    vMousePos = m_vInjectedMousePos;

    if (m_bInjectedButtonDown)
    {
      buttonState = m_bPrevInjectedButtonDown ? plKeyState::Down : plKeyState::Pressed;
    }
    else
    {
      buttonState = m_bPrevInjectedButtonDown ? plKeyState::Released : plKeyState::Up;
    }

    m_bPrevInjectedButtonDown = m_bInjectedButtonDown;

    fWheelTicks = m_fInjectedWheelTicks;
    m_fInjectedWheelTicks = 0.0f;
  }
  else
  {
    plInputManager::GetInputSlotState(plInputSlot_MousePositionX, &vMousePos.x);
    plInputManager::GetInputSlotState(plInputSlot_MousePositionY, &vMousePos.y);

    float fButtonValue = 0.0f;
    buttonState = plInputManager::GetInputSlotState(plInputSlot_MouseButton0, &fButtonValue);

    // wheel slots report per-frame ticks (the DearImgui pattern)
    if (plInputManager::GetInputSlotState(plInputSlot_MouseWheelUp) == plKeyState::Pressed)
    {
      fWheelTicks += 1.0f;
    }
    if (plInputManager::GetInputSlotState(plInputSlot_MouseWheelDown) == plKeyState::Pressed)
    {
      fWheelTicks -= 1.0f;
    }
  }

  const plVec2 vRefPos((vMousePos.x * fViewWidth - vOffset.x) / fScale, (vMousePos.y * fViewHeight - vOffset.y) / fScale);
  const plRectFloat canvasRect(0.0f, 0.0f, vCanvasSize.x, vCanvasSize.y);

  plHybridArray<plUiFocusable, 32> focusables;

  plUiHitTestResult hit;
  hit.m_pFocusables = &focusables;
  UiTransformStack transforms;
  plHybridArray<plUiFocusable::RouteEntry, 8> route;
  HitTestInRect(pDocument->GetRoot(), pDocument->GetRoot(), pDocument->GetRoot()->ComputeRect(canvasRect), canvasRect, vRefPos, &m_BindingState, &m_ScrollOffsets, 0, 0, nullptr, -1, &m_DynamicInstances, 0, route, transforms, hit);

  const plUiElementInstance hitInstance{hit.m_pInteractive, hit.m_uiInteractiveSalt};
  m_HoveredElement = hitInstance;

  // focus must reference something that is still focusable (elements hide, disable, unload)
  if (m_FocusedElement.IsValid())
  {
    bool bStillFocusable = false;

    for (const plUiFocusable& focusable : focusables)
    {
      if (focusable.m_pElement == m_FocusedElement.m_pElement && focusable.m_uiSalt == m_FocusedElement.m_uiSalt)
      {
        bStillFocusable = true;
        break;
      }
    }

    if (!bStillFocusable)
    {
      m_FocusedElement.Invalidate();
      m_pFocusedDocumentRoot = nullptr;
      m_FocusedRoute.Clear();
      m_pFocusedItemList = nullptr;
      m_iFocusedItemIndex = -1;
    }
  }

  // ————— gamepad/keyboard navigation (the "UiNav" action set, or injected in interact mode) —————
  {
    plHybridArray<plUInt8, 8> navActions;

    if (m_bInputInjectionEnabled)
    {
      navActions = m_QueuedNavActions;
      m_QueuedNavActions.Clear();
    }
    else
    {
      auto pollAction = [&](const char* szAction, plUiNavAction::Enum action) {
        if (plInputManager::GetInputActionState("UiNav", szAction) == plKeyState::Pressed)
        {
          navActions.PushBack(static_cast<plUInt8>(action));
        }
      };

      pollAction("Up", plUiNavAction::Up);
      pollAction("Down", plUiNavAction::Down);
      pollAction("Left", plUiNavAction::Left);
      pollAction("Right", plUiNavAction::Right);
      pollAction("Accept", plUiNavAction::Accept);
      pollAction("Cancel", plUiNavAction::Cancel);
    }

    for (plUInt8 uiAction : navActions)
    {
      HandleNavAction(static_cast<plUiNavAction::Enum>(uiAction), focusables);
    }
  }

  // typed characters + editing keys route to the focused text input
  UpdateTextInputEditing(static_cast<float>(deltaTime.GetSeconds()));

  // mouse wheel scrolls the topmost scroll view under the cursor
  if (fWheelTicks != 0.0f && hit.m_pScrollView != nullptr)
  {
    const plUiItemList* pAsList = plDynamicCast<const plUiItemList*>(hit.m_pScrollView);
    const float fContent = (pAsList != nullptr)
                             ? GetListContentExtent(pAsList, m_BindingState.GetListItemCount(pAsList))
                             : plUiLayout::ComputeStackContentExtent(hit.m_pScrollView, hit.m_ScrollViewRect);
    const float fView = (hit.m_pScrollView->m_Direction == plUiStackDirection::Horizontal) ? hit.m_ScrollViewRect.width : hit.m_ScrollViewRect.height;

    float& fOffset = m_ScrollOffsets[plUiElementInstance{hit.m_pScrollView, hit.m_uiScrollViewSalt}]; // inserts 0 on first scroll
    fOffset = plMath::Clamp(fOffset - fWheelTicks * hit.m_pScrollView->m_fScrollSpeed, 0.0f, plMath::Max(fContent - fView, 0.0f));
  }

  // slider drag: every frame while pressed, using the rect captured at press time
  // (the drag keeps working when the cursor leaves the widget)
  if (const plUiSlider* pDraggedSlider = plDynamicCast<const plUiSlider*>(m_PressedElement.m_pElement))
  {
    if (buttonState == plKeyState::Down || buttonState == plKeyState::Pressed)
    {
      const float fT = plMath::Clamp((vRefPos.x - m_PressedRect.x) / plMath::Max(m_PressedRect.width, 1.0f), 0.0f, 1.0f);

      if (!pDraggedSlider->m_sValueBinding.IsEmpty())
      {
        const plTempHashedString sEntry(pDraggedSlider->m_sValueBinding.GetData());

        plBlackboard* pBoard = m_BindingState.FindBoardForEntry(sEntry);
        if (pBoard == nullptr)
        {
          pBoard = m_BindingState.GetPrimaryBoard(); // create the entry on the first board
        }

        if (pBoard != nullptr)
        {
          pBoard->SetEntryValue(pDraggedSlider->m_sValueBinding, pDraggedSlider->m_fMin + fT * (pDraggedSlider->m_fMax - pDraggedSlider->m_fMin));
        }
      }
      else
      {
        m_LocalSliderValues[m_PressedElement] = fT;
      }
    }
  }

  if (buttonState == plKeyState::Pressed)
  {
    m_PressedElement = hitInstance;
    m_pPressedDocumentRoot = hit.m_pDocumentRoot;
    m_PressedRoute = hit.m_Route;
    m_PressedRect = hit.m_InteractiveRect; // drags survive the cursor leaving the widget
    m_FocusedElement = hitInstance;        // pointer and nav share one focus (clicking empty space clears it)
    m_pFocusedDocumentRoot = hit.m_pDocumentRoot;
    m_FocusedRoute = hit.m_Route;
    m_pPressedItemList = hit.m_pItemList;
    m_iPressedItemIndex = hit.m_iItemIndex;
    m_pFocusedItemList = hit.m_pItemList;
    m_iFocusedItemIndex = hit.m_iItemIndex;
    m_uiPressedUiInstanceId = hit.m_uiDynamicInstanceId;
    m_uiFocusedUiInstanceId = hit.m_uiDynamicInstanceId;

    // pressing anywhere inside a list row selects it (no button required)
    WriteListSelection(hit.m_pItemList, hit.m_iItemIndex);

    // clicking a text input starts editing with the caret at the click position
    if (const plUiTextInput* pTextInput = plDynamicCast<const plUiTextInput*>(hitInstance.m_pElement))
    {
      plUiTextEditState& editState = BeginTextEdit(pTextInput, hitInstance);
      editState.m_iCaret = ComputeTextInputCaretFromX(pTextInput, editState.m_sText, vRefPos.x - hit.m_InteractiveRect.x - kTextInputPadding);
      editState.m_fBlink = 0.0f;
    }
  }
  else if (buttonState == plKeyState::Released)
  {
    const plUiButton* pPressedButton = plDynamicCast<const plUiButton*>(m_PressedElement.m_pElement);

    if (pPressedButton != nullptr && m_PressedElement == hitInstance)
    {
      TriggerClick(pPressedButton, m_PressedElement, m_pPressedDocumentRoot, m_PressedRoute, m_iPressedItemIndex, m_pPressedItemList, m_uiPressedUiInstanceId);
    }

    m_PressedElement.Invalidate();
    m_pPressedDocumentRoot = nullptr;
    m_PressedRoute.Clear();
    m_pPressedItemList = nullptr;
    m_iPressedItemIndex = -1;
    m_uiPressedUiInstanceId = 0;
  }
  else if (buttonState == plKeyState::Up)
  {
    m_PressedElement.Invalidate();
    m_pPressedDocumentRoot = nullptr;
    m_PressedRoute.Clear();
    m_pPressedItemList = nullptr;
    m_iPressedItemIndex = -1;
    m_uiPressedUiInstanceId = 0;
  }
}

void plUiCanvasComponent::WriteListSelection(const plUiItemList* pItemList, plInt32 iItemIndex)
{
  if (pItemList == nullptr || iItemIndex < 0 || pItemList->m_sSelectedBinding.IsEmpty())
    return;

  const plTempHashedString sEntry(pItemList->m_sSelectedBinding.GetData());

  plBlackboard* pBoard = m_BindingState.FindBoardForEntry(sEntry);
  if (pBoard == nullptr)
  {
    pBoard = m_BindingState.GetPrimaryBoard(); // create the entry on the first board
  }

  if (pBoard != nullptr)
  {
    pBoard->SetEntryValue(pItemList->m_sSelectedBinding, iItemIndex);
  }
}

bool plUiCanvasComponent::ExecuteEventReactions(plStringView sEvent, plArrayPtr<const plUiFocusable::RouteEntry> route)
{
  auto findElementByName = [](const plUiElement* pRoot, plStringView sName) -> const plUiElement* {
    std::function<const plUiElement*(const plUiElement*)> findRecursive;
    findRecursive = [&](const plUiElement* pElement) -> const plUiElement* {
      if (pElement == nullptr)
        return nullptr;
      if (pElement->m_sName == sName)
        return pElement;

      for (const plUiElement* pChild : pElement->m_Children)
      {
        if (const plUiElement* pFound = findRecursive(pChild))
          return pFound;
      }
      return nullptr;
    };

    return sName.IsEmpty() ? nullptr : findRecursive(pRoot);
  };

  for (plUInt32 uiRoute = route.GetCount(); uiRoute-- > 0;)
  {
    const plUiFocusable::RouteEntry& routeEntry = route[uiRoute];
    if (routeEntry.m_pPlacement == nullptr)
      continue;

    for (const plUiEventReaction* pReaction : routeEntry.m_pPlacement->m_Reactions)
    {
      if (pReaction == nullptr || pReaction->m_sEvent != sEvent)
        continue;

      for (const plUiEventAction* pAction : pReaction->m_Actions)
      {
        if (pAction == nullptr)
          continue;

        switch (pAction->m_Type)
        {
          case plUiEventActionType::SetState:
          {
            if (pAction->m_sEntry.IsEmpty())
              break;

            const plTempHashedString sEntry(pAction->m_sEntry.GetData());
            plBlackboard* pBoard = m_BindingState.FindBoardForEntry(sEntry);
            if (pBoard == nullptr)
              pBoard = m_pLocalBlackboard.Borrow();

            if (pBoard != nullptr)
              pBoard->SetEntryValue(pAction->m_sEntry, pAction->m_Value);
            break;
          }

          case plUiEventActionType::Show:
          case plUiEventActionType::Hide:
          case plUiEventActionType::ToggleVisibility:
          {
            const plUiElement* pTarget = findElementByName(routeEntry.m_pParentDocumentRoot, pAction->m_sTarget);
            if (pTarget == nullptr)
              break;

            const plUiElementInstance target{pTarget, routeEntry.m_uiParentSalt};
            if (pAction->m_Type == plUiEventActionType::ToggleVisibility)
              m_BindingState.ToggleFlowVisibility(target);
            else
              m_BindingState.SetFlowVisibility(target, pAction->m_Type == plUiEventActionType::Show);
            break;
          }

          case plUiEventActionType::PlayClip:
            if (!pAction->m_sClip.IsEmpty())
              m_BindingState.PlayClip(pAction->m_sClip, routeEntry.m_pParentDocumentRoot, routeEntry.m_uiParentSalt);
            break;
        }
      }

      return true;
    }
  }

  return false;
}

void plUiCanvasComponent::TriggerClick(const plUiButton* pButton, const plUiElementInstance& instance, const plUiElement* pDocumentRoot, plArrayPtr<const plUiFocusable::RouteEntry> route, plInt32 iItemIndex, const plUiItemList* pItemList, plUInt32 uiUiInstanceId)
{
  // clicks inside a list row also select it (SelectedBinding entry)
  WriteListSelection(pItemList, iItemIndex);

  // event message to gameplay + animation clips listening on the identifier
  if (!pButton->m_sClickEvent.IsEmpty())
  {
    m_BindingState.NotifyEvent(pButton->m_sClickEvent, pDocumentRoot, instance.m_uiSalt);
    ExecuteEventReactions(pButton->m_sClickEvent, route);

    plMsgUiEvent msg;
    msg.m_sIdentifier.Assign(pButton->m_sClickEvent);
    msg.m_iItemIndex = iItemIndex;
    msg.m_uiUiInstanceId = uiUiInstanceId;

    m_UiEventSender.SendEventMessage(msg, this, GetOwner());
  }

  // blackboard entry write, so other bound elements react without game code
  if (!pButton->m_sClickEntryBinding.IsEmpty())
  {
    const plTempHashedString sEntry(pButton->m_sClickEntryBinding.GetData());

    plBlackboard* pBoard = m_BindingState.FindBoardForEntry(sEntry);
    if (pBoard == nullptr)
    {
      pBoard = m_BindingState.GetPrimaryBoard(); // create the entry on the first board
    }

    if (pBoard != nullptr)
    {
      bool bNewValue = true;

      if (pButton->m_ClickEntryAction == plUiClickEntryAction::Toggle)
      {
        bNewValue = !pBoard->GetBoolValue(sEntry, false);
      }
      else if (pButton->m_ClickEntryAction == plUiClickEntryAction::SetFalse)
      {
        bNewValue = false;
      }

      pBoard->SetEntryValue(pButton->m_sClickEntryBinding, bNewValue);
    }
  }

  // unbound toggles flip their per-instance state on click
  if (plDynamicCast<const plUiToggle*>(pButton) != nullptr && pButton->m_sClickEntryBinding.IsEmpty())
  {
    bool& bChecked = m_LocalToggleState[instance]; // inserts false on first click
    bChecked = !bChecked;
  }
}

void plUiCanvasComponent::AdjustSliderValue(const plUiSlider* pSlider, const plUiElementInstance& instance, float fDelta01)
{
  // current normalized value: resolved binding > local state > authored (mirrors drawing)
  const float fRange = pSlider->m_fMax - pSlider->m_fMin;
  float fT = (plMath::Abs(fRange) > 1e-6f) ? plMath::Clamp((pSlider->m_fValue - pSlider->m_fMin) / fRange, 0.0f, 1.0f) : 0.0f;

  if (const float* pLocal = m_LocalSliderValues.GetValue(instance))
  {
    fT = plMath::Clamp(*pLocal, 0.0f, 1.0f);
  }

  if (const plUiBindingState::ResolvedValues* pResolved = m_BindingState.GetResolved(instance))
  {
    if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasFill)
    {
      fT = pResolved->m_fFill;
    }
  }

  fT = plMath::Clamp(fT + fDelta01, 0.0f, 1.0f);

  if (!pSlider->m_sValueBinding.IsEmpty())
  {
    const plTempHashedString sEntry(pSlider->m_sValueBinding.GetData());

    plBlackboard* pBoard = m_BindingState.FindBoardForEntry(sEntry);
    if (pBoard == nullptr)
    {
      pBoard = m_BindingState.GetPrimaryBoard();
    }

    if (pBoard != nullptr)
    {
      pBoard->SetEntryValue(pSlider->m_sValueBinding, pSlider->m_fMin + fT * fRange);
    }
  }
  else
  {
    m_LocalSliderValues[instance] = fT;
  }
}

namespace
{
  plInt32 CountTextCharacters(plStringView sText)
  {
    plInt32 iCount = 0;

    for (auto it = sText.GetIteratorFront(); it.IsValid(); ++it)
    {
      ++iCount;
    }

    return iCount;
  }

  /// byte offset of the given CHARACTER index (clamped to the string end)
  plUInt32 CharIndexToByteOffset(plStringView sText, plInt32 iChar)
  {
    auto it = sText.GetIteratorFront();
    plInt32 i = 0;

    while (i < iChar && it.IsValid())
    {
      ++it;
      ++i;
    }

    return it.IsValid() ? static_cast<plUInt32>(it.GetData() - sText.GetStartPointer()) : sText.GetElementCount();
  }

  /// removes the character at the given index (no-op when out of range)
  void RemoveTextCharacter(plString& ref_sText, plInt32 iChar)
  {
    if (iChar < 0)
      return;

    const plUInt32 uiFrom = CharIndexToByteOffset(ref_sText, iChar);
    const plUInt32 uiTo = CharIndexToByteOffset(ref_sText, iChar + 1);

    if (uiFrom >= uiTo)
      return;

    plStringBuilder sb;
    sb.Append(plStringView(ref_sText.GetData(), ref_sText.GetData() + uiFrom));
    sb.Append(plStringView(ref_sText.GetData() + uiTo, ref_sText.GetData() + ref_sText.GetElementCount()));
    ref_sText = sb;
  }
} // namespace

plString plUiCanvasComponent::GetTextInputDisplayText(const plUiTextInput* pInput, const plUiElementInstance& instance) const
{
  // edit memory wins (it is also the persistent value of unbound inputs), then the bound
  // entry, then the authored default — mirrors the draw-side precedence
  if (const plUiTextEditState* pState = m_TextEditStates.GetValue(instance))
  {
    return pState->m_sText;
  }

  if (!pInput->m_sTextBinding.IsEmpty())
  {
    if (const plUiBindingState::ResolvedValues* pResolved = m_BindingState.GetResolved(instance))
    {
      if (pResolved->m_uiFlags & plUiBindingState::ResolvedValues::HasText)
      {
        return pResolved->m_sText;
      }
    }
  }

  return pInput->m_sText;
}

plUiTextEditState& plUiCanvasComponent::BeginTextEdit(const plUiTextInput* pInput, const plUiElementInstance& instance)
{
  if (plUiTextEditState* pState = m_TextEditStates.GetValue(instance))
  {
    return *pState;
  }

  // seed from the current display value BEFORE inserting the (empty) edit state
  const plString sInitial = GetTextInputDisplayText(pInput, instance);

  plUiTextEditState& state = m_TextEditStates[instance];
  state.m_sText = sInitial;
  state.m_iCaret = CountTextCharacters(sInitial);
  state.m_fBlink = 0.0f;
  return state;
}

void plUiCanvasComponent::WriteTextInputBinding(const plUiTextInput* pInput, const plString& sText)
{
  if (pInput->m_sTextBinding.IsEmpty())
    return;

  const plTempHashedString sEntry(pInput->m_sTextBinding.GetData());

  plBlackboard* pBoard = m_BindingState.FindBoardForEntry(sEntry);
  if (pBoard == nullptr)
  {
    pBoard = m_BindingState.GetPrimaryBoard(); // create the entry on the first board
  }

  if (pBoard != nullptr)
  {
    pBoard->SetEntryValue(pInput->m_sTextBinding, sText);
  }
}

void plUiCanvasComponent::InsertTextInputCharacter(const plUiTextInput* pInput, plUInt32 uiCodepoint)
{
  if (uiCodepoint < 32)
    return;

  plUiTextEditState& state = BeginTextEdit(pInput, m_FocusedElement);

  if (pInput->m_uiMaxLength > 0 && CountTextCharacters(state.m_sText) >= static_cast<plInt32>(pInput->m_uiMaxLength))
    return;

  char szUtf8[8] = "";
  char* pWrite = szUtf8;
  plUnicodeUtils::EncodeUtf32ToUtf8(uiCodepoint, pWrite);
  *pWrite = '\0';

  const plUInt32 uiOffset = CharIndexToByteOffset(state.m_sText, state.m_iCaret);

  plStringBuilder sb;
  sb.Append(plStringView(state.m_sText.GetData(), state.m_sText.GetData() + uiOffset));
  sb.Append(szUtf8);
  sb.Append(plStringView(state.m_sText.GetData() + uiOffset, state.m_sText.GetData() + state.m_sText.GetElementCount()));

  state.m_sText = sb;
  ++state.m_iCaret;
  state.m_fBlink = 0.0f;

  WriteTextInputBinding(pInput, state.m_sText); // live write, the slider-drag precedent
}

void plUiCanvasComponent::ApplyTextInputEditKey(const plUiTextInput* pInput, plUiEditKey::Enum key)
{
  plUiTextEditState& state = BeginTextEdit(pInput, m_FocusedElement);
  state.m_fBlink = 0.0f;

  switch (key)
  {
    case plUiEditKey::Backspace:
      if (state.m_iCaret > 0)
      {
        RemoveTextCharacter(state.m_sText, state.m_iCaret - 1);
        --state.m_iCaret;
        WriteTextInputBinding(pInput, state.m_sText);
      }
      break;

    case plUiEditKey::Delete:
      if (state.m_iCaret < CountTextCharacters(state.m_sText))
      {
        RemoveTextCharacter(state.m_sText, state.m_iCaret);
        WriteTextInputBinding(pInput, state.m_sText);
      }
      break;

    case plUiEditKey::Home:
      state.m_iCaret = 0;
      break;

    case plUiEditKey::End:
      state.m_iCaret = CountTextCharacters(state.m_sText);
      break;

    default:
      break;
  }
}

void plUiCanvasComponent::MoveTextInputCaret(const plUiTextInput* pInput, plInt32 iDelta)
{
  plUiTextEditState& state = BeginTextEdit(pInput, m_FocusedElement);
  state.m_iCaret = plMath::Clamp(state.m_iCaret + iDelta, 0, CountTextCharacters(state.m_sText));
  state.m_fBlink = 0.0f;
}

void plUiCanvasComponent::CommitTextInput(const plUiTextInput* pInput)
{
  plUiTextEditState& state = BeginTextEdit(pInput, m_FocusedElement);

  WriteTextInputBinding(pInput, state.m_sText);

  if (!pInput->m_sSubmitEvent.IsEmpty())
  {
    m_BindingState.NotifyEvent(pInput->m_sSubmitEvent);

    plMsgUiEvent msg;
    msg.m_sIdentifier.Assign(pInput->m_sSubmitEvent);

    m_UiEventSender.SendEventMessage(msg, this, GetOwner());
  }

  state.m_fBlink = 0.0f;
}

void plUiCanvasComponent::RevertTextInput(const plUiTextInput* pInput)
{
  PL_IGNORE_UNUSED(pInput);

  // drop the edit memory: the display falls back to the bound entry / authored default
  m_TextEditStates.Remove(m_FocusedElement);
  m_FocusedElement.Invalidate();
  m_pFocusedDocumentRoot = nullptr;
  m_FocusedRoute.Clear();
}

void plUiCanvasComponent::UpdateTextInputEditing(float fDeltaTime)
{
  const plUiTextInput* pInput = plDynamicCast<const plUiTextInput*>(m_FocusedElement.m_pElement);

  if (pInput == nullptr)
  {
    m_QueuedCharacters.Clear();
    m_QueuedEditKeys.Clear();
    return;
  }

  if (m_bInputInjectionEnabled)
  {
    for (plUInt32 uiCodepoint : m_QueuedCharacters)
    {
      InsertTextInputCharacter(pInput, uiCodepoint);
    }

    for (plUInt8 uiKey : m_QueuedEditKeys)
    {
      ApplyTextInputEditKey(pInput, static_cast<plUiEditKey::Enum>(uiKey));
    }

    m_QueuedCharacters.Clear();
    m_QueuedEditKeys.Clear();
  }
  else
  {
    // the DearImgui/RmlUi polling convention: last typed codepoint + raw editing keys
    const plUInt32 uiCodepoint = plInputManager::RetrieveLastCharacter(false);

    if (uiCodepoint >= 32)
    {
      InsertTextInputCharacter(pInput, uiCodepoint);
    }

    auto keyPressed = [](const char* szSlot) {
      return plInputManager::GetInputSlotState(szSlot) == plKeyState::Pressed;
    };

    if (keyPressed(plInputSlot_KeyBackspace))
    {
      ApplyTextInputEditKey(pInput, plUiEditKey::Backspace);
    }
    if (keyPressed(plInputSlot_KeyDelete))
    {
      ApplyTextInputEditKey(pInput, plUiEditKey::Delete);
    }
    if (keyPressed(plInputSlot_KeyHome))
    {
      ApplyTextInputEditKey(pInput, plUiEditKey::Home);
    }
    if (keyPressed(plInputSlot_KeyEnd))
    {
      ApplyTextInputEditKey(pInput, plUiEditKey::End);
    }
  }

  // caret blink phase (reset to 0 by every edit, so typing keeps the caret solid)
  if (plUiTextEditState* pState = m_TextEditStates.GetValue(m_FocusedElement))
  {
    pState->m_fBlink += fDeltaTime;
  }
}

void plUiCanvasComponent::HandleNavAction(plUiNavAction::Enum action, plArrayPtr<const plUiFocusable> focusables)
{
  // a focused text input consumes everything except Up/Down (which navigate away)
  if (const plUiTextInput* pTextInput = plDynamicCast<const plUiTextInput*>(m_FocusedElement.m_pElement))
  {
    switch (action)
    {
      case plUiNavAction::Left:
        MoveTextInputCaret(pTextInput, -1);
        return;
      case plUiNavAction::Right:
        MoveTextInputCaret(pTextInput, 1);
        return;
      case plUiNavAction::Accept:
        CommitTextInput(pTextInput);
        return;
      case plUiNavAction::Cancel:
        RevertTextInput(pTextInput); // does NOT raise CancelEvent — Escape first leaves the field
        return;
      default:
        break;
    }
  }

  if (action == plUiNavAction::Cancel)
  {
    if (!m_sCancelEvent.IsEmpty())
    {
      m_BindingState.NotifyEvent(m_sCancelEvent);

      plMsgUiEvent msg;
      msg.m_sIdentifier.Assign(m_sCancelEvent);

      m_UiEventSender.SendEventMessage(msg, this, GetOwner());
    }

    return;
  }

  if (action == plUiNavAction::Accept)
  {
    if (const plUiButton* pButton = plDynamicCast<const plUiButton*>(m_FocusedElement.m_pElement))
    {
      TriggerClick(pButton, m_FocusedElement, m_pFocusedDocumentRoot, m_FocusedRoute, m_iFocusedItemIndex, m_pFocusedItemList, m_uiFocusedUiInstanceId);
    }

    return;
  }

  // ————— directional move —————

  if (focusables.IsEmpty())
    return;

  // nothing focused yet: the first direction press lands on the DefaultFocus element
  // (mockup's star marker), else the top-left-most focusable
  if (!m_FocusedElement.IsValid())
  {
    plInt32 iBest = -1;

    for (plUInt32 i = 0; i < focusables.GetCount(); ++i)
    {
      if (focusables[i].m_pElement->m_bDefaultFocus)
      {
        iBest = static_cast<plInt32>(i);
        break;
      }

      if (iBest < 0 || (focusables[i].m_Rect.y + focusables[i].m_Rect.x * 0.001f) < (focusables[iBest].m_Rect.y + focusables[iBest].m_Rect.x * 0.001f))
      {
        iBest = static_cast<plInt32>(i);
      }
    }

    m_FocusedElement = plUiElementInstance{focusables[iBest].m_pElement, focusables[iBest].m_uiSalt};
    m_pFocusedDocumentRoot = focusables[iBest].m_pDocumentRoot;
    m_FocusedRoute = focusables[iBest].m_Route;
    m_pFocusedItemList = focusables[iBest].m_pItemList;
    m_iFocusedItemIndex = focusables[iBest].m_iItemIndex;
    m_uiFocusedUiInstanceId = focusables[iBest].m_uiDynamicInstanceId;
    return;
  }

  // focused sliders consume Left/Right to change their value instead of moving focus
  if (const plUiSlider* pSlider = plDynamicCast<const plUiSlider*>(m_FocusedElement.m_pElement))
  {
    if (action == plUiNavAction::Left || action == plUiNavAction::Right)
    {
      AdjustSliderValue(pSlider, m_FocusedElement, (action == plUiNavAction::Right) ? 0.05f : -0.05f);
      return;
    }
  }

  // find the focused element's rect in this frame's collection (validated above)
  plUiFocusable from;

  for (const plUiFocusable& focusable : focusables)
  {
    if (focusable.m_pElement == m_FocusedElement.m_pElement && focusable.m_uiSalt == m_FocusedElement.m_uiSalt)
    {
      from = focusable;
      break;
    }
  }

  const plInt32 iTarget = FindNavTarget(from, action, focusables);

  if (iTarget >= 0)
  {
    m_FocusedElement = plUiElementInstance{focusables[iTarget].m_pElement, focusables[iTarget].m_uiSalt};
    m_pFocusedDocumentRoot = focusables[iTarget].m_pDocumentRoot;
    m_FocusedRoute = focusables[iTarget].m_Route;
    m_pFocusedItemList = focusables[iTarget].m_pItemList;
    m_iFocusedItemIndex = focusables[iTarget].m_iItemIndex;
    m_uiFocusedUiInstanceId = focusables[iTarget].m_uiDynamicInstanceId;
  }
}

plInt32 plUiCanvasComponent::FindNavTarget(const plUiFocusable& from, plUiNavAction::Enum direction, plArrayPtr<const plUiFocusable> focusables, bool* out_pExplicit /*= nullptr*/)
{
  if (out_pExplicit != nullptr)
  {
    *out_pExplicit = false;
  }

  if (from.m_pElement == nullptr)
    return -1;

  // explicit override by element name wins (authoring escape hatch for odd layouts)
  const plString* pOverrideName = nullptr;

  switch (direction)
  {
    case plUiNavAction::Up:
      pOverrideName = &from.m_pElement->m_sNavUp;
      break;
    case plUiNavAction::Down:
      pOverrideName = &from.m_pElement->m_sNavDown;
      break;
    case plUiNavAction::Left:
      pOverrideName = &from.m_pElement->m_sNavLeft;
      break;
    case plUiNavAction::Right:
      pOverrideName = &from.m_pElement->m_sNavRight;
      break;
    default:
      return -1;
  }

  if (!pOverrideName->IsEmpty())
  {
    for (plUInt32 i = 0; i < focusables.GetCount(); ++i)
    {
      if (focusables[i].m_pElement->m_sName == *pOverrideName && !(focusables[i].m_pElement == from.m_pElement && focusables[i].m_uiSalt == from.m_uiSalt))
      {
        if (out_pExplicit != nullptr)
        {
          *out_pExplicit = true;
        }

        return static_cast<plInt32>(i);
      }
    }
    // named target missing/hidden: fall through to the geometric search
  }

  plVec2 vDir = plVec2::MakeZero();

  switch (direction)
  {
    case plUiNavAction::Up:
      vDir.Set(0, -1);
      break;
    case plUiNavAction::Down:
      vDir.Set(0, 1);
      break;
    case plUiNavAction::Left:
      vDir.Set(-1, 0);
      break;
    case plUiNavAction::Right:
      vDir.Set(1, 0);
      break;
    default:
      return -1;
  }

  // geometric best candidate: direction half-space + ~70 degree cone, scored by projected
  // distance plus a doubled orthogonal penalty (Unreal-style)
  const plVec2 vFrom(from.m_Rect.x + from.m_Rect.width * 0.5f, from.m_Rect.y + from.m_Rect.height * 0.5f);

  plInt32 iBest = -1;
  float fBestScore = plMath::MaxValue<float>();

  for (plUInt32 i = 0; i < focusables.GetCount(); ++i)
  {
    const plUiFocusable& candidate = focusables[i];

    if (candidate.m_pElement == from.m_pElement && candidate.m_uiSalt == from.m_uiSalt)
      continue;

    const plVec2 vCenter(candidate.m_Rect.x + candidate.m_Rect.width * 0.5f, candidate.m_Rect.y + candidate.m_Rect.height * 0.5f);
    const plVec2 vDelta = vCenter - vFrom;

    const float fProjected = vDir.Dot(vDelta);
    if (fProjected <= 0.5f)
      continue; // behind or on top of the source

    const float fLength = vDelta.GetLength();
    if (fProjected < 0.3f * fLength)
      continue; // outside the direction cone

    const float fOrthogonal = plMath::Abs(vDir.x * vDelta.y - vDir.y * vDelta.x);
    const float fScore = fProjected + 2.0f * fOrthogonal;

    if (fScore < fBestScore)
    {
      fBestScore = fScore;
      iBest = static_cast<plInt32>(i);
    }
  }

  return iBest;
}

void plUiCanvasComponent::CollectFocusables(const plVec2& vViewportSize, plDynamicArray<plUiFocusable>& out_focusables) const
{
  out_focusables.Clear();

  if (!m_hDocument.IsValid())
    return;

  plResourceLock<plUiDocumentResource> pDocument(m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
  if (pDocument.GetAcquireResult() != plResourceAcquireResult::Final || pDocument->GetRoot() == nullptr)
    return;

  float fScale = 1.0f;
  plVec2 vOffset = plVec2::MakeZero();
  plVec2 vCanvasSize = plVec2::MakeZero();
  ComputeCanvasTransform(vViewportSize, fScale, vOffset, vCanvasSize);

  const plRectFloat canvasRect(0.0f, 0.0f, vCanvasSize.x, vCanvasSize.y);

  plUiHitTestResult hit;
  hit.m_pFocusables = &out_focusables;

  // a probe position no element contains: this walk only collects the nav graph
  UiTransformStack transforms;
  plHybridArray<plUiFocusable::RouteEntry, 8> route;
  HitTestInRect(pDocument->GetRoot(), pDocument->GetRoot(), pDocument->GetRoot()->ComputeRect(canvasRect), canvasRect, plVec2(-2.0e9f, -2.0e9f), &m_BindingState, &m_ScrollOffsets, 0, 0, nullptr, -1, &m_DynamicInstances, 0, route, transforms, hit);
}

void plUiCanvasComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);

  plStreamWriter& s = inout_stream.GetStream();

  s << m_hDocument;
  s << m_ScaleMode;
  s << m_uiReferenceHeight;
  s << m_iSortOrder;
  s << m_bAutobindBlackboards;

  // version 3
}

void plUiCanvasComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());

  plStreamReader& s = inout_stream.GetStream();

  s >> m_hDocument;
  s >> m_ScaleMode;
  s >> m_uiReferenceHeight;
  s >> m_iSortOrder;

  if (uiVersion >= 2)
  {
    s >> m_bAutobindBlackboards;
  }

  if (uiVersion == 3)
  {
    // legacy: the removed document-wide StyleOverride (styles are per-element now)
    plUiStyleResourceHandle hLegacyStyleOverride;
    s >> hLegacyStyleOverride;
  }
}

plResult plUiCanvasComponent::GetLocalBounds(plBoundingBoxSphere& ref_bounds, bool& ref_bAlwaysVisible, plMsgUpdateLocalBounds& ref_msg)
{
  ref_bAlwaysVisible = true;
  return PL_SUCCESS;
}

void plUiCanvasComponent::SetUiDocument(const plUiDocumentResourceHandle& hDocument)
{
  if (m_hDocument == hDocument)
    return;

  m_hDocument = hDocument;
  m_pLocalStateRoot = nullptr;
  m_uiLocalStateDocumentVersion = 0xFFFFFFFF;
  if (m_pLocalBlackboard != nullptr)
    m_pLocalBlackboard->RemoveAllEntries();
  m_bBindingBoardsDirty = true;
  ResetInteractionState();
}

void plUiCanvasComponent::OnMsgExtractRenderData(plMsgExtractRenderData& msg) const
{
  if (msg.m_pView->GetCameraUsageHint() != plCameraUsageHint::MainView && msg.m_pView->GetCameraUsageHint() != plCameraUsageHint::EditorView && msg.m_pView->GetCameraUsageHint() != plCameraUsageHint::Thumbnail)
    return;

  // Don't extract render data for selection or shadow passes.
  if (msg.m_OverrideCategory != plInvalidRenderDataCategory)
    return;

  if (!m_hDocument.IsValid())
    return;

  plResourceLock<plUiDocumentResource> pDocument(m_hDocument, plResourceAcquireMode::AllowLoadingFallback_NeverFail);
  if (pDocument.GetAcquireResult() != plResourceAcquireResult::Final || pDocument->GetRoot() == nullptr)
    return;

  const float fViewWidth = msg.m_pView->GetViewport().width;
  const float fViewHeight = msg.m_pView->GetViewport().height;

  if (fViewWidth <= 0.0f || fViewHeight <= 0.0f)
    return;

  float fScale = 1.0f;
  plVec2 vOffset = plVec2::MakeZero();
  plVec2 vCanvasSize = plVec2::MakeZero();
  ComputeCanvasTransform(plVec2(fViewWidth, fViewHeight), fScale, vOffset, vCanvasSize);

  const plRectFloat canvasRect(0.0f, 0.0f, vCanvasSize.x, vCanvasSize.y);

  const plDebugRendererContext debugContext(msg.m_pView->GetHandle());

  plUiDrawContext ctx;
  ctx.m_fScale = fScale;
  ctx.m_vOffset = vOffset;
  ctx.m_pDebugContext = &debugContext;
  ctx.m_HoveredElement = GetEffectiveHover(); // focus lights up like hover
  ctx.m_PressedElement = m_PressedElement;
  ctx.m_FocusedElement = m_FocusedElement;
  ctx.m_pBindings = &m_BindingState;
  ctx.m_pScrollOffsets = &m_ScrollOffsets;
  ctx.m_pLocalToggleState = &m_LocalToggleState;
  ctx.m_pLocalSliderValues = &m_LocalSliderValues;
  ctx.m_pTextEditStates = &m_TextEditStates;
  ctx.m_pDynamicInstances = &m_DynamicInstances;

  // clip stack base = the full viewport (every batch needs a scissor, ScissorTest is on)
  {
    const plRectFloat& viewport = msg.m_pView->GetViewport();
    ctx.m_ClipStack.PushBack(plRectU32(static_cast<plUInt32>(viewport.x), static_cast<plUInt32>(viewport.y), static_cast<plUInt32>(viewport.width), static_cast<plUInt32>(viewport.height)));
  }

  // The simulated device canvas can be letterboxed, zoomed, or panned within the
  // viewport. Keep root overflow inside that canvas just like a real render target.
  ctx.PushClip(canvasRect);

  // editor preview: checkerboard behind the UI (emitted first = drawn underneath)
  if (m_PreviewView.m_bCheckerboard)
  {
    const plColorLinearUB darkColor(38, 38, 38, 255);
    const plColorLinearUB lightColor(52, 52, 52, 255);

    // keep the quad count bounded for large simulated resolutions
    const float fCellSize = plMath::Max(32.0f, vCanvasSize.y / 24.0f);

    for (float y = 0; y < vCanvasSize.y; y += fCellSize)
    {
      for (float x = 0; x < vCanvasSize.x; x += fCellSize)
      {
        const bool bLight = (static_cast<plInt32>(x / fCellSize) + static_cast<plInt32>(y / fCellSize)) % 2 == 0;
        const plRectFloat cell(x, y, plMath::Min(fCellSize, vCanvasSize.x - x), plMath::Min(fCellSize, vCanvasSize.y - y));
        ctx.AddQuad(cell, plVec2::MakeZero(), plVec2(1, 1), bLight ? lightColor : darkColor, nullptr);
      }
    }
  }

  DrawElementInRect(pDocument->GetRoot(), pDocument->GetRoot()->ComputeRect(canvasRect), ctx);

  if (ctx.m_Vertices.IsEmpty())
    return;

  plUiRenderData* pRenderData = msg.m_pRenderDataManager->CreateRenderDataForThisFrame<plUiRenderData>(GetOwner());
  pRenderData->m_uiSortingKey = static_cast<plUInt32>(m_iSortOrder + 0x8000);

  // copy into frame-allocated arrays; this memory is reset without destructors,
  // which is fine because all three element types are PODs (see plUiBatch)
  pRenderData->m_Vertices = PL_NEW_ARRAY(plFrameAllocator::GetCurrentAllocator(), plUiVertex, ctx.m_Vertices.GetCount());
  pRenderData->m_Vertices.CopyFrom(ctx.m_Vertices.GetArrayPtr());

  pRenderData->m_Indices = PL_NEW_ARRAY(plFrameAllocator::GetCurrentAllocator(), plUInt32, ctx.m_Indices.GetCount());
  pRenderData->m_Indices.CopyFrom(ctx.m_Indices.GetArrayPtr());

  pRenderData->m_Batches = PL_NEW_ARRAY(plFrameAllocator::GetCurrentAllocator(), plUiBatch, ctx.m_Batches.GetCount());
  pRenderData->m_Batches.CopyFrom(ctx.m_Batches.GetArrayPtr());

  // draw-list stats for the editor status bar (display-only, hence mutable)
  m_uiLastExtractedBatches = ctx.m_Batches.GetCount();
  m_uiLastExtractedQuads = ctx.m_Indices.GetCount() / 6;

  msg.AddRenderData(pRenderData, plDefaultRenderDataCategories::GUI, plRenderData::Caching::Never);
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Components_Implementation_UiCanvasComponent);
