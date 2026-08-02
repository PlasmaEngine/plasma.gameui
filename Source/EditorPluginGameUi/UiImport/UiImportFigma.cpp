#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiImport/UiImportFigma.h>
#include <EditorPluginGameUi/UiImport/UiImportWriter.h>

#include <EditorFramework/EditorApp/EditorApp.moc.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/JSONReader.h>
#include <Foundation/IO/MemoryStream.h>
#include <Foundation/IO/OSFile.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Document/DocumentManager.h>
#include <ToolsFoundation/Project/ToolsProject.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <memory>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiFigmaDocumentGenerator, 1, plRTTIDefaultAllocator<plUiFigmaDocumentGenerator>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiFigmaImportAction, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

// ═══════════════════════════════════════════════════════════════════════════════════════
// JSON helpers
// ═══════════════════════════════════════════════════════════════════════════════════════

namespace
{
  const plVariant* JsonGet(const plVariantDictionary& dict, plStringView sKey)
  {
    return dict.GetValue(plString(sKey));
  }

  plStringView JsonString(const plVariantDictionary& dict, plStringView sKey, plStringView sFallback = {})
  {
    const plVariant* pVar = JsonGet(dict, sKey);
    if (pVar != nullptr && pVar->IsA<plString>())
      return pVar->Get<plString>().GetView();
    return sFallback;
  }

  double JsonNumber(const plVariantDictionary& dict, plStringView sKey, double fFallback = 0.0)
  {
    const plVariant* pVar = JsonGet(dict, sKey);
    if (pVar != nullptr && pVar->IsNumber())
      return pVar->ConvertTo<double>();
    return fFallback;
  }

  bool JsonBool(const plVariantDictionary& dict, plStringView sKey, bool bFallback)
  {
    const plVariant* pVar = JsonGet(dict, sKey);
    if (pVar != nullptr && pVar->IsA<bool>())
      return pVar->Get<bool>();
    return bFallback;
  }

  const plVariantArray* JsonArray(const plVariantDictionary& dict, plStringView sKey)
  {
    const plVariant* pVar = JsonGet(dict, sKey);
    if (pVar != nullptr && pVar->IsA<plVariantArray>())
      return &pVar->Get<plVariantArray>();
    return nullptr;
  }

  const plVariantDictionary* JsonDict(const plVariantDictionary& dict, plStringView sKey)
  {
    const plVariant* pVar = JsonGet(dict, sKey);
    if (pVar != nullptr && pVar->IsA<plVariantDictionary>())
      return &pVar->Get<plVariantDictionary>();
    return nullptr;
  }

  plColorGammaUB FigmaColor(const plVariantDictionary& colorDict, double fOpacity)
  {
    const double r = JsonNumber(colorDict, "r", 1.0);
    const double g = JsonNumber(colorDict, "g", 1.0);
    const double b = JsonNumber(colorDict, "b", 1.0);
    const double a = JsonNumber(colorDict, "a", 1.0) * fOpacity;

    return plColorGammaUB(
      static_cast<plUInt8>(plMath::Clamp(r * 255.0, 0.0, 255.0)),
      static_cast<plUInt8>(plMath::Clamp(g * 255.0, 0.0, 255.0)),
      static_cast<plUInt8>(plMath::Clamp(b * 255.0, 0.0, 255.0)),
      static_cast<plUInt8>(plMath::Clamp(a * 255.0, 0.0, 255.0)));
  }

  bool IsVectorType(plStringView sType)
  {
    return sType == "VECTOR" || sType == "BOOLEAN_OPERATION" || sType == "STAR" || sType == "LINE" ||
           sType == "ELLIPSE" || sType == "POLYGON" || sType == "REGULAR_POLYGON";
  }

  bool IsContainerType(plStringView sType)
  {
    return sType == "FRAME" || sType == "GROUP" || sType == "COMPONENT" || sType == "COMPONENT_SET" ||
           sType == "INSTANCE" || sType == "SECTION";
  }

  plRectFloat FigmaBounds(const plVariantDictionary& node, const plRectFloat& fallback)
  {
    const plVariantDictionary* pBox = JsonDict(node, "absoluteBoundingBox");
    if (pBox == nullptr)
      return fallback;

    return plRectFloat(
      static_cast<float>(JsonNumber(*pBox, "x", fallback.x)),
      static_cast<float>(JsonNumber(*pBox, "y", fallback.y)),
      static_cast<float>(plMath::Max(JsonNumber(*pBox, "width", fallback.width), 1.0)),
      static_cast<float>(plMath::Max(JsonNumber(*pBox, "height", fallback.height), 1.0)));
  }

  struct FigmaContext
  {
    plUiImportReport* m_pReport = nullptr;
    plMap<plString, plString> m_ImageFiles;  ///< imageRef -> downloaded png (REST path only)
    plMap<plString, plString> m_VectorFiles; ///< node id -> rendered png (REST path only)
    plSet<plString> m_ReportedFonts;
    plSet<plString> m_ReportedComponents;
    bool m_bOffline = true;
  };

  /// Applies fills/strokes/effects/corner radii of a node to a visual-element IR node.
  /// Returns true when an image fill was applied.
  bool ApplyFigmaVisuals(const plVariantDictionary& node, plUiImportNode& ref_ir, FigmaContext& ctx, plStringView sPath, bool bDefaultTransparent)
  {
    bool bHasFill = false;
    bool bImageFill = false;

    if (const plVariantArray* pFills = JsonArray(node, "fills"))
    {
      for (const plVariant& fillVar : *pFills)
      {
        if (!fillVar.IsA<plVariantDictionary>())
          continue;

        const plVariantDictionary& fill = fillVar.Get<plVariantDictionary>();
        if (!JsonBool(fill, "visible", true))
          continue;

        const plStringView sFillType = JsonString(fill, "type");
        const double fFillOpacity = JsonNumber(fill, "opacity", 1.0);

        if (sFillType == "SOLID")
        {
          if (const plVariantDictionary* pColor = JsonDict(fill, "color"))
          {
            ref_ir.Set("Color", FigmaColor(*pColor, fFillOpacity));
            bHasFill = true;
          }
        }
        else if (sFillType == "GRADIENT_LINEAR")
        {
          const plVariantArray* pStops = JsonArray(fill, "gradientStops");
          if (pStops != nullptr && pStops->GetCount() >= 2 &&
              (*pStops)[0].IsA<plVariantDictionary>() && pStops->PeekBack().IsA<plVariantDictionary>())
          {
            const plVariantDictionary& stop0 = (*pStops)[0].Get<plVariantDictionary>();
            const plVariantDictionary& stopN = pStops->PeekBack().Get<plVariantDictionary>();

            plInt32 iMode = plUiGradientMode::Vertical;
            if (const plVariantArray* pHandles = JsonArray(fill, "gradientHandlePositions"))
            {
              if (pHandles->GetCount() >= 2 && (*pHandles)[0].IsA<plVariantDictionary>() && (*pHandles)[1].IsA<plVariantDictionary>())
              {
                const plVariantDictionary& h0 = (*pHandles)[0].Get<plVariantDictionary>();
                const plVariantDictionary& h1 = (*pHandles)[1].Get<plVariantDictionary>();
                const double dx = plMath::Abs(JsonNumber(h1, "x") - JsonNumber(h0, "x"));
                const double dy = plMath::Abs(JsonNumber(h1, "y") - JsonNumber(h0, "y"));
                iMode = dx >= dy ? plUiGradientMode::Horizontal : plUiGradientMode::Vertical;
              }
            }

            if (const plVariantDictionary* pC0 = JsonDict(stop0, "color"))
              ref_ir.Set("Color", FigmaColor(*pC0, fFillOpacity));
            if (const plVariantDictionary* pCN = JsonDict(stopN, "color"))
              ref_ir.Set("GradientColor", FigmaColor(*pCN, fFillOpacity));
            ref_ir.Set("GradientMode", iMode);

            if (pStops->GetCount() > 2)
              ctx.m_pReport->Info(sPath, "Gradient has more than two stops; only the first and last import.");
            bHasFill = true;
          }
        }
        else if (sFillType == "IMAGE")
        {
          const plStringView sRef = JsonString(fill, "imageRef");
          const plString* pFile = ctx.m_ImageFiles.GetValue(plString(sRef));

          if (pFile != nullptr && !pFile->IsEmpty())
          {
            ref_ir.m_sTextureFile = *pFile;
            ref_ir.Set("BrushMode", (plInt32)plUiBrushMode::Image);
            ref_ir.Set("Color", plColorGammaUB(255, 255, 255, 255));
            bImageFill = true;
          }
          else
          {
            ref_ir.Set("Color", plColorGammaUB(128, 128, 128, 255));
            ctx.m_pReport->Warning(sPath, ctx.m_bOffline
                                             ? "Image fill needs the REST importer (Project > Import UI from Figma...); gray placeholder."
                                             : "Image fill could not be downloaded; gray placeholder.");
          }
          bHasFill = true;
        }
        else
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("Fill type '{}' is unsupported; skipped.", sFillType);
          ctx.m_pReport->Warning(sPath, sMsg);
        }

        break; // first visible fill only
      }
    }

    if (!bHasFill && bDefaultTransparent)
      ref_ir.Set("Color", plColorGammaUB(0, 0, 0, 0));

    // strokes -> border
    if (const plVariantArray* pStrokes = JsonArray(node, "strokes"))
    {
      for (const plVariant& strokeVar : *pStrokes)
      {
        if (!strokeVar.IsA<plVariantDictionary>())
          continue;

        const plVariantDictionary& stroke = strokeVar.Get<plVariantDictionary>();
        if (!JsonBool(stroke, "visible", true) || JsonString(stroke, "type") != "SOLID")
          continue;

        if (const plVariantDictionary* pColor = JsonDict(stroke, "color"))
        {
          ref_ir.Set("BorderColor", FigmaColor(*pColor, JsonNumber(stroke, "opacity", 1.0)));
          ref_ir.Set("BorderWidth", static_cast<float>(JsonNumber(node, "strokeWeight", 1.0)));
        }
        break;
      }
    }

    // corner radii
    if (const plVariantArray* pRadii = JsonArray(node, "rectangleCornerRadii"))
    {
      if (pRadii->GetCount() == 4)
      {
        ref_ir.Set("CornerRadius", plVec4(
                                     static_cast<float>((*pRadii)[0].ConvertTo<double>()),
                                     static_cast<float>((*pRadii)[1].ConvertTo<double>()),
                                     static_cast<float>((*pRadii)[2].ConvertTo<double>()),
                                     static_cast<float>((*pRadii)[3].ConvertTo<double>())));
      }
    }
    else
    {
      const double fRadius = JsonNumber(node, "cornerRadius", 0.0);
      if (fRadius > 0.0)
        ref_ir.Set("CornerRadius", plVec4(static_cast<float>(fRadius)));
    }

    // effects -> drop shadow
    if (const plVariantArray* pEffects = JsonArray(node, "effects"))
    {
      for (const plVariant& effectVar : *pEffects)
      {
        if (!effectVar.IsA<plVariantDictionary>())
          continue;

        const plVariantDictionary& effect = effectVar.Get<plVariantDictionary>();
        if (!JsonBool(effect, "visible", true))
          continue;

        const plStringView sEffectType = JsonString(effect, "type");
        if (sEffectType == "DROP_SHADOW")
        {
          if (const plVariantDictionary* pColor = JsonDict(effect, "color"))
            ref_ir.Set("ShadowColor", FigmaColor(*pColor, 1.0));

          if (const plVariantDictionary* pOffset = JsonDict(effect, "offset"))
            ref_ir.Set("ShadowOffset", plVec2(static_cast<float>(JsonNumber(*pOffset, "x")), static_cast<float>(JsonNumber(*pOffset, "y"))));

          ref_ir.Set("ShadowSize", static_cast<float>(plMath::Max(JsonNumber(effect, "radius", 4.0), 1.0)));
        }
        else
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("Effect '{}' is unsupported; dropped.", sEffectType);
          ctx.m_pReport->Warning(sPath, sMsg);
        }
      }
    }

    const double fOpacity = JsonNumber(node, "opacity", 1.0);
    if (fOpacity < 1.0)
      ref_ir.Set("Opacity", static_cast<float>(fOpacity));

    return bImageFill;
  }

  /// Anchors from Figma constraints.
  void FigmaConstraintsToAnchors(const plVariantDictionary& node, plVec2& out_vAnchorMin, plVec2& out_vAnchorMax, FigmaContext& ctx, plStringView sPath)
  {
    plStringView sH = "LEFT";
    plStringView sV = "TOP";

    if (const plVariantDictionary* pConstraints = JsonDict(node, "constraints"))
    {
      sH = JsonString(*pConstraints, "horizontal", "LEFT");
      sV = JsonString(*pConstraints, "vertical", "TOP");
    }

    if (sH == "RIGHT")
      out_vAnchorMin.x = out_vAnchorMax.x = 1.0f;
    else if (sH == "CENTER")
      out_vAnchorMin.x = out_vAnchorMax.x = 0.5f;
    else if (sH == "LEFT_RIGHT")
    {
      out_vAnchorMin.x = 0.0f;
      out_vAnchorMax.x = 1.0f;
    }
    else if (sH == "SCALE")
    {
      out_vAnchorMin.x = 0.0f;
      out_vAnchorMax.x = 1.0f;
      ctx.m_pReport->Info(sPath, "SCALE constraint approximated as stretch.");
    }
    else // LEFT
      out_vAnchorMin.x = out_vAnchorMax.x = 0.0f;

    if (sV == "BOTTOM")
      out_vAnchorMin.y = out_vAnchorMax.y = 1.0f;
    else if (sV == "CENTER")
      out_vAnchorMin.y = out_vAnchorMax.y = 0.5f;
    else if (sV == "TOP_BOTTOM")
    {
      out_vAnchorMin.y = 0.0f;
      out_vAnchorMax.y = 1.0f;
    }
    else if (sV == "SCALE")
    {
      out_vAnchorMin.y = 0.0f;
      out_vAnchorMax.y = 1.0f;
    }
    else // TOP
      out_vAnchorMin.y = out_vAnchorMax.y = 0.0f;
  }

  /// Name-prefix widget promotion (documented convention).
  const plRTTI* PromoteByName(plStringView sName)
  {
    if (sName.StartsWith("Btn_"))
      return plGetStaticRTTI<plUiButton>();
    if (sName.StartsWith("Tgl_"))
      return plGetStaticRTTI<plUiToggle>();
    if (sName.StartsWith("Sld_"))
      return plGetStaticRTTI<plUiSlider>();
    if (sName.StartsWith("Bar_"))
      return plGetStaticRTTI<plUiProgressBar>();
    if (sName.StartsWith("Inp_"))
      return plGetStaticRTTI<plUiTextInput>();
    return nullptr;
  }

  void BuildFigmaNode(const plVariantDictionary& node, FigmaContext& ctx, plUiImportNode& ref_irParent, const plRectFloat& parentRect, bool bParentAutoLayout, plStringView sParentPath)
  {
    const plStringView sType = JsonString(node, "type");
    const plStringView sName = JsonString(node, "name", "node");

    plStringBuilder sPath = sParentPath;
    sPath.AppendPath(plStringBuilder(sName).GetData());

    const plRectFloat rect = FigmaBounds(node, plRectFloat(parentRect.x, parentRect.y, 100, 50));
    const bool bVisible = JsonBool(node, "visible", true);

    // ————— type mapping —————
    const plRTTI* pType = nullptr;
    bool bIsVector = false;
    bool bAutoLayout = false;

    const plStringView sLayoutMode = JsonString(node, "layoutMode");
    const plRTTI* pPromoted = PromoteByName(sName);

    if (pPromoted != nullptr)
    {
      pType = pPromoted;
    }
    else if (sType == "TEXT")
    {
      pType = plGetStaticRTTI<plUiText>();
    }
    else if (IsVectorType(sType))
    {
      bIsVector = true;

      // best: real geometry (REST geometry=paths); then a rendered png; then a placeholder
      bool bHasGeometry = false;
      if (const plVariantArray* pFillGeometry = JsonArray(node, "fillGeometry"))
      {
        for (const plVariant& geomVar : *pFillGeometry)
        {
          if (geomVar.IsA<plVariantDictionary>() && !JsonString(geomVar.Get<plVariantDictionary>(), "path").IsEmpty())
          {
            bHasGeometry = true;
            break;
          }
        }
      }

      const plStringView sId = JsonString(node, "id");
      const plString* pFile = ctx.m_VectorFiles.GetValue(plString(sId));

      if (bHasGeometry)
      {
        pType = plGetStaticRTTI<plUiPath>();
      }
      else if (pFile != nullptr && !pFile->IsEmpty())
      {
        pType = plGetStaticRTTI<plUiImage>();
      }
      else
      {
        pType = plGetStaticRTTI<plUiPanel>();
        ctx.m_pReport->Warning(sPath, ctx.m_bOffline
                                        ? "Vector shape has no geometry data in this dump (save the REST response with geometry=paths); gray placeholder."
                                        : "Vector shape could not be imported; gray placeholder.");
      }
    }
    else if (IsContainerType(sType))
    {
      if (sType == "INSTANCE")
      {
        const plStringView sComponentId = JsonString(node, "componentId");
        if (!ctx.m_ReportedComponents.Contains(plString(sComponentId)))
        {
          ctx.m_ReportedComponents.Insert(plString(sComponentId));
          plStringBuilder sMsg;
          sMsg.SetFormat("Component instance '{}' imports flattened (sub-document promotion is not automated).", sName);
          ctx.m_pReport->Info(sPath, sMsg);
        }
      }

      bAutoLayout = sLayoutMode == "HORIZONTAL" || sLayoutMode == "VERTICAL";
      pType = bAutoLayout ? plGetStaticRTTI<plUiStackPanel>() : plGetStaticRTTI<plUiPanel>();
    }
    else if (sType == "RECTANGLE")
    {
      pType = plGetStaticRTTI<plUiPanel>();
    }
    else if (sType == "SLICE")
    {
      return; // export helpers carry no visuals
    }
    else
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("Node type '{}' is unsupported; imported as a Panel.", sType);
      ctx.m_pReport->Warning(sPath, sMsg);
      pType = plGetStaticRTTI<plUiPanel>();
    }

    plUiImportNode* pIr = ref_irParent.AddChild(pType, sName);

    if (!bVisible)
      pIr->Set("Visible", false);

    // ————— placement —————
    if (bParentAutoLayout)
    {
      pIr->m_bStackChild = true;
      pIr->m_vOffsets = plVec4(0, 0, rect.width, rect.height);
    }
    else
    {
      plVec2 vAnchorMin, vAnchorMax;
      FigmaConstraintsToAnchors(node, vAnchorMin, vAnchorMax, ctx, sPath);
      pIr->m_vAnchorMin = vAnchorMin;
      pIr->m_vAnchorMax = vAnchorMax;
      pIr->m_vOffsets = plUiImportUtils::ComputePlacement(rect, parentRect, vAnchorMin, vAnchorMax);
    }

    // ————— content —————
    if (pType == plGetStaticRTTI<plUiText>())
    {
      pIr->Set("Text", JsonString(node, "characters"));

      if (const plVariantDictionary* pStyle = JsonDict(node, "style"))
      {
        pIr->Set("FontSize", static_cast<plInt32>(plMath::Clamp(JsonNumber(*pStyle, "fontSize", 16.0), 4.0, 256.0)));

        const plStringView sAlignH = JsonString(*pStyle, "textAlignHorizontal", "LEFT");
        if (sAlignH == "CENTER")
          pIr->Set("HAlign", (plInt32)plUiTextHAlign::Center);
        else if (sAlignH == "RIGHT")
          pIr->Set("HAlign", (plInt32)plUiTextHAlign::Right);
        else if (sAlignH == "JUSTIFIED")
          ctx.m_pReport->Info(sPath, "Justified text imports left-aligned.");

        const plStringView sAlignV = JsonString(*pStyle, "textAlignVertical", "TOP");
        if (sAlignV == "CENTER")
          pIr->Set("VAlign", (plInt32)plUiTextVAlign::Center);
        else if (sAlignV == "BOTTOM")
          pIr->Set("VAlign", (plInt32)plUiTextVAlign::Bottom);

        const plStringView sFont = JsonString(*pStyle, "fontFamily");
        if (!sFont.IsEmpty() && !ctx.m_ReportedFonts.Contains(plString(sFont)))
        {
          ctx.m_ReportedFonts.Insert(plString(sFont));
          plStringBuilder sMsg;
          sMsg.SetFormat("Font family '{}' is not mapped; assign a UI Font asset after import.", sFont);
          ctx.m_pReport->Info(sPath, sMsg);
        }
      }

      pIr->Set("Wrap", true);

      // text color from the first solid fill
      if (const plVariantArray* pFills = JsonArray(node, "fills"))
      {
        for (const plVariant& fillVar : *pFills)
        {
          if (!fillVar.IsA<plVariantDictionary>())
            continue;
          const plVariantDictionary& fill = fillVar.Get<plVariantDictionary>();
          if (!JsonBool(fill, "visible", true) || JsonString(fill, "type") != "SOLID")
            continue;
          if (const plVariantDictionary* pColor = JsonDict(fill, "color"))
            pIr->Set("TextColor", FigmaColor(*pColor, JsonNumber(fill, "opacity", 1.0)));
          break;
        }
      }
      return;
    }

    if (bIsVector && pType == plGetStaticRTTI<plUiPath>())
    {
      // combine all fill-geometry subpaths into one path-data string; the geometry is in
      // the node's local space, so Fill stretch maps it exactly onto the element rect
      plStringBuilder sCombined;
      if (const plVariantArray* pFillGeometry = JsonArray(node, "fillGeometry"))
      {
        for (const plVariant& geomVar : *pFillGeometry)
        {
          if (!geomVar.IsA<plVariantDictionary>())
            continue;
          const plStringView sGeomPath = JsonString(geomVar.Get<plVariantDictionary>(), "path");
          if (sGeomPath.IsEmpty())
            continue;
          if (!sCombined.IsEmpty())
            sCombined.Append(" ");
          sCombined.Append(sGeomPath);
        }
      }

      pIr->Set("PathData", sCombined.GetView());
      pIr->Set("Stretch", 1); // plUiPathStretch::Fill

      // fill color from the first solid fill
      if (const plVariantArray* pFills = JsonArray(node, "fills"))
      {
        for (const plVariant& fillVar : *pFills)
        {
          if (!fillVar.IsA<plVariantDictionary>())
            continue;
          const plVariantDictionary& fill = fillVar.Get<plVariantDictionary>();
          if (!JsonBool(fill, "visible", true) || JsonString(fill, "type") != "SOLID")
            continue;
          if (const plVariantDictionary* pColor = JsonDict(fill, "color"))
            pIr->Set("Color", FigmaColor(*pColor, JsonNumber(fill, "opacity", 1.0)));
          break;
        }
      }
      return;
    }

    if (bIsVector && pType == plGetStaticRTTI<plUiImage>())
    {
      const plStringView sId = JsonString(node, "id");
      pIr->m_sTextureFile = *ctx.m_VectorFiles.GetValue(plString(sId));
      pIr->Set("BrushMode", (plInt32)plUiBrushMode::Image);
      return;
    }

    if (bIsVector)
    {
      // placeholder panel
      pIr->Set("Color", plColorGammaUB(128, 128, 128, 160));
      return;
    }

    const bool bContainerLike = pType == plGetStaticRTTI<plUiPanel>() || pType == plGetStaticRTTI<plUiStackPanel>();
    const bool bImageFill = ApplyFigmaVisuals(node, *pIr, ctx, sPath, bContainerLike && sType != "RECTANGLE");

    if (pPromoted != nullptr)
    {
      // widget promotion extras
      if (pType == plGetStaticRTTI<plUiButton>() || pType == plGetStaticRTTI<plUiToggle>())
      {
        plStringBuilder sEvent = sName;
        pIr->Set("ClickEvent", sEvent.GetView());
      }
      // Bar_/Sld_/Inp_ keep their defaults; bindings are game-side decisions
    }

    if (bImageFill && !IsContainerType(sType))
      return; // plain image rectangle

    // ————— children —————
    if (const plVariantArray* pChildren = JsonArray(node, "children"))
    {
      plRectFloat contentRect = rect;

      if (bAutoLayout)
      {
        pIr->Set("Direction", sLayoutMode == "HORIZONTAL" ? (plInt32)plUiStackDirection::Horizontal : (plInt32)plUiStackDirection::Vertical);
        pIr->Set("Spacing", static_cast<float>(JsonNumber(node, "itemSpacing", 0.0)));
        pIr->Set("Padding", plVec4(
                              static_cast<float>(JsonNumber(node, "paddingLeft", 0.0)),
                              static_cast<float>(JsonNumber(node, "paddingTop", 0.0)),
                              static_cast<float>(JsonNumber(node, "paddingRight", 0.0)),
                              static_cast<float>(JsonNumber(node, "paddingBottom", 0.0))));
      }

      if (JsonBool(node, "clipsContent", false) && pType != plGetStaticRTTI<plUiStackPanel>())
        pIr->Set("ClipChildren", true);

      for (const plVariant& childVar : *pChildren)
      {
        if (childVar.IsA<plVariantDictionary>())
          BuildFigmaNode(childVar.Get<plVariantDictionary>(), ctx, *pIr, contentRect, bAutoLayout, sPath);
      }
    }
  }

  /// One top-level frame -> one import document.
  void BuildFigmaFrameDocument(const plVariantDictionary& frame, FigmaContext& ctx, plStringView sTargetFolderAbs, plUiImportDocument& out_importDoc)
  {
    const plStringView sName = JsonString(frame, "name", "Frame");

    plStringBuilder sFileName = sName;
    // sanitize for the file system
    sFileName.ReplaceAll("/", "_");
    sFileName.ReplaceAll("\\", "_");
    sFileName.ReplaceAll(":", "_");
    sFileName.ReplaceAll("*", "_");
    sFileName.ReplaceAll("?", "_");
    sFileName.ReplaceAll("\"", "_");
    sFileName.ReplaceAll(" ", "");

    plStringBuilder sTargetPath = sTargetFolderAbs;
    sTargetPath.AppendPath(sFileName);
    sTargetPath.Append(".plUiDocumentAsset");

    out_importDoc.m_sTargetPathAbs = sTargetPath;
    out_importDoc.m_Root.m_sName = sName;

    const plRectFloat frameRect = FigmaBounds(frame, plRectFloat(0, 0, 1920, 1080));
    out_importDoc.m_vReferenceSize = plVec2(frameRect.width, frameRect.height);

    plStringBuilder sPath = sName;

    // the frame's own background becomes a full-stretch panel
    {
      plUiImportNode* pBackground = out_importDoc.m_Root.AddChild(plGetStaticRTTI<plUiPanel>(), "Background");
      pBackground->m_vAnchorMin = plVec2::MakeZero();
      pBackground->m_vAnchorMax = plVec2(1.0f, 1.0f);
      pBackground->m_vOffsets = plVec4::MakeZero();
      ApplyFigmaVisuals(frame, *pBackground, ctx, sPath, true);
    }

    const bool bAutoLayout = JsonString(frame, "layoutMode") == "HORIZONTAL" || JsonString(frame, "layoutMode") == "VERTICAL";
    if (bAutoLayout)
      ctx.m_pReport->Info(sPath, "The top-level frame's auto-layout applies only to a nested stack; children import anchored.");

    if (const plVariantArray* pChildren = JsonArray(frame, "children"))
    {
      for (const plVariant& childVar : *pChildren)
      {
        if (childVar.IsA<plVariantDictionary>())
          BuildFigmaNode(childVar.Get<plVariantDictionary>(), ctx, out_importDoc.m_Root, frameRect, false, sPath);
      }
    }
  }

  struct FigmaFrameEntry
  {
    plString m_sPageName;
    const plVariantDictionary* m_pFrame = nullptr;
  };

  /// Collects the top-level frames of all pages.
  void CollectTopLevelFrames(const plVariantDictionary& fileRoot, plDynamicArray<FigmaFrameEntry>& out_frames)
  {
    const plVariantDictionary* pDocument = JsonDict(fileRoot, "document");
    if (pDocument == nullptr)
      return;

    const plVariantArray* pPages = JsonArray(*pDocument, "children");
    if (pPages == nullptr)
      return;

    for (const plVariant& pageVar : *pPages)
    {
      if (!pageVar.IsA<plVariantDictionary>())
        continue;

      const plVariantDictionary& page = pageVar.Get<plVariantDictionary>();
      const plString sPageName = JsonString(page, "name", "Page");

      const plVariantArray* pFrames = JsonArray(page, "children");
      if (pFrames == nullptr)
        continue;

      for (const plVariant& frameVar : *pFrames)
      {
        if (!frameVar.IsA<plVariantDictionary>())
          continue;

        const plVariantDictionary& frame = frameVar.Get<plVariantDictionary>();
        if (IsContainerType(JsonString(frame, "type")))
        {
          FigmaFrameEntry& entry = out_frames.ExpandAndGetRef();
          entry.m_sPageName = sPageName;
          entry.m_pFrame = &frame;
        }
      }
    }
  }

  plResult ParseJsonFile(plStringView sFileAbs, plVariantDictionary& out_root)
  {
    plFileReader file;
    if (file.Open(sFileAbs).Failed())
      return PL_FAILURE;

    plJSONReader reader;
    if (reader.Parse(file).Failed())
      return PL_FAILURE;

    out_root = reader.GetTopLevelObject();
    return PL_SUCCESS;
  }
} // namespace

// ═══════════════════════════════════════════════════════════════════════════════════════
// Offline generator (*.fig.json)
// ═══════════════════════════════════════════════════════════════════════════════════════

plUiFigmaDocumentGenerator::plUiFigmaDocumentGenerator()
{
  AddSupportedFileType("json");
}

plUiFigmaDocumentGenerator::~plUiFigmaDocumentGenerator() = default;

void plUiFigmaDocumentGenerator::GetImportModes(plStringView sAbsInputFile, plDynamicArray<plAssetDocumentGenerator::ImportMode>& out_modes) const
{
  // only offer this for *.fig.json dumps (plain .json files are almost never Figma files)
  if (!sAbsInputFile.IsEmpty() && !sAbsInputFile.EndsWith_NoCase(".fig.json"))
    return;

  plAssetDocumentGenerator::ImportMode& info = out_modes.ExpandAndGetRef();
  info.m_Priority = plAssetDocGeneratorPriority::DefaultPriority;
  info.m_sName = "FigmaToUiDocument";
  info.m_sIcon = ":/AssetIcons/GameUi.svg";
}

plStatus plUiFigmaDocumentGenerator::Generate(plStringView sInputFileAbs, plStringView sMode, plDynamicArray<plDocument*>& out_generatedDocuments)
{
  PL_IGNORE_UNUSED(sMode);

  plVariantDictionary fileRoot;
  if (ParseJsonFile(sInputFileAbs, fileRoot).Failed())
    return plStatus(plFmt("Could not read '{}' as JSON.", sInputFileAbs));

  plDynamicArray<FigmaFrameEntry> frames;
  CollectTopLevelFrames(fileRoot, frames);

  if (frames.IsEmpty())
    return plStatus("The JSON contains no importable frames (expected a Figma GET /v1/files response).");

  plUiImportReport report;
  plUiImportWriter writer(report);

  FigmaContext ctx;
  ctx.m_pReport = &report;
  ctx.m_bOffline = true;

  plStringBuilder sTargetFolder = sInputFileAbs;
  sTargetFolder.PathParentDirectory();

  plStatus lastStatus(PL_SUCCESS);
  plUInt32 uiImported = 0;

  for (const auto& frameEntry : frames)
  {
    plUiImportDocument importDoc;
    BuildFigmaFrameDocument(*frameEntry.m_pFrame, ctx, sTargetFolder, importDoc);

    const plStatus status = writer.WriteDocument(importDoc, out_generatedDocuments);
    if (status.Failed())
    {
      report.Warning(importDoc.m_Root.m_sName, status.m_sMessage);
      lastStatus = status;
    }
    else
    {
      ++uiImported;
    }
  }

  plStringBuilder sTitle;
  sTitle.SetFormat("Figma Import — {}", plPathUtils::GetFileNameAndExtension(sInputFileAbs));
  plUiImportShowReport(report, sTitle);

  if (uiImported == 0)
    return lastStatus;

  return plStatus(PL_SUCCESS);
}

// ═══════════════════════════════════════════════════════════════════════════════════════
// REST import dialog
// ═══════════════════════════════════════════════════════════════════════════════════════

namespace
{
  QByteArray FetchUrl(QNetworkAccessManager& nam, const QString& sUrl, const QString& sToken, QString& out_sError)
  {
    QNetworkRequest request{QUrl(sUrl)};
    if (!sToken.isEmpty())
      request.setRawHeader("X-Figma-Token", sToken.toUtf8());

    QNetworkReply* pReply = nam.get(request);

    QEventLoop loop;
    QObject::connect(pReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray data;
    if (pReply->error() != QNetworkReply::NoError)
      out_sError = pReply->errorString();
    else
      data = pReply->readAll();

    pReply->deleteLater();
    return data;
  }

  QString ExtractFigmaFileKey(QString sUrl)
  {
    sUrl = sUrl.trimmed();

    for (const QString& sMarker : {QStringLiteral("/file/"), QStringLiteral("/design/")})
    {
      const int iPos = sUrl.indexOf(sMarker);
      if (iPos >= 0)
      {
        QString sKey = sUrl.mid(iPos + sMarker.length());
        const int iSlash = sKey.indexOf('/');
        if (iSlash >= 0)
          sKey = sKey.left(iSlash);
        const int iQuery = sKey.indexOf('?');
        if (iQuery >= 0)
          sKey = sKey.left(iQuery);
        return sKey;
      }
    }

    return sUrl; // assume a raw key
  }

  plResult ParseJsonBytes(const QByteArray& bytes, plVariantDictionary& out_root)
  {
    if (bytes.isEmpty())
      return PL_FAILURE;

    plRawMemoryStreamReader memoryReader(bytes.constData(), bytes.size());

    plJSONReader reader;
    if (reader.Parse(memoryReader).Failed())
      return PL_FAILURE;

    out_root = reader.GetTopLevelObject();
    return PL_SUCCESS;
  }

  void CollectImageRefsAndVectors(const plVariantDictionary& node, plSet<plString>& ref_imageRefs, plSet<plString>& ref_vectorIds)
  {
    const plStringView sType = JsonString(node, "type");

    if (IsVectorType(sType) && JsonBool(node, "visible", true))
    {
      // nodes with real fill geometry import as vectors - no server render needed
      const plVariantArray* pFillGeometry = JsonArray(node, "fillGeometry");
      if (pFillGeometry == nullptr || pFillGeometry->IsEmpty())
        ref_vectorIds.Insert(plString(JsonString(node, "id")));
    }

    if (const plVariantArray* pFills = JsonArray(node, "fills"))
    {
      for (const plVariant& fillVar : *pFills)
      {
        if (!fillVar.IsA<plVariantDictionary>())
          continue;
        const plVariantDictionary& fill = fillVar.Get<plVariantDictionary>();
        if (JsonBool(fill, "visible", true) && JsonString(fill, "type") == "IMAGE")
          ref_imageRefs.Insert(plString(JsonString(fill, "imageRef")));
      }
    }

    if (const plVariantArray* pChildren = JsonArray(node, "children"))
    {
      for (const plVariant& childVar : *pChildren)
      {
        if (childVar.IsA<plVariantDictionary>())
          CollectImageRefsAndVectors(childVar.Get<plVariantDictionary>(), ref_imageRefs, ref_vectorIds);
      }
    }
  }

  QString SanitizeFileName(QString s)
  {
    for (const QChar c : {QChar(':'), QChar(';'), QChar('/'), QChar('\\'), QChar('?'), QChar('*'), QChar('"'), QChar('<'), QChar('>'), QChar('|')})
      s.replace(c, QChar('_'));
    return s;
  }

  /// Downloads a png to <folder>/FigmaImages/<name>.png; returns the absolute path or empty.
  plString DownloadPng(QNetworkAccessManager& nam, const QString& sUrl, const QString& sTargetFolder, const QString& sFileBaseName)
  {
    QString sError;
    const QByteArray bytes = FetchUrl(nam, sUrl, {}, sError); // image URLs are pre-signed, no token
    if (bytes.isEmpty())
      return {};

    plStringBuilder sDir = sTargetFolder.toUtf8().constData();
    sDir.AppendPath("FigmaImages");
    if (plOSFile::CreateDirectoryStructure(sDir).Failed())
      return {};

    plStringBuilder sFile = sDir;
    sFile.AppendPath(SanitizeFileName(sFileBaseName).toUtf8().constData());
    sFile.Append(".png");

    plOSFile file;
    if (file.Open(sFile, plFileOpenMode::Write).Failed())
      return {};

    file.Write(bytes.constData(), bytes.size()).IgnoreResult();
    file.Close();

    return plString(sFile.GetView());
  }
} // namespace

void plUiOpenFigmaImportDialog()
{
  QDialog dlg(QApplication::activeWindow());
  dlg.setWindowTitle("Import UI from Figma");
  dlg.resize(640, 520);

  QSettings settings;

  QVBoxLayout* pLayout = new QVBoxLayout(&dlg);

  QGridLayout* pForm = new QGridLayout();
  pForm->addWidget(new QLabel("File URL or key:", &dlg), 0, 0);
  QLineEdit* pUrlEdit = new QLineEdit(&dlg);
  pUrlEdit->setPlaceholderText("https://www.figma.com/file/<key>/... or the raw file key");
  pUrlEdit->setText(settings.value("FigmaImport/LastUrl").toString());
  pForm->addWidget(pUrlEdit, 0, 1);

  pForm->addWidget(new QLabel("Access token:", &dlg), 1, 0);
  QLineEdit* pTokenEdit = new QLineEdit(&dlg);
  pTokenEdit->setEchoMode(QLineEdit::Password);
  pTokenEdit->setPlaceholderText("figd_... (Figma > Settings > Personal access tokens)");
  pTokenEdit->setText(settings.value("FigmaImport/Token").toString());
  pForm->addWidget(pTokenEdit, 1, 1);

  pForm->addWidget(new QLabel("Output folder:", &dlg), 2, 0);
  QHBoxLayout* pFolderRow = new QHBoxLayout();
  QLineEdit* pFolderEdit = new QLineEdit(&dlg);
  {
    QString sDefault = settings.value("FigmaImport/LastFolder").toString();
    if (sDefault.isEmpty() && plToolsProject::GetSingleton() != nullptr && plToolsProject::GetSingleton()->IsProjectOpen())
      sDefault = QString::fromUtf8(plToolsProject::GetSingleton()->GetProjectDirectory().GetData());
    pFolderEdit->setText(sDefault);
  }
  QPushButton* pBrowse = new QPushButton("...", &dlg);
  pFolderRow->addWidget(pFolderEdit, 1);
  pFolderRow->addWidget(pBrowse);
  pForm->addLayout(pFolderRow, 2, 1);

  pLayout->addLayout(pForm);

  QPushButton* pFetch = new QPushButton("Fetch Frames", &dlg);
  pLayout->addWidget(pFetch);

  QTreeWidget* pFrameTree = new QTreeWidget(&dlg);
  pFrameTree->setColumnCount(1);
  pFrameTree->setHeaderLabels({"Frames (check what to import)"});
  pLayout->addWidget(pFrameTree, 1);

  QLabel* pStatus = new QLabel(&dlg);
  pStatus->setWordWrap(true);
  pLayout->addWidget(pStatus);

  QDialogButtonBox* pButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
  QPushButton* pImport = pButtons->addButton("Import Selected", QDialogButtonBox::AcceptRole);
  pImport->setEnabled(false);
  pLayout->addWidget(pButtons);

  QNetworkAccessManager nam;

  // parsed file data shared between Fetch and Import
  auto pFileRoot = std::make_shared<plVariantDictionary>();
  auto pFrames = std::make_shared<plDynamicArray<FigmaFrameEntry>>();

  QObject::connect(pBrowse, &QPushButton::clicked, [&]()
    {
      const QString sDir = QFileDialog::getExistingDirectory(&dlg, "Choose Output Folder", pFolderEdit->text());
      if (!sDir.isEmpty())
        pFolderEdit->setText(sDir);
    });

  QObject::connect(pFetch, &QPushButton::clicked, [&]()
    {
      pFrameTree->clear();
      pImport->setEnabled(false);
      pFileRoot->Clear();
      pFrames->Clear();

      const QString sKey = ExtractFigmaFileKey(pUrlEdit->text());
      const QString sToken = pTokenEdit->text().trimmed();

      if (sKey.isEmpty() || sToken.isEmpty())
      {
        pStatus->setText("Enter both a file URL/key and an access token.");
        return;
      }

      pStatus->setText("Fetching file...");
      QApplication::setOverrideCursor(Qt::WaitCursor);

      QString sError;
      // geometry=paths includes vector fill geometry, so shapes import as real vectors
      const QByteArray bytes = FetchUrl(nam, QString("https://api.figma.com/v1/files/%1?geometry=paths").arg(sKey), sToken, sError);

      QApplication::restoreOverrideCursor();

      if (!sError.isEmpty())
      {
        pStatus->setText(QString("Request failed: %1").arg(sError));
        return;
      }

      if (ParseJsonBytes(bytes, *pFileRoot).Failed())
      {
        pStatus->setText("The response could not be parsed (invalid token or file key?).");
        return;
      }

      CollectTopLevelFrames(*pFileRoot, *pFrames);

      if (pFrames->IsEmpty())
      {
        pStatus->setText("The file contains no top-level frames.");
        return;
      }

      QMap<QString, QTreeWidgetItem*> pageItems;
      plInt32 iFrameIndex = 0;

      for (const auto& frameEntry : *pFrames)
      {
        const QString sPage = QString::fromUtf8(frameEntry.m_sPageName.GetData());
        QTreeWidgetItem*& pPageItem = pageItems[sPage];
        if (pPageItem == nullptr)
        {
          pPageItem = new QTreeWidgetItem(pFrameTree);
          pPageItem->setText(0, sPage);
          pPageItem->setExpanded(true);
          pPageItem->setFlags(Qt::ItemIsEnabled);
        }

        QTreeWidgetItem* pFrameItem = new QTreeWidgetItem(pPageItem);
        pFrameItem->setText(0, QString::fromUtf8(plStringBuilder(JsonString(*frameEntry.m_pFrame, "name", "Frame")).GetData()));
        pFrameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        pFrameItem->setCheckState(0, Qt::Checked);
        pFrameItem->setData(0, Qt::UserRole, iFrameIndex);
        ++iFrameIndex;
      }

      pStatus->setText(QString("%1 frame(s) found. Uncheck what you don't need, then Import.").arg(iFrameIndex));
      pImport->setEnabled(true);

      settings.setValue("FigmaImport/LastUrl", pUrlEdit->text());
      settings.setValue("FigmaImport/Token", pTokenEdit->text());
    });

  QObject::connect(pButtons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  QObject::connect(pImport, &QPushButton::clicked, [&]()
    {
      const QString sFolder = pFolderEdit->text().trimmed();
      if (sFolder.isEmpty())
      {
        pStatus->setText("Choose an output folder inside the project first.");
        return;
      }

      settings.setValue("FigmaImport/LastFolder", sFolder);

      // gather checked frame indices
      plDynamicArray<plInt32> checkedFrames;
      for (QTreeWidgetItemIterator it(pFrameTree); *it != nullptr; ++it)
      {
        QTreeWidgetItem* pItem = *it;
        if (pItem->flags().testFlag(Qt::ItemIsUserCheckable) && pItem->checkState(0) == Qt::Checked)
          checkedFrames.PushBack(pItem->data(0, Qt::UserRole).toInt());
      }

      if (checkedFrames.IsEmpty())
      {
        pStatus->setText("No frames checked.");
        return;
      }

      const QString sKey = ExtractFigmaFileKey(pUrlEdit->text());
      const QString sToken = pTokenEdit->text().trimmed();

      QApplication::setOverrideCursor(Qt::WaitCursor);

      plUiImportReport report;
      plUiImportWriter writer(report);

      FigmaContext ctx;
      ctx.m_pReport = &report;
      ctx.m_bOffline = false;

      // collect image refs + vector nodes of the checked frames
      plSet<plString> imageRefs, vectorIds;
      for (plInt32 iFrame : checkedFrames)
        CollectImageRefsAndVectors(*(*pFrames)[iFrame].m_pFrame, imageRefs, vectorIds);

      QString sError;

      // image fills: GET /v1/files/:key/images -> { meta: { images: { ref: url } } }
      if (!imageRefs.IsEmpty())
      {
        pStatus->setText("Downloading image fills...");
        QApplication::processEvents();

        const QByteArray bytes = FetchUrl(nam, QString("https://api.figma.com/v1/files/%1/images").arg(sKey), sToken, sError);

        plVariantDictionary imagesRoot;
        if (ParseJsonBytes(bytes, imagesRoot).Succeeded())
        {
          if (const plVariantDictionary* pMeta = JsonDict(imagesRoot, "meta"))
          {
            if (const plVariantDictionary* pImages = JsonDict(*pMeta, "images"))
            {
              for (const plString& sRef : imageRefs)
              {
                const plStringView sUrl = JsonString(*pImages, sRef);
                if (sUrl.IsEmpty())
                  continue;

                const plString sFile = DownloadPng(nam, QString::fromUtf8(plStringBuilder(sUrl).GetData()), sFolder, QString::fromUtf8(sRef.GetData()));
                ctx.m_ImageFiles[sRef] = sFile;
              }
            }
          }
        }
      }

      // vector nodes: GET /v1/images/:key?ids=...&format=png&scale=2 -> { images: { id: url } }
      if (!vectorIds.IsEmpty())
      {
        pStatus->setText("Rendering vector shapes...");
        QApplication::processEvents();

        QString sIds;
        for (const plString& sId : vectorIds)
        {
          if (!sIds.isEmpty())
            sIds += ",";
          sIds += QString::fromUtf8(sId.GetData());
        }

        const QByteArray bytes = FetchUrl(nam, QString("https://api.figma.com/v1/images/%1?ids=%2&format=png&scale=2").arg(sKey, sIds), sToken, sError);

        plVariantDictionary rendersRoot;
        if (ParseJsonBytes(bytes, rendersRoot).Succeeded())
        {
          if (const plVariantDictionary* pImages = JsonDict(rendersRoot, "images"))
          {
            for (const plString& sId : vectorIds)
            {
              const plStringView sUrl = JsonString(*pImages, sId);
              if (sUrl.IsEmpty())
                continue;

              QString sBase = QString("vec_%1").arg(QString::fromUtf8(sId.GetData()));
              const plString sFile = DownloadPng(nam, QString::fromUtf8(plStringBuilder(sUrl).GetData()), sFolder, sBase);
              ctx.m_VectorFiles[sId] = sFile;
            }
          }
        }
      }

      // import the frames
      pStatus->setText("Importing frames...");
      QApplication::processEvents();

      plStringBuilder sFolderPl = sFolder.toUtf8().constData();

      plDynamicArray<plDocument*> generatedDocs;
      plString sFirstDocPath;

      for (plInt32 iFrame : checkedFrames)
      {
        plUiImportDocument importDoc;
        BuildFigmaFrameDocument(*(*pFrames)[iFrame].m_pFrame, ctx, sFolderPl, importDoc);

        const plStatus status = writer.WriteDocument(importDoc, generatedDocs);
        if (status.Failed())
          report.Warning(importDoc.m_Root.m_sName, status.m_sMessage);
        else if (sFirstDocPath.IsEmpty())
          sFirstDocPath = importDoc.m_sTargetPathAbs;
      }

      // save + close like the asset-import framework does
      for (plDocument* pDoc : generatedDocs)
      {
        pDoc->SaveDocument(true).LogFailure();
        pDoc->GetDocumentManager()->CloseDocument(pDoc);
      }

      QApplication::restoreOverrideCursor();

      plStringBuilder sTitle;
      sTitle.SetFormat("Figma Import — {} frame(s)", checkedFrames.GetCount());
      plUiImportShowReport(report, sTitle);

      if (!sFirstDocPath.IsEmpty())
        plQtEditorApp::GetSingleton()->OpenDocumentQueued(sFirstDocPath);

      dlg.accept();
    });

  dlg.exec();
}

// ═══════════════════════════════════════════════════════════════════════════════════════

plUiFigmaImportAction::plUiFigmaImportAction(const plActionContext& context, const char* szName, plInt32 iUnused)
  : plButtonAction(context, szName, false, "")
{
  PL_IGNORE_UNUSED(iUnused);
}

void plUiFigmaImportAction::Execute(const plVariant& value)
{
  PL_IGNORE_UNUSED(value);
  plUiOpenFigmaImportDialog();
}
