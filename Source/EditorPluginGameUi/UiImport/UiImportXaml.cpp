#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiImport/UiImportWriter.h>
#include <EditorPluginGameUi/UiImport/UiImportXaml.h>
#include <EditorPluginGameUi/UiImport/UiMarkupParser.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <GameUiPlugin/Elements/UiAnimationClip.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <Texture/Image/Image.h>
#include <ToolsFoundation/Document/Document.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiXamlDocumentGenerator, 1, plRTTIDefaultAllocator<plUiXamlDocumentGenerator>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiXamlDocumentGenerator::plUiXamlDocumentGenerator()
{
  AddSupportedFileType("xaml");
}

plUiXamlDocumentGenerator::~plUiXamlDocumentGenerator() = default;

void plUiXamlDocumentGenerator::GetImportModes(plStringView sAbsInputFile, plDynamicArray<plAssetDocumentGenerator::ImportMode>& out_modes) const
{
  plAssetDocumentGenerator::ImportMode& info = out_modes.ExpandAndGetRef();
  info.m_Priority = plAssetDocGeneratorPriority::DefaultPriority;
  info.m_sName = "XamlToUiDocument";
  info.m_sIcon = ":/AssetIcons/GameUi.svg";
}

namespace
{
  struct XamlStyle
  {
    plString m_sTargetType;
    plString m_sBasedOn; // resource key
    plHybridArray<plUiMarkupNode::Attribute, 8> m_Setters;
  };

  struct XamlGradientStop
  {
    plString m_sColor;
    float m_fOffset = 0.0f;
  };

  struct XamlGradientBrush
  {
    plHybridArray<XamlGradientStop, 8> m_Stops;
    plInt32 m_iMode = plUiGradientMode::Vertical;
    bool m_bRadial = false;
    bool m_bAbsoluteOrRepeated = false;
  };

  struct XamlFlowContract
  {
    plString m_sEntry;
    plString m_sValue;
    plString m_sEnterClip;
    plString m_sExitClip;
    float m_fExitDuration = 0.0f;
    plHybridArray<plString, 8> m_Commands;
    bool m_bConflict = false;

    bool IsValid() const { return !m_bConflict && !m_sEntry.IsEmpty() && !m_sValue.IsEmpty(); }
  };

  struct XamlSubDocumentPlacement
  {
    plUiImportNode* m_pNode = nullptr;
    plString m_sSourcePath;
    bool m_bInitiallyVisible = true;
  };

  struct XamlContext
  {
    plUiImportReport* m_pReport = nullptr;
    plString m_sFolderAbs;
    plString m_sMainName;
    plMap<plString, plString> m_Brushes;                  // resource key -> color value string (brushes AND <Color> resources)
    plMap<plString, XamlGradientBrush> m_GradientBrushes; // resource key -> two/multi-stop gradient definition
    plMap<plString, XamlStyle> m_Styles;                  // resource key or "type:<TargetType>" -> style
    plMap<plString, float> m_TemplateContentWidths;       // ControlTemplate key -> fixed content-column width hint
    plMap<plString, plString> m_SubDocs;                  // absolute xaml path -> imported doc guid
    plMap<plString, plVec2> m_SubDocReferenceSizes;       // absolute xaml path -> design/reference size
    plMap<plString, XamlFlowContract> m_SubDocFlows;      // absolute xaml path -> inferred navigation contract
    plSet<plString> m_LoadedDictionaries;                 // absolute paths of resource-dictionary files already loaded
    plMap<plString, plString> m_FontFiles;                // FontFamily value -> resolved .ttf/.otf path ("" = no match)
    plString m_sDefaultFontFile;                          // resolved from the root element's FontFamily
    plMap<plString, plString> m_ImageSources;             // ImageSource/BitmapImage/ImageBrush resource key -> image path
    plMap<plString, plString> m_FontResources;            // <FontFamily x:Key> resource key -> family value ("Fonts/#Name")
    plMap<plString, plString> m_GeometryResources;        // <StreamGeometry/PathGeometry x:Key> resource key -> path data
    plUiImportWriter* m_pWriter = nullptr;
    plDynamicArray<plDocument*>* m_pOutDocs = nullptr;
    plUiImportDocument* m_pCurrentDocument = nullptr;
    plHybridArray<XamlSubDocumentPlacement, 16>* m_pCurrentPlacements = nullptr;
    plInt32 m_iDocCounter = 0;
    plInt32 m_iDepth = 0;
  };

  enum class PlacementMode
  {
    Free,     ///< alignment + margin -> anchors (single-cell Grid, root, Border content)
    Stack,    ///< parent is a StackPanel/ScrollView: preferred size only
    Canvas,   ///< Canvas.Left/Top + Width/Height -> point anchors
    GridCell, ///< uniform grid: children stretch into their cells
  };

  struct MarkupExtension
  {
    enum class Kind
    {
      None, ///< a plain value
      Binding,
      StaticResource,
      Other,
    };

    Kind m_Kind = Kind::None;
    plString m_sArg;
    plString m_sElementName; ///< ElementName= of a Binding, when present
  };

  MarkupExtension ParseMarkupExtension(plStringView sValue)
  {
    MarkupExtension result;

    plStringBuilder sVal = sValue;
    sVal.Trim(" \t");

    if (!sVal.StartsWith("{"))
      return result;

    sVal.Trim("{}");
    sVal.Trim(" \t");

    if (sVal.StartsWith("Binding"))
    {
      result.m_Kind = MarkupExtension::Kind::Binding;

      plStringBuilder sArgs = sVal;
      sArgs.TrimWordStart("Binding");
      sArgs.Trim(" \t");

      plHybridArray<plStringView, 8> parts;
      sArgs.Split(false, parts, ",");

      for (plStringView sPart : parts)
      {
        plStringBuilder sP = sPart;
        sP.Trim(" \t");

        if (sP.StartsWith("Path="))
        {
          sP.TrimWordStart("Path=");
          result.m_sArg = sP;
          continue;
        }

        if (sP.StartsWith("ElementName="))
        {
          sP.TrimWordStart("ElementName=");
          result.m_sElementName = sP;
          continue;
        }

        if (sP.FindSubString("=") == nullptr && result.m_sArg.IsEmpty())
        {
          result.m_sArg = sP; // first positional token is the path
        }
      }
    }
    else if (sVal.StartsWith("StaticResource") || sVal.StartsWith("DynamicResource"))
    {
      result.m_Kind = MarkupExtension::Kind::StaticResource;

      plStringBuilder sArg = sVal;
      sArg.TrimWordStart("StaticResource");
      sArg.TrimWordStart("DynamicResource");
      sArg.Trim(" \t");
      result.m_sArg = sArg;
    }
    else
    {
      result.m_Kind = MarkupExtension::Kind::Other;
      result.m_sArg = sVal;
    }

    return result;
  }

  /// Resolves an attribute value that may be a color, a {StaticResource brushKey} or a gradient
  /// value string. Resource values may reference further resources (SolidColorBrush whose Color
  /// is itself a {StaticResource colorKey}), so lookups resolve recursively.
  plResult ResolveBrushColor(plStringView sValue, XamlContext& ctx, plStringView sPath, plColorGammaUB& out_color, plInt32 iDepth = 0)
  {
    if (iDepth > 8)
      return PL_FAILURE;

    MarkupExtension ext = ParseMarkupExtension(sValue);

    if (ext.m_Kind == MarkupExtension::Kind::StaticResource)
    {
      const plString* pBrush = ctx.m_Brushes.GetValue(ext.m_sArg);
      if (pBrush == nullptr)
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Brush resource '{}' not found.", ext.m_sArg);
        ctx.m_pReport->Warning(sPath, sMsg);
        return PL_FAILURE;
      }
      return ResolveBrushColor(pBrush->GetView(), ctx, sPath, out_color, iDepth + 1);
    }

    if (ext.m_Kind != MarkupExtension::Kind::None)
      return PL_FAILURE;

    return plUiImportUtils::ParseColor(sValue, true, out_color);
  }

  /// WPF Thickness: "a" | "l,t" ("h,v") | "l,t,r,b" -> plVec4(l, t, r, b)
  plVec4 ParseThickness(plStringView sValue)
  {
    plHybridArray<plStringView, 4> parts;
    plStringBuilder sVal = sValue;
    sVal.Split(false, parts, ",", " ");

    float v[4] = {0, 0, 0, 0};
    const plUInt32 uiCount = plMath::Min<plUInt32>(parts.GetCount(), 4);

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      double f = 0.0;
      plStringBuilder sP = parts[i];
      sP.Trim(" \t");
      plConversionUtils::StringToFloat(sP, f).IgnoreResult();
      v[i] = static_cast<float>(f);
    }

    if (uiCount == 1)
    {
      v[1] = v[2] = v[3] = v[0];
    }
    else if (uiCount == 2)
    {
      v[2] = v[0];
      v[3] = v[1];
    }

    return plVec4(v[0], v[1], v[2], v[3]);
  }

  float ParseFloatAttr(plStringView sValue, float fFallback)
  {
    double f = fFallback;
    plStringBuilder sVal = sValue;
    sVal.Trim(" \t");
    if (sVal.IsEmpty() || plConversionUtils::StringToFloat(sVal, f).Failed())
      return fFallback;
    return static_cast<float>(f);
  }

  XamlGradientBrush ReadGradientBrush(const plUiMarkupNode& brushNode, bool bRadial)
  {
    XamlGradientBrush gradient;
    gradient.m_bRadial = bRadial;

    if (!bRadial)
    {
      const plVec4 vStart = ParseThickness(brushNode.GetAttribute("StartPoint", "0,0"));
      const plVec4 vEnd = ParseThickness(brushNode.GetAttribute("EndPoint", "1,1"));
      gradient.m_iMode = plMath::Abs(vEnd.x - vStart.x) >= plMath::Abs(vEnd.y - vStart.y) ? plUiGradientMode::Horizontal : plUiGradientMode::Vertical;
    }

    gradient.m_bAbsoluteOrRepeated =
      brushNode.GetAttribute("MappingMode").IsEqual_NoCase("Absolute") ||
      (!brushNode.GetAttribute("SpreadMethod").IsEmpty() && !brushNode.GetAttribute("SpreadMethod").IsEqual_NoCase("Pad"));

    plHybridArray<const plUiMarkupNode*, 8> stops;
    for (const plUiMarkupNode* pChild : brushNode.m_Children)
    {
      if (pChild->GetLocalTag() == "GradientStop")
      {
        stops.PushBack(pChild);
      }
      else if (pChild->GetLocalTag().EndsWith(".GradientStops"))
      {
        for (const plUiMarkupNode* pStop : pChild->m_Children)
        {
          if (pStop->GetLocalTag() == "GradientStop")
            stops.PushBack(pStop);
        }
      }
    }

    for (plUInt32 i = 0; i < stops.GetCount(); ++i)
    {
      XamlGradientStop& stop = gradient.m_Stops.ExpandAndGetRef();
      stop.m_sColor = stops[i]->GetAttribute("Color");
      stop.m_fOffset = ParseFloatAttr(stops[i]->GetAttribute("Offset"), stops.GetCount() > 1 ? static_cast<float>(i) / static_cast<float>(stops.GetCount() - 1) : 0.0f);
    }

    return gradient;
  }

  bool ApplyGradientBrush(plUiImportNode& ref_ir, const XamlGradientBrush& gradient, XamlContext& ctx, plStringView sPath)
  {
    if (gradient.m_Stops.IsEmpty())
      return false;

    plHybridArray<plColorGammaUB, 8> colors;
    for (const XamlGradientStop& stop : gradient.m_Stops)
    {
      plColorGammaUB color;
      if (ResolveBrushColor(stop.m_sColor, ctx, sPath, color).Succeeded())
        colors.PushBack(color);
      else
        return false;
    }

    plUInt32 uiFirst = 0;
    plUInt32 uiLast = 0;
    plUInt32 uiStrongest = 0;
    for (plUInt32 i = 1; i < gradient.m_Stops.GetCount(); ++i)
    {
      if (gradient.m_Stops[i].m_fOffset < gradient.m_Stops[uiFirst].m_fOffset)
        uiFirst = i;
      if (gradient.m_Stops[i].m_fOffset > gradient.m_Stops[uiLast].m_fOffset)
        uiLast = i;
      if (colors[i].a > colors[uiStrongest].a)
        uiStrongest = i;
    }

    if (gradient.m_Stops.GetCount() > 2)
    {
      // A symmetric fade whose endpoint colors match would become invisible or nearly
      // invisible as a two-stop gradient. A flat sample of its strongest stop retains
      // the intended separator/glow much better.
      if (!gradient.m_bRadial && colors[uiFirst] == colors[uiLast])
      {
        ref_ir.Set("Color", colors[uiStrongest]);
        ref_ir.Set("GradientMode", (plInt32)plUiGradientMode::None);
        ctx.m_pReport->Info(sPath, "Symmetric multi-stop gradient approximated by its strongest stop.");
        return true;
      }

      ctx.m_pReport->Info(sPath, "Gradient has more than two stops; its endpoint colors import.");
    }

    ref_ir.Set("Color", colors[uiFirst]);
    ref_ir.Set("GradientColor", colors[uiLast]);
    ref_ir.Set("GradientMode", gradient.m_iMode);

    if (gradient.m_bRadial)
      ctx.m_pReport->Info(sPath, "Radial gradient approximated by a two-color vertical gradient.");
    if (gradient.m_bAbsoluteOrRepeated)
      ctx.m_pReport->Info(sPath, "Absolute/repeated gradient mapping approximated within the element bounds.");

    return true;
  }

  bool ResolveImageFile(plStringView sValue, XamlContext& ctx, plStringView sPath, plStringBuilder& out_sAbsPath, plUInt32 uiResourceDepth = 0)
  {
    if (uiResourceDepth > 8)
    {
      ctx.m_pReport->Warning(sPath, "Image resource references exceed the supported nesting depth.");
      return false;
    }

    const MarkupExtension ext = ParseMarkupExtension(sValue);
    if (ext.m_Kind == MarkupExtension::Kind::StaticResource)
    {
      if (const plString* pResource = ctx.m_ImageSources.GetValue(ext.m_sArg))
        return ResolveImageFile(pResource->GetView(), ctx, sPath, out_sAbsPath, uiResourceDepth + 1);

      plStringBuilder sMsg;
      sMsg.SetFormat("Image resource '{}' not found.", ext.m_sArg);
      ctx.m_pReport->Warning(sPath, sMsg);
      return false;
    }

    if (ext.m_Kind == MarkupExtension::Kind::Binding)
    {
      ctx.m_pReport->Warning(sPath, "Image source bindings are unsupported (textures are not bindable).");
      return false;
    }

    if (ext.m_Kind != MarkupExtension::Kind::None)
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("Image source markup extension '{}' is unsupported.", ext.m_sArg);
      ctx.m_pReport->Warning(sPath, sMsg);
      return false;
    }

    plStringBuilder sImagePath = sValue;
    sImagePath.Trim(" \t\r\n'\"");
    if (sImagePath.IsEmpty())
      return false;

    // WPF/Noesis resource URIs commonly use either
    //   pack://application:,,,/Assembly;component/Images/Foo.png
    // or /Assembly;component/Images/Foo.png. The assembly portion is irrelevant for
    // an external import; resolve the component-relative path beside the XAML file.
    bool bResourceRelative = false;
    if (sImagePath.StartsWith_NoCase("pack://application:,,,/"))
    {
      sImagePath.Shrink(23, 0);
      bResourceRelative = true;
    }
    else if (sImagePath.StartsWith_NoCase("pack://siteoforigin:,,,/"))
    {
      sImagePath.Shrink(24, 0);
      bResourceRelative = true;
    }
    else if (sImagePath.StartsWith_NoCase("file:///"))
    {
      sImagePath.Shrink(7, 0); // retain the Unix root slash
      if (sImagePath.GetElementCount() >= 3 && sImagePath.GetData()[0] == '/' && sImagePath.GetData()[2] == ':')
        sImagePath.Shrink(1, 0); // Windows file:///C:/...
    }

    if (const char* szComponent = sImagePath.FindSubString_NoCase(";component/"))
    {
      plStringBuilder sComponentPath;
      sComponentPath.SetSubString_FromTo(szComponent + 11, sImagePath.GetView().GetEndPointer());
      sImagePath = sComponentPath;
      bResourceRelative = true;
    }

    if (bResourceRelative)
      sImagePath.Trim("/\\");
    sImagePath.MakeCleanPath();

    // Absolute file paths are accepted for authoring tools; ordinary XAML paths are
    // relative to the document that declared them.
    if (sImagePath.IsAbsolutePath() && plOSFile::ExistsFile(sImagePath))
    {
      out_sAbsPath = sImagePath;
      return true;
    }

    out_sAbsPath = ctx.m_sFolderAbs;
    out_sAbsPath.AppendPath(sImagePath);
    out_sAbsPath.MakeCleanPath();
    if (plOSFile::ExistsFile(out_sAbsPath))
      return true;

    plStringBuilder sMsg;
    sMsg.SetFormat("Image source not found: '{}'.", sValue);
    ctx.m_pReport->Warning(sPath, sMsg);
    out_sAbsPath.Clear();
    return false;
  }

  bool ApplyImageSource(plUiImportNode& ref_ir, plStringView sSource, XamlContext& ctx, plStringView sPath)
  {
    plStringBuilder sAbsPath;
    if (!ResolveImageFile(sSource, ctx, sPath, sAbsPath))
      return false;

    ref_ir.m_sTextureFile = sAbsPath;
    ref_ir.Set("BrushMode", (plInt32)plUiBrushMode::Image);
    ref_ir.Set("Color", plColorGammaUB(255, 255, 255, 255));
    return true;
  }

  bool ApplyVisualBrush(plUiImportNode& ref_ir, plStringView sValue, XamlContext& ctx, plStringView sPath)
  {
    const MarkupExtension ext = ParseMarkupExtension(sValue);
    if (ext.m_Kind == MarkupExtension::Kind::StaticResource)
    {
      if (const XamlGradientBrush* pGradient = ctx.m_GradientBrushes.GetValue(ext.m_sArg))
        return ApplyGradientBrush(ref_ir, *pGradient, ctx, sPath);
      if (ctx.m_ImageSources.Contains(ext.m_sArg))
        return ApplyImageSource(ref_ir, sValue, ctx, sPath);
    }

    plColorGammaUB color;
    if (ResolveBrushColor(sValue, ctx, sPath, color).Succeeded())
    {
      ref_ir.Set("Color", color);
      return true;
    }

    return false;
  }

  float ParseKeyTime(plStringView sValue)
  {
    plStringBuilder sTime = sValue;
    sTime.Trim(" \t");
    if (sTime.IsEmpty())
      return 0.0f;

    plHybridArray<plStringView, 3> parts;
    sTime.Split(false, parts, ":");

    double values[3] = {0.0, 0.0, 0.0};
    for (plUInt32 i = 0; i < parts.GetCount(); ++i)
    {
      plConversionUtils::StringToFloat(parts[i], values[i]).IgnoreResult();
    }

    if (parts.GetCount() == 3)
      return static_cast<float>(values[0] * 3600.0 + values[1] * 60.0 + values[2]);
    if (parts.GetCount() == 2)
      return static_cast<float>(values[0] * 60.0 + values[1]);
    return static_cast<float>(values[0]);
  }

  plInt32 ParseAnimationEasing(const plUiMarkupNode& keyNode)
  {
    if (keyNode.GetLocalTag().StartsWith("Linear") || keyNode.GetLocalTag().StartsWith("Discrete"))
      return plUiAnimEasing::Linear;

    for (const plUiMarkupNode* pProperty : keyNode.m_Children)
    {
      if (!pProperty->GetLocalTag().EndsWith(".EasingFunction"))
        continue;

      for (const plUiMarkupNode* pEasing : pProperty->m_Children)
      {
        const plStringView sMode = pEasing->GetAttribute("EasingMode");
        if (sMode == "EaseIn")
          return plUiAnimEasing::EaseIn;
        if (sMode == "EaseOut")
          return plUiAnimEasing::EaseOut;
        if (sMode == "EaseInOut")
          return plUiAnimEasing::EaseInOut;
      }
    }

    return plUiAnimEasing::EaseInOut;
  }

  plInt32 GetAnimationTarget(plStringView sTargetProperty)
  {
    if (sTargetProperty.FindSubString("Opacity") != nullptr)
      return plUiAnimTarget::Opacity;
    if (sTargetProperty.FindSubString("TranslateTransform.X") != nullptr || sTargetProperty.EndsWith(".Translate.X)"))
      return plUiAnimTarget::OffsetX;
    if (sTargetProperty.FindSubString("TranslateTransform.Y") != nullptr || sTargetProperty.EndsWith(".Translate.Y)"))
      return plUiAnimTarget::OffsetY;
    if (sTargetProperty.FindSubString("ScaleTransform.Scale") != nullptr)
      return plUiAnimTarget::Scale;
    if (sTargetProperty.FindSubString("RotateTransform.Angle") != nullptr || sTargetProperty.FindSubString("Rotation") != nullptr)
      return plUiAnimTarget::Rotation;
    if (sTargetProperty.FindSubString("Width") != nullptr)
      return plUiAnimTarget::Width;
    if (sTargetProperty.FindSubString("Height") != nullptr)
      return plUiAnimTarget::Height;
    return -1;
  }

  plUiImportAnimTrack* FindAnimationTrack(plUiImportAnimation& animation, plStringView sElement, plInt32 iTarget)
  {
    for (plUiImportAnimTrack& track : animation.m_Tracks)
    {
      if (track.m_sElement == sElement && track.m_iTarget == iTarget)
        return &track;
    }
    return nullptr;
  }

  bool AnimationKeysMatch(const plUiImportAnimTrack& a, const plUiImportAnimTrack& b)
  {
    if (a.m_Keys.GetCount() != b.m_Keys.GetCount())
      return false;

    for (plUInt32 i = 0; i < a.m_Keys.GetCount(); ++i)
    {
      if (!plMath::IsEqual(a.m_Keys[i].m_fTime, b.m_Keys[i].m_fTime, 0.0001f) ||
          !plMath::IsEqual(a.m_Keys[i].m_fValue, b.m_Keys[i].m_fValue, 0.0001f))
        return false;
    }
    return true;
  }

  void ImportStoryboard(const plUiMarkupNode& storyboardNode, plStringView sName, XamlContext& ctx, plStringView sPath)
  {
    if (ctx.m_pCurrentDocument == nullptr || sName.IsEmpty())
      return;

    plUiImportAnimation animation;
    animation.m_sName = sName;
    animation.m_fDuration = 0.01f;

    bool bHadUnsupportedTracks = false;

    for (const plUiMarkupNode* pAnimation : storyboardNode.m_Children)
    {
      const plStringView sAnimTag = pAnimation->GetLocalTag();
      const plStringView sElement = pAnimation->GetAttribute("Storyboard.TargetName");
      const plStringView sTargetProperty = pAnimation->GetAttribute("Storyboard.TargetProperty");

      if (sAnimTag.FindSubString("DoubleAnimation") == nullptr || sElement.IsEmpty())
      {
        if (sAnimTag.FindSubString("Animation") != nullptr)
          bHadUnsupportedTracks = true;
        continue;
      }

      const plInt32 iTarget = GetAnimationTarget(sTargetProperty);
      if (iTarget < 0)
      {
        bHadUnsupportedTracks = true;
        continue;
      }

      plUiImportAnimTrack importedTrack;
      importedTrack.m_sElement = sElement;
      importedTrack.m_iTarget = iTarget;

      for (const plUiMarkupNode* pKey : pAnimation->m_Children)
      {
        if (pKey->GetLocalTag().FindSubString("KeyFrame") == nullptr)
          continue;

        const plStringView sValue = pKey->GetAttribute("Value");
        if (ParseMarkupExtension(sValue).m_Kind != MarkupExtension::Kind::None)
        {
          bHadUnsupportedTracks = true;
          continue;
        }

        plUiImportAnimKey& key = importedTrack.m_Keys.ExpandAndGetRef();
        key.m_fTime = ParseKeyTime(pKey->GetAttribute("KeyTime"));
        key.m_fValue = ParseFloatAttr(sValue, iTarget == plUiAnimTarget::Opacity || iTarget == plUiAnimTarget::Scale ? 1.0f : 0.0f);
        key.m_iEasing = ParseAnimationEasing(*pKey);
        animation.m_fDuration = plMath::Max(animation.m_fDuration, key.m_fTime);
      }

      // Also support the compact DoubleAnimation From/To/Duration form.
      if (importedTrack.m_Keys.IsEmpty() && (!pAnimation->GetAttribute("From").IsEmpty() || !pAnimation->GetAttribute("To").IsEmpty()))
      {
        const float fDefault = iTarget == plUiAnimTarget::Opacity || iTarget == plUiAnimTarget::Scale ? 1.0f : 0.0f;
        const float fDuration = plMath::Max(ParseKeyTime(pAnimation->GetAttribute("Duration")), 0.01f);

        plUiImportAnimKey& from = importedTrack.m_Keys.ExpandAndGetRef();
        from.m_fValue = ParseFloatAttr(pAnimation->GetAttribute("From"), fDefault);
        from.m_iEasing = plUiAnimEasing::Linear;

        plUiImportAnimKey& to = importedTrack.m_Keys.ExpandAndGetRef();
        to.m_fTime = fDuration;
        to.m_fValue = ParseFloatAttr(pAnimation->GetAttribute("To"), fDefault);
        to.m_iEasing = plUiAnimEasing::EaseInOut;
        animation.m_fDuration = plMath::Max(animation.m_fDuration, fDuration);
      }

      if (importedTrack.m_Keys.IsEmpty())
        continue;

      // XAML commonly authors identical ScaleX and ScaleY tracks. GameUI exposes a
      // single uniform Scale channel, so discard only an exact duplicate.
      if (plUiImportAnimTrack* pExisting = FindAnimationTrack(animation, importedTrack.m_sElement, importedTrack.m_iTarget))
      {
        if (!AnimationKeysMatch(*pExisting, importedTrack))
          bHadUnsupportedTracks = true;
        continue;
      }

      animation.m_Tracks.PushBack(importedTrack);

      if (pAnimation->GetAttribute("RepeatBehavior").IsEqual_NoCase("Forever"))
        animation.m_iLoopMode = plUiClipLoopMode::Loop;
      if (pAnimation->GetAttribute("AutoReverse").IsEqual_NoCase("True"))
        animation.m_iLoopMode = plUiClipLoopMode::PingPong;
    }

    if (storyboardNode.GetAttribute("RepeatBehavior").IsEqual_NoCase("Forever"))
      animation.m_iLoopMode = plUiClipLoopMode::Loop;
    if (storyboardNode.GetAttribute("AutoReverse").IsEqual_NoCase("True"))
      animation.m_iLoopMode = plUiClipLoopMode::PingPong;

    if (!animation.m_Tracks.IsEmpty())
    {
      // Last definition wins, matching XAML resource lookup behavior.
      for (plUInt32 i = 0; i < ctx.m_pCurrentDocument->m_Animations.GetCount(); ++i)
      {
        if (ctx.m_pCurrentDocument->m_Animations[i].m_sName == sName)
        {
          ctx.m_pCurrentDocument->m_Animations[i] = animation;
          return;
        }
      }

      ctx.m_pCurrentDocument->m_Animations.PushBack(animation);

      plStringBuilder sMsg;
      sMsg.SetFormat("Storyboard '{}' imported as a GameUI animation clip with {} track(s).", sName, animation.m_Tracks.GetCount());
      if (bHadUnsupportedTracks)
        sMsg.Append(" Unsupported visibility, enabled-state, color, or non-uniform-scale tracks were skipped.");
      ctx.m_pReport->Info(sPath, sMsg);
    }
    else
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("Storyboard '{}' has no channels supported by GameUI animation clips.", sName);
      ctx.m_pReport->Info(sPath, sMsg);
    }
  }

  bool IsPropertyElement(const plUiMarkupNode& node)
  {
    return node.GetLocalTag().FindSubString(".") != nullptr;
  }

  float FindTemplateContentWidth(const plUiMarkupNode& node)
  {
    // HeaderedContentControl templates generally place a ContentPresenter in a fixed
    // final Grid column. Retain the last explicit pixel column as a useful layout hint;
    // Auto/star definitions deliberately do not count.
    float fLastFixedWidth = -1.0f;

    if (node.GetLocalTag() == "ColumnDefinition")
    {
      plStringBuilder sWidth = node.GetAttribute("Width");
      sWidth.Trim(" \t");
      if (!sWidth.IsEmpty() && !sWidth.EndsWith("*") && !sWidth.IsEqual_NoCase("Auto"))
        fLastFixedWidth = ParseFloatAttr(sWidth, -1.0f);
    }

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      const float fChildWidth = FindTemplateContentWidth(*pChild);
      if (fChildWidth > 0.0f)
        fLastFixedWidth = fChildWidth;
    }

    return fLastFixedWidth;
  }

  /// Effective attribute lookup: element attribute wins, then flattened style setters
  /// (explicit Style= first, then the implicit per-type style).
  struct EffectiveAttributes
  {
    const plUiMarkupNode* m_pNode = nullptr;
    const XamlStyle* m_pExplicitStyle = nullptr;
    const XamlStyle* m_pImplicitStyle = nullptr;
    XamlContext* m_pCtx = nullptr;

    plStringView Get(plStringView sName, plStringView sFallback = {}) const
    {
      if (const plString* pValue = m_pNode->FindAttribute(sName))
        return pValue->GetView();

      for (const XamlStyle* pStyle : {m_pExplicitStyle, m_pImplicitStyle})
      {
        const XamlStyle* pCur = pStyle;
        plInt32 iGuard = 0;
        while (pCur != nullptr && iGuard++ < 8)
        {
          for (const plUiMarkupNode::Attribute& setter : pCur->m_Setters)
          {
            if (setter.m_sName.IsEqual_NoCase(sName))
              return setter.m_sValue.GetView();
          }
          pCur = pCur->m_sBasedOn.IsEmpty() ? nullptr : m_pCtx->m_Styles.GetValue(pCur->m_sBasedOn);
        }
      }

      return sFallback;
    }
  };

  EffectiveAttributes MakeEffective(const plUiMarkupNode& node, XamlContext& ctx, plStringView sPath)
  {
    EffectiveAttributes eff;
    eff.m_pNode = &node;
    eff.m_pCtx = &ctx;

    const plString* pStyleAttr = node.FindAttribute("Style");
    if (pStyleAttr != nullptr)
    {
      MarkupExtension ext = ParseMarkupExtension(*pStyleAttr);
      if (ext.m_Kind == MarkupExtension::Kind::StaticResource)
      {
        eff.m_pExplicitStyle = ctx.m_Styles.GetValue(ext.m_sArg);
        if (eff.m_pExplicitStyle == nullptr)
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("Style resource '{}' not found.", ext.m_sArg);
          ctx.m_pReport->Warning(sPath, sMsg);
        }
      }
    }

    plStringBuilder sImplicitKey;
    sImplicitKey.SetFormat("type:{}", node.GetLocalTag());
    eff.m_pImplicitStyle = ctx.m_Styles.GetValue(plString(sImplicitKey.GetView()));

    return eff;
  }

  // forward
  void LoadResourceDictionaryFile(plStringView sSource, XamlContext& ctx, plStringView sPath);

  void CollectResources(const plUiMarkupNode& resourcesNode, XamlContext& ctx, plStringView sPath)
  {
    // a dictionary node may itself reference a file (<ResourceDictionary Source="..."/>)
    {
      const plStringView sSource = resourcesNode.GetAttribute("Source");
      if (!sSource.IsEmpty())
        LoadResourceDictionaryFile(sSource, ctx, sPath);
    }

    for (const plUiMarkupNode* pRes : resourcesNode.m_Children)
    {
      const plStringView sTag = pRes->GetLocalTag();
      const plStringView sKey = pRes->GetAttribute("x:Key");

      if (sTag == "ResourceDictionary" || sTag.EndsWith(".MergedDictionaries") || sTag.EndsWith(".Resources"))
      {
        // nested dictionaries / merged-dictionary lists recurse (Source handled on entry)
        CollectResources(*pRes, ctx, sPath);
      }
      else if (sTag == "Color")
      {
        // <Color x:Key="K">#AARRGGBB</Color> - shares the brush namespace, brushes reference these
        if (!sKey.IsEmpty())
          ctx.m_Brushes[plString(sKey)] = plString(pRes->m_sText.GetView());
      }
      else if (sTag == "FontFamily")
      {
        // <FontFamily x:Key="Font.Default.Thin">Fonts/#Larke Neue Thin</FontFamily>
        plStringBuilder sValue = pRes->m_sText.GetView();
        sValue.Trim(" \t\r\n");
        if (!sKey.IsEmpty() && !sValue.IsEmpty())
          ctx.m_FontResources[plString(sKey)] = plString(sValue.GetView());
      }
      else if (sTag == "ImageSource" || sTag == "BitmapImage")
      {
        // <ImageSource x:Key="K">Images/Bg.png</ImageSource> (BitmapImage uses UriSource)
        plStringBuilder sSource = pRes->m_sText.GetView();
        if (sSource.IsEmpty())
          sSource = pRes->GetAttribute("UriSource");
        sSource.Trim(" \t\r\n");

        if (!sKey.IsEmpty() && !sSource.IsEmpty())
          ctx.m_ImageSources[plString(sKey)] = plString(sSource.GetView());
      }
      else if (sTag == "ImageBrush")
      {
        // Resource form: <ImageBrush x:Key="K" ImageSource="Images/Bg.png"/>.
        // Property-element ImageBrush nodes are handled while their owning element is built.
        plStringBuilder sSource = pRes->GetAttribute("ImageSource");
        if (sSource.IsEmpty())
        {
          for (const plUiMarkupNode* pImageSource : pRes->m_Children)
          {
            if (!pImageSource->GetLocalTag().EndsWith(".ImageSource"))
              continue;

            for (const plUiMarkupNode* pImage : pImageSource->m_Children)
            {
              sSource = pImage->GetAttribute("UriSource");
              if (sSource.IsEmpty())
                sSource = pImage->m_sText.GetView();
              if (!sSource.IsEmpty())
                break;
            }

            if (!sSource.IsEmpty())
              break;
          }
        }
        sSource.Trim(" \t\r\n");

        if (!sKey.IsEmpty() && !sSource.IsEmpty())
          ctx.m_ImageSources[plString(sKey)] = plString(sSource.GetView());
      }
      else if (sTag == "StreamGeometry" || sTag == "PathGeometry" || sTag == "Geometry")
      {
        // <StreamGeometry x:Key="Logo">F1 M 0,0 L ... Z</StreamGeometry>
        plStringBuilder sData = pRes->m_sText.GetView();
        if (sData.IsEmpty())
          sData = pRes->GetAttribute("Figures");
        sData.Trim(" \t\r\n");

        if (!sKey.IsEmpty() && !sData.IsEmpty())
          ctx.m_GeometryResources[plString(sKey)] = plString(sData.GetView());
      }
      else if (sTag == "SolidColorBrush")
      {
        if (!sKey.IsEmpty())
          ctx.m_Brushes[plString(sKey)] = pRes->GetAttribute("Color");
      }
      else if (sTag == "LinearGradientBrush" || sTag == "RadialGradientBrush")
      {
        if (!sKey.IsEmpty())
        {
          XamlGradientBrush gradient = ReadGradientBrush(*pRes, sTag == "RadialGradientBrush");
          if (!gradient.m_Stops.IsEmpty())
          {
            // Keep the first color in the ordinary brush table as a fallback for
            // properties that cannot render gradients (text, borders, etc.).
            ctx.m_Brushes[plString(sKey)] = gradient.m_Stops[0].m_sColor;
            ctx.m_GradientBrushes[plString(sKey)] = gradient;
          }
        }
      }
      else if (sTag == "Style")
      {
        XamlStyle style;

        plStringBuilder sTargetType = pRes->GetAttribute("TargetType");
        sTargetType.TrimWordStart("x:Type ");
        sTargetType.Trim(" \t{}");
        style.m_sTargetType = sTargetType;

        MarkupExtension basedOn = ParseMarkupExtension(pRes->GetAttribute("BasedOn"));
        if (basedOn.m_Kind == MarkupExtension::Kind::StaticResource)
          style.m_sBasedOn = basedOn.m_sArg;

        for (const plUiMarkupNode* pSetter : pRes->m_Children)
        {
          if (pSetter->GetLocalTag() == "Setter")
          {
            plUiMarkupNode::Attribute& setter = style.m_Setters.ExpandAndGetRef();
            setter.m_sName = pSetter->GetAttribute("Property");
            setter.m_sValue = pSetter->GetAttribute("Value");
          }
          else if (pSetter->GetLocalTag().FindSubString("Trigger") != nullptr)
          {
            ctx.m_pReport->Warning(sPath, "Style triggers are unsupported; use element states / UI Styles after import.");
          }
          else if (pSetter->GetLocalTag().FindSubString("Template") != nullptr)
          {
            ctx.m_pReport->Warning(sPath, "ControlTemplates are unsupported; the control imports with its default look.");
          }
        }

        if (!sKey.IsEmpty())
        {
          ctx.m_Styles[plString(sKey)] = style;
        }
        else if (!style.m_sTargetType.IsEmpty())
        {
          plStringBuilder sImplicitKey;
          sImplicitKey.SetFormat("type:{}", style.m_sTargetType);
          ctx.m_Styles[plString(sImplicitKey.GetView())] = style;
        }
      }
      else if (sTag == "Storyboard")
      {
        ImportStoryboard(*pRes, sKey, ctx, sPath);
      }
      else if (sTag == "ControlTemplate")
      {
        if (!sKey.IsEmpty())
        {
          const float fContentWidth = FindTemplateContentWidth(*pRes);
          if (fContentWidth > 0.0f)
            ctx.m_TemplateContentWidths[plString(sKey)] = fContentWidth;
        }

        ctx.m_pReport->Info(sPath, "ControlTemplate visuals and triggers are unsupported; reusable layout hints were retained.");
      }
      else if (sTag == "DataTemplate")
      {
        ctx.m_pReport->Warning(sPath, "Resource DataTemplates are unsupported (only inline ItemTemplate imports).");
      }
      else
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Unsupported resource '{}' skipped.", sTag);
        ctx.m_pReport->Info(sPath, sMsg);
      }
    }
  }

  /// Loads a <ResourceDictionary Source="file.xaml"/> reference (deduplicated, relative to the import folder).
  void LoadResourceDictionaryFile(plStringView sSource, XamlContext& ctx, plStringView sPath)
  {
    // pack:// URIs point INSIDE a .NET assembly (e.g. the NoesisTheme dictionaries shipped
    // in Noesis.GUI.Extensions). Salvage attempt: search the import folder for a loose copy
    // of the referenced file name.
    if (sSource.StartsWith_NoCase("pack://"))
    {
      plStringView sFileName = sSource;
      if (const char* szSlash = sSource.FindLastSubString("/"))
        sFileName = plStringView(szSlash + 1, sSource.GetEndPointer());

      plString sFound;
      plFileSystemIterator it;
      for (it.StartSearch(ctx.m_sFolderAbs, plFileSystemIteratorFlags::ReportFilesRecursive); it.IsValid(); it.Next())
      {
        if (it.GetStats().m_sName.IsEqual_NoCase(sFileName))
        {
          plStringBuilder sFull;
          it.GetStats().GetFullPath(sFull);
          sFound = sFull;
          break;
        }
      }

      if (!sFound.IsEmpty())
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Assembly theme '{}' resolved to the loose copy next to the project.", sFileName);
        ctx.m_pReport->Info(sPath, sMsg);
        LoadResourceDictionaryFile(sFound, ctx, sPath);
        return;
      }

      plStringBuilder sMsg;
      sMsg.SetFormat("'{}' is embedded in a .NET assembly (pack:// URI) and cannot be read - copy '{}' into the import folder to include its styles.", sSource, sFileName);
      ctx.m_pReport->Warning(sPath, sMsg);
      return;
    }

    plStringBuilder sAbs;
    if (plPathUtils::IsAbsolutePath(sSource))
    {
      sAbs = sSource;
    }
    else
    {
      sAbs = ctx.m_sFolderAbs;
      sAbs.AppendPath(sSource);
    }
    sAbs.MakeCleanPath();

    if (ctx.m_LoadedDictionaries.Contains(plString(sAbs.GetView())))
      return;
    ctx.m_LoadedDictionaries.Insert(plString(sAbs.GetView()));

    plStringBuilder sContent;
    {
      plFileReader file;
      if (file.Open(sAbs).Failed())
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Referenced resource dictionary not found: '{}'.", sSource);
        ctx.m_pReport->Warning(sPath, sMsg);
        return;
      }
      sContent.ReadAll(file);
    }

    plUniquePtr<plUiMarkupNode> pParsed = plUiMarkupParser::Parse(sContent.GetView(), false, *ctx.m_pReport, sSource);

    for (const plUiMarkupNode* pRoot : pParsed->m_Children)
    {
      if (pRoot->GetLocalTag() == "ResourceDictionary")
      {
        CollectResources(*pRoot, ctx, sSource);

        plStringBuilder sMsg;
        sMsg.SetFormat("Resources loaded from '{}'.", sSource);
        ctx.m_pReport->Info(sPath, sMsg);
        return;
      }
    }

    plStringBuilder sMsg;
    sMsg.SetFormat("'{}' is not a ResourceDictionary; no resources loaded.", sSource);
    ctx.m_pReport->Warning(sPath, sMsg);
  }

  /// Preloads application-level resources: if App.xaml sits next to the imported file, its
  /// Application.Resources (typically merged dictionaries) resolve StaticResource references
  /// that live outside the imported document - the standard WPF/Noesis project layout.
  void PreloadApplicationResources(XamlContext& ctx)
  {
    plStringBuilder sAppXaml = ctx.m_sFolderAbs;
    sAppXaml.AppendPath("App.xaml");

    if (!plOSFile::ExistsFile(sAppXaml))
      return;

    plStringBuilder sContent;
    {
      plFileReader file;
      if (file.Open(sAppXaml).Failed())
        return;
      sContent.ReadAll(file);
    }

    plUniquePtr<plUiMarkupNode> pParsed = plUiMarkupParser::Parse(sContent.GetView(), false, *ctx.m_pReport, "App.xaml");

    for (const plUiMarkupNode* pRoot : pParsed->m_Children)
    {
      if (pRoot->GetLocalTag() != "Application")
        continue;

      for (const plUiMarkupNode* pChild : pRoot->m_Children)
      {
        if (pChild->GetLocalTag().EndsWith(".Resources"))
          CollectResources(*pChild, ctx, "App.xaml");
      }
    }
  }

  /// alignment + margin + size -> anchors/offsets on the IR node (Free placement)
  void ApplyAlignmentPlacement(plUiImportNode& ref_ir, const EffectiveAttributes& eff, float fDefaultWidth, float fDefaultHeight)
  {
    const plVec4 vMargin = ParseThickness(eff.Get("Margin", "0"));
    const float fWidth = ParseFloatAttr(eff.Get("Width"), fDefaultWidth);
    const float fHeight = ParseFloatAttr(eff.Get("Height"), fDefaultHeight);
    const bool bHasWidth = !eff.Get("Width").IsEmpty();
    const bool bHasHeight = !eff.Get("Height").IsEmpty();

    plStringView sHAlign = eff.Get("HorizontalAlignment", "Stretch");
    plStringView sVAlign = eff.Get("VerticalAlignment", "Stretch");

    // WPF: an explicit size turns Stretch into Center
    if (bHasWidth && sHAlign == "Stretch")
      sHAlign = "Center";
    if (bHasHeight && sVAlign == "Stretch")
      sVAlign = "Center";

    // X axis
    if (sHAlign == "Left")
    {
      ref_ir.m_vAnchorMin.x = ref_ir.m_vAnchorMax.x = 0.0f;
      ref_ir.m_vOffsets.x = vMargin.x;
      ref_ir.m_vOffsets.z = fWidth;
    }
    else if (sHAlign == "Right")
    {
      ref_ir.m_vAnchorMin.x = ref_ir.m_vAnchorMax.x = 1.0f;
      ref_ir.m_vOffsets.x = -vMargin.z - fWidth;
      ref_ir.m_vOffsets.z = fWidth;
    }
    else if (sHAlign == "Center")
    {
      ref_ir.m_vAnchorMin.x = ref_ir.m_vAnchorMax.x = 0.5f;
      ref_ir.m_vOffsets.x = -fWidth * 0.5f + (vMargin.x - vMargin.z) * 0.5f;
      ref_ir.m_vOffsets.z = fWidth;
    }
    else // Stretch
    {
      ref_ir.m_vAnchorMin.x = 0.0f;
      ref_ir.m_vAnchorMax.x = 1.0f;
      ref_ir.m_vOffsets.x = vMargin.x;
      ref_ir.m_vOffsets.z = -vMargin.z;
    }

    // Y axis
    if (sVAlign == "Top")
    {
      ref_ir.m_vAnchorMin.y = ref_ir.m_vAnchorMax.y = 0.0f;
      ref_ir.m_vOffsets.y = vMargin.y;
      ref_ir.m_vOffsets.w = fHeight;
    }
    else if (sVAlign == "Bottom")
    {
      ref_ir.m_vAnchorMin.y = ref_ir.m_vAnchorMax.y = 1.0f;
      ref_ir.m_vOffsets.y = -vMargin.w - fHeight;
      ref_ir.m_vOffsets.w = fHeight;
    }
    else if (sVAlign == "Center")
    {
      ref_ir.m_vAnchorMin.y = ref_ir.m_vAnchorMax.y = 0.5f;
      ref_ir.m_vOffsets.y = -fHeight * 0.5f + (vMargin.y - vMargin.w) * 0.5f;
      ref_ir.m_vOffsets.w = fHeight;
    }
    else // Stretch
    {
      ref_ir.m_vAnchorMin.y = 0.0f;
      ref_ir.m_vAnchorMax.y = 1.0f;
      ref_ir.m_vOffsets.y = vMargin.y;
      ref_ir.m_vOffsets.w = -vMargin.w;
    }
  }

  // forward declarations
  void BuildElement(const plUiMarkupNode& node, XamlContext& ctx, plUiImportNode& ref_irParent, PlacementMode placement, plStringView sParentPath);
  plString ImportXamlFileAsDocument(plStringView sXamlPathAbs, XamlContext& ctx);
  const plUiMarkupNode* FindDocumentRoot(const plUiMarkupNode& parseRoot);

  /// Returns the design/reference size of a same-folder custom UserControl. Grid Auto
  /// sizing needs this before the child document itself is imported.
  bool TryGetCustomControlReferenceSize(const plUiMarkupNode& node, XamlContext& ctx, plVec2& out_vSize)
  {
    plStringBuilder sCandidate = ctx.m_sFolderAbs;
    sCandidate.AppendPath(node.GetLocalTag());
    sCandidate.Append(".xaml");
    sCandidate.MakeCleanPath();

    const plString sPathKey = sCandidate;
    if (const plVec2* pCached = ctx.m_SubDocReferenceSizes.GetValue(sPathKey))
    {
      out_vSize = *pCached;
      return true;
    }

    if (!plOSFile::ExistsFile(sCandidate))
      return false;

    plStringBuilder sContent;
    {
      plFileReader file;
      if (file.Open(sCandidate).Failed())
        return false;
      sContent.ReadAll(file);
    }

    const plStringBuilder sName = plPathUtils::GetFileName(sCandidate);
    plUniquePtr<plUiMarkupNode> pParsed = plUiMarkupParser::Parse(sContent.GetView(), false, *ctx.m_pReport, sName);
    const plUiMarkupNode* pRoot = FindDocumentRoot(*pParsed);
    if (pRoot == nullptr)
      return false;

    const float fWidth = ParseFloatAttr(pRoot->GetAttribute("Width", pRoot->GetAttribute("d:DesignWidth")), 200.0f);
    const float fHeight = ParseFloatAttr(pRoot->GetAttribute("Height", pRoot->GetAttribute("d:DesignHeight")), 50.0f);
    out_vSize.Set(plMath::Max(fWidth, 1.0f), plMath::Max(fHeight, 1.0f));
    ctx.m_SubDocReferenceSizes[sPathKey] = out_vSize;
    return true;
  }

  /// Rough height estimate for stack-flow children without an explicit Height.
  /// Uses raw attributes only (no style lookups - this must not emit report entries).
  float EstimateXamlHeight(const plUiMarkupNode& node, plInt32 iDepth, XamlContext* pCtx = nullptr)
  {
    const float fExplicit = ParseFloatAttr(node.GetAttribute("Height", node.GetAttribute("d:DesignHeight")), -1.0f);
    const float fMinimum = plMath::Max(ParseFloatAttr(node.GetAttribute("MinHeight"), 0.0f), 0.0f);
    if (fExplicit >= 0.0f)
      return plMath::Max(fExplicit, fMinimum);

    if (iDepth > 6)
      return plMath::Max(50.0f, fMinimum);

    const plStringView sTag = node.GetLocalTag();

    if (sTag == "TextBlock" || sTag == "Label")
      return plMath::Max(ParseFloatAttr(node.GetAttribute("FontSize"), 16.0f) * 1.5f, fMinimum);

    if (sTag == "Button" || sTag == "ToggleButton" || sTag == "RepeatButton" || sTag == "CheckBox" || sTag == "RadioButton" || sTag == "ContentControl")
      return plMath::Max(plMath::Max(44.0f, ParseFloatAttr(node.GetAttribute("FontSize"), 16.0f) * 1.5f + 16.0f), fMinimum);

    if (sTag == "TextBox" || sTag == "PasswordBox")
      return 36.0f;
    if (sTag == "Slider" || sTag == "ProgressBar")
      return 24.0f;
    if (sTag == "Image")
      return 100.0f;
    if (sTag == "Rectangle" || sTag == "Line")
      return 4.0f; // separators; real shapes carry a Height attribute
    if (sTag == "Ellipse" || sTag == "Path" || sTag == "Polygon")
      return 32.0f;

    if (pCtx != nullptr)
    {
      plVec2 vCustomSize;
      if (TryGetCustomControlReferenceSize(node, *pCtx, vCustomSize))
        return plMath::Max(vCustomSize.y, fMinimum);
    }

    // containers: vertical flow sums, everything else takes the tallest child
    const bool bVerticalFlow =
      (sTag == "StackPanel" && node.GetAttribute("Orientation", "Vertical") != "Horizontal") ||
      sTag == "ItemsControl" || sTag == "ListBox";

    float fSum = 0.0f;
    float fMax = 0.0f;
    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (IsPropertyElement(*pChild))
        continue;

      const plVec4 vMargin = ParseThickness(pChild->GetAttribute("Margin", "0"));
      const float fChild = plMath::Max(EstimateXamlHeight(*pChild, iDepth + 1, pCtx) + vMargin.y + vMargin.w, 0.0f);
      fSum += fChild;
      fMax = plMath::Max(fMax, fChild);
    }

    const float fResult = bVerticalFlow ? fSum : fMax;
    return plMath::Max(fResult > 0.0f ? fResult : 50.0f, fMinimum);
  }

  // forward
  const plUiMarkupNode* FindPropertyElement(const plUiMarkupNode& node, plStringView sProperty);

  /// Rough width estimate for Auto grid columns (raw attributes only).
  float EstimateXamlWidth(const plUiMarkupNode& node, plInt32 iDepth = 0, XamlContext* pCtx = nullptr)
  {
    const float fExplicit = ParseFloatAttr(node.GetAttribute("Width", node.GetAttribute("d:DesignWidth")), -1.0f);
    const float fMinimum = plMath::Max(ParseFloatAttr(node.GetAttribute("MinWidth"), 0.0f), 0.0f);
    if (fExplicit >= 0.0f)
      return plMath::Max(fExplicit, fMinimum);

    if (iDepth > 6)
      return plMath::Max(200.0f, fMinimum);

    const plStringView sTag = node.GetLocalTag();

    if (sTag == "TextBlock" || sTag == "Label")
    {
      plStringView sText = node.GetAttribute("Text");
      if (sText.IsEmpty())
        sText = node.m_sText.GetView();
      return plMath::Max(plMath::Max(40.0f, static_cast<float>(sText.GetElementCount()) * ParseFloatAttr(node.GetAttribute("FontSize"), 16.0f) * 0.55f), fMinimum);
    }

    if (sTag == "CheckBox" || sTag == "RadioButton")
      return plMath::Max(32.0f, fMinimum);

    if (pCtx != nullptr)
    {
      plVec2 vCustomSize;
      if (TryGetCustomControlReferenceSize(node, *pCtx, vCustomSize))
        return plMath::Max(vCustomSize.x, fMinimum);
    }

    const bool bHorizontalFlow = sTag == "StackPanel" && node.GetAttribute("Orientation") == "Horizontal";
    float fSum = 0.0f;
    float fMax = 0.0f;
    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (IsPropertyElement(*pChild))
        continue;

      const plVec4 vMargin = ParseThickness(pChild->GetAttribute("Margin", "0"));
      const float fChild = plMath::Max(EstimateXamlWidth(*pChild, iDepth + 1, pCtx) + vMargin.x + vMargin.z, 0.0f);
      fSum += fChild;
      fMax = plMath::Max(fMax, fChild);
    }

    const float fResult = bHorizontalFlow ? fSum : fMax;
    return plMath::Max(fResult > 0.0f ? fResult : 200.0f, fMinimum);
  }

  /// Converts a Grid's Row/ColumnDefinitions into per-stop (anchor fraction, pixel offset)
  /// pairs. A position inside the grid is EXACTLY  frac * parentSize + px  for star and
  /// fixed-px definitions; Auto definitions are estimated from their cell content.
  void BuildGridStops(const plUiMarkupNode& gridNode, const char* szDefsProperty, const char* szSizeAttr, bool bRows, XamlContext& ctx, plStringView sPath,
    plHybridArray<float, 9>& out_fracStops, plHybridArray<float, 9>& out_pxStops)
  {
    struct Def
    {
      bool m_bStar = true;
      float m_fValue = 1.0f; ///< star weight or px size
    };

    plHybridArray<Def, 8> defs;
    bool bHadAuto = false;

    if (const plUiMarkupNode* pDefs = FindPropertyElement(gridNode, szDefsProperty))
    {
      plInt32 iDefIndex = 0;
      for (const plUiMarkupNode* pDef : pDefs->m_Children)
      {
        plStringBuilder sSize = pDef->GetAttribute(szSizeAttr, "*");
        sSize.Trim(" \t");

        Def& def = defs.ExpandAndGetRef();

        if (sSize.IsEmpty() || sSize == "*")
        {
          // defaults
        }
        else if (sSize.EndsWith("*"))
        {
          sSize.Shrink(0, 1);
          double fWeight = 1.0;
          if (plConversionUtils::StringToFloat(sSize, fWeight).Failed() || fWeight <= 0.0)
            fWeight = 1.0;
          def.m_fValue = static_cast<float>(fWeight);
        }
        else if (sSize.IsEqual_NoCase("Auto"))
        {
          // estimate from the content of this row/column (span-1 children only)
          bHadAuto = true;
          float fEstimate = bRows ? 24.0f : 60.0f;

          for (const plUiMarkupNode* pChild : gridNode.m_Children)
          {
            if (IsPropertyElement(*pChild))
              continue;

            const plInt32 iIndex = static_cast<plInt32>(ParseFloatAttr(pChild->GetAttribute(bRows ? "Grid.Row" : "Grid.Column", "0"), 0.0f));
            const plInt32 iSpan = static_cast<plInt32>(ParseFloatAttr(pChild->GetAttribute(bRows ? "Grid.RowSpan" : "Grid.ColumnSpan", "1"), 1.0f));
            if (iIndex != iDefIndex || iSpan > 1)
              continue;

            const plVec4 vMargin = ParseThickness(pChild->GetAttribute("Margin", "0"));
            const float fChild = bRows ? EstimateXamlHeight(*pChild, 0, &ctx) + vMargin.y + vMargin.w
                                       : EstimateXamlWidth(*pChild, 0, &ctx) + vMargin.x + vMargin.z;
            fEstimate = plMath::Max(fEstimate, fChild);
          }

          def.m_bStar = false;
          def.m_fValue = fEstimate;
        }
        else
        {
          def.m_bStar = false;
          def.m_fValue = plMath::Max(ParseFloatAttr(sSize, 0.0f), 0.0f);
        }

        ++iDefIndex;
      }
    }

    if (defs.IsEmpty())
    {
      defs.ExpandAndGetRef(); // single implicit star
    }

    float fStarTotal = 0.0f;
    float fPxTotal = 0.0f;
    for (const Def& def : defs)
    {
      if (def.m_bStar)
        fStarTotal += def.m_fValue;
      else
        fPxTotal += def.m_fValue;
    }

    // a grid without any star definition doesn't stretch in WPF; approximate it as
    // proportional fractions so the content still fills its rect
    if (fStarTotal <= 0.0f)
    {
      for (Def& def : defs)
      {
        def.m_bStar = true;
        def.m_fValue = plMath::Max(def.m_fValue, 1.0f);
        fStarTotal += def.m_fValue;
      }
      fPxTotal = 0.0f;
      ctx.m_pReport->Info(sPath, "Grid has no star-sized definitions; sizes imported as proportions.");
    }
    else if (bHadAuto)
    {
      ctx.m_pReport->Info(sPath, "Auto-sized grid definitions were estimated from their content.");
    }

    out_fracStops.Clear();
    out_pxStops.Clear();

    float fStarBefore = 0.0f;
    float fPxBefore = 0.0f;
    out_fracStops.PushBack(0.0f);
    out_pxStops.PushBack(0.0f);

    for (const Def& def : defs)
    {
      if (def.m_bStar)
        fStarBefore += def.m_fValue;
      else
        fPxBefore += def.m_fValue;

      const float fFrac = fStarBefore / fStarTotal;
      out_fracStops.PushBack(fFrac);
      out_pxStops.PushBack(fPxBefore - fFrac * fPxTotal);
    }
  }

  /// Applies a {Binding} or plain value to a string property; returns true when a binding was set.
  bool ApplyStringOrBinding(plUiImportNode& ref_ir, plStringView sValue, const char* szPlainProperty, const char* szBindingProperty, XamlContext& ctx, plStringView sPath)
  {
    MarkupExtension ext = ParseMarkupExtension(sValue);

    if (ext.m_Kind == MarkupExtension::Kind::Binding)
    {
      if (ext.m_sArg.IsEmpty())
      {
        ctx.m_pReport->Warning(sPath, "Binding without a path; skipped.");
        return false;
      }
      if (szBindingProperty == nullptr)
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Property '{}' cannot be bound in GameUI; binding '{}' dropped.", szPlainProperty, ext.m_sArg);
        ctx.m_pReport->Warning(sPath, sMsg);
        return false;
      }
      ref_ir.Set(szBindingProperty, ext.m_sArg.GetView());
      return true;
    }

    if (ext.m_Kind == MarkupExtension::Kind::None)
    {
      if (szPlainProperty != nullptr && !sValue.IsEmpty())
        ref_ir.Set(szPlainProperty, sValue);
      return false;
    }

    plStringBuilder sMsg;
    sMsg.SetFormat("Markup extension '{}' on '{}' is unsupported.", ext.m_sArg, szPlainProperty);
    ctx.m_pReport->Warning(sPath, sMsg);
    return false;
  }

  void ApplyCommonVisuals(plUiImportNode& ref_ir, const EffectiveAttributes& eff, XamlContext& ctx, plStringView sPath, bool bDefaultTransparent)
  {
    plColorGammaUB col;

    const plStringView sBackground = eff.Get("Background");
    if (!sBackground.IsEmpty())
      ApplyVisualBrush(ref_ir, sBackground, ctx, sPath);
    else if (bDefaultTransparent)
      ref_ir.Set("Color", plColorGammaUB(0, 0, 0, 0));

    const plStringView sBorderBrush = eff.Get("BorderBrush");
    const plStringView sBorderThickness = eff.Get("BorderThickness");
    if (!sBorderBrush.IsEmpty() && !sBorderThickness.IsEmpty())
    {
      const plVec4 vThickness = ParseThickness(sBorderThickness);
      const float fMax = plMath::Max(plMath::Max(vThickness.x, vThickness.y), plMath::Max(vThickness.z, vThickness.w));
      const float fMin = plMath::Min(plMath::Min(vThickness.x, vThickness.y), plMath::Min(vThickness.z, vThickness.w));
      if (fMax != fMin)
        ctx.m_pReport->Info(sPath, "Non-uniform BorderThickness; the maximum imports.");

      if (fMax > 0.0f && ResolveBrushColor(sBorderBrush, ctx, sPath, col).Succeeded())
      {
        ref_ir.Set("BorderWidth", fMax);
        ref_ir.Set("BorderColor", col);
      }
    }

    const plStringView sCornerRadius = eff.Get("CornerRadius");
    if (!sCornerRadius.IsEmpty())
    {
      const plVec4 v = ParseThickness(sCornerRadius); // same "a" / "a,b,c,d" grammar; WPF order tl,tr,br,bl
      ref_ir.Set("CornerRadius", v);
    }

    const plStringView sOpacity = eff.Get("Opacity");
    if (!sOpacity.IsEmpty())
      ref_ir.Set("Opacity", plMath::Clamp(ParseFloatAttr(sOpacity, 1.0f), 0.0f, 1.0f));

    const plStringView sVisibility = eff.Get("Visibility");
    if (sVisibility == "Collapsed" || sVisibility == "Hidden")
    {
      ref_ir.Set("Visible", false);
    }
    else if (sVisibility.StartsWith("{"))
    {
      MarkupExtension ext = ParseMarkupExtension(sVisibility);
      if (ext.m_Kind == MarkupExtension::Kind::Binding && !ext.m_sArg.IsEmpty())
      {
        ref_ir.Set("VisibleBinding", ext.m_sArg.GetView());
        ctx.m_pReport->Info(sPath, "Visibility binding imports as VisibleBinding (expects a bool-convertible entry, no converter support).");
      }
    }

    // the Noesis tooltip pattern - IsEnabled="{Binding IsKeyboardFocused, ElementName=X}" -
    // imports as a FLOW STATE: hidden by default, shown while the canvas-published
    // "Ui_Active" entry equals the referenced element's name
    const plStringView sIsEnabled = eff.Get("IsEnabled");
    if (!sIsEnabled.IsEmpty())
    {
      MarkupExtension enabledExt = ParseMarkupExtension(sIsEnabled);

      if (enabledExt.m_Kind == MarkupExtension::Kind::Binding && !enabledExt.m_sElementName.IsEmpty() &&
          (enabledExt.m_sArg == "IsKeyboardFocused" || enabledExt.m_sArg == "IsKeyboardFocusWithin" ||
            enabledExt.m_sArg == "IsFocused" || enabledExt.m_sArg == "IsMouseOver"))
      {
        ref_ir.Set("Visible", false);

        plUiImportState& state = ref_ir.m_States.ExpandAndGetRef();
        state.m_sName = "FocusReveal";
        state.m_sEntry = "Ui_Active";
        state.m_iCondition = plUiStateCondition::Equal;
        state.m_sValueText = enabledExt.m_sElementName;
        state.m_iVisibility = plUiStateVisibility::Show;

        plStringBuilder sMsg;
        sMsg.SetFormat("Focus binding imported as a flow state: visible while Ui_Active == '{}'.", enabledExt.m_sElementName);
        ctx.m_pReport->Info(sPath, sMsg);
      }
      else
      {
        ctx.m_pReport->Info(sPath, "IsEnabled has no generic equivalent (buttons support DisabledBinding).");
      }
    }
  }

  plString NormalizeFontName(plStringView sName)
  {
    plStringBuilder sNorm;
    for (auto it = plStringBuilder(sName).GetIteratorFront(); it.IsValid(); ++it)
    {
      const plUInt32 c = it.GetCharacter();
      if (c == ' ' || c == '_' || c == '-' || c == '.')
        continue;
      sNorm.Append(plStringUtils::ToLowerChar(c));
    }
    return plString(sNorm.GetView());
  }

  /// Match quality of a font FILE name against a font FAMILY name (both normalized).
  /// Lower is better; -1 = no match. Handles abbreviated file names like Noesis's
  /// "weblysleekuisl.ttf" for "WeblySleek UI Semilight": the file name must be an in-order
  /// SUBSEQUENCE of the family name (or contain it), scored by how tightly it matches.
  plInt32 FontMatchScore(const plString& sNormFile, const plString& sNormFamily)
  {
    if (sNormFile == sNormFamily)
      return 0;

    if (sNormFile.FindSubString(sNormFamily) != nullptr)
      return 1 + static_cast<plInt32>(sNormFile.GetElementCount() - sNormFamily.GetElementCount());

    if (sNormFamily.FindSubString(sNormFile) != nullptr)
      return 1 + static_cast<plInt32>(sNormFamily.GetElementCount() - sNormFile.GetElementCount());

    // subsequence: consume the file's characters from the family in order (both are
    // normalized ASCII); score = index of the last consumed family character, so
    // tighter/earlier matches win ("...uisb" resolves to Semibold, not Semilight)
    const char* szFile = sNormFile.GetData();
    const char* szFamily = sNormFamily.GetData();
    const plUInt32 uiFileLen = sNormFile.GetElementCount();
    const plUInt32 uiFamilyLen = sNormFamily.GetElementCount();

    if (uiFileLen < 4 || uiFileLen > uiFamilyLen || szFile[0] != szFamily[0])
      return -1;

    plUInt32 uiFamilyPos = 0;
    for (plUInt32 uiFilePos = 0; uiFilePos < uiFileLen; ++uiFilePos)
    {
      while (uiFamilyPos < uiFamilyLen && szFamily[uiFamilyPos] != szFile[uiFilePos])
        ++uiFamilyPos;

      if (uiFamilyPos >= uiFamilyLen)
        return -1;

      ++uiFamilyPos;
    }

    return 100 + static_cast<plInt32>(uiFamilyPos);
  }

  /// Resolves a XAML FontFamily ("Fonts/#WeblySleek UI Semilight", "./#Family" or a plain
  /// family name) to a .ttf/.otf on disk by fuzzy-matching file names in the referenced
  /// folder (plus ./ and ./Fonts). Cached per family; empty = no match.
  plString ResolveFontFile(plStringView sFontFamily, XamlContext& ctx, plStringView sPath)
  {
    // "{StaticResource Font.Default.Thin}" -> the <FontFamily x:Key> resource's value
    {
      MarkupExtension ext = ParseMarkupExtension(sFontFamily);
      if (ext.m_Kind == MarkupExtension::Kind::StaticResource)
      {
        if (const plString* pResource = ctx.m_FontResources.GetValue(ext.m_sArg))
          return ResolveFontFile(pResource->GetView(), ctx, sPath);

        plStringBuilder sMsg;
        sMsg.SetFormat("FontFamily resource '{}' not found; assign a UI Font asset after import.", ext.m_sArg);
        ctx.m_pReport->Info(sPath, sMsg);
        return {};
      }
      if (ext.m_Kind != MarkupExtension::Kind::None)
        return {};
    }

    if (const plString* pCached = ctx.m_FontFiles.GetValue(plString(sFontFamily)))
      return *pCached;

    plStringView sFolderPart;
    plStringView sFamilyPart = sFontFamily;

    if (const char* szHash = sFontFamily.FindSubString("#"))
    {
      sFolderPart = plStringView(sFontFamily.GetStartPointer(), szHash);
      sFamilyPart = plStringView(szHash + 1, sFontFamily.GetEndPointer());
    }

    const plString sNormFamily = NormalizeFontName(sFamilyPart);

    plHybridArray<plString, 3> folders;
    if (!sFolderPart.IsEmpty())
    {
      plStringBuilder sFolder = ctx.m_sFolderAbs;
      sFolder.AppendPath(sFolderPart);
      sFolder.MakeCleanPath();
      sFolder.Trim("/\\");
      folders.PushBack(plString(sFolder.GetView()));
    }
    folders.PushBack(ctx.m_sFolderAbs);
    {
      plStringBuilder sFonts = ctx.m_sFolderAbs;
      sFonts.AppendPath("Fonts");
      folders.PushBack(plString(sFonts.GetView()));
    }

    plString sResult;
    plInt32 iBestScore = plMath::MaxValue<plInt32>();

    for (const plString& sFolder : folders)
    {
      plFileSystemIterator it;
      for (it.StartSearch(sFolder, plFileSystemIteratorFlags::ReportFiles); it.IsValid(); it.Next())
      {
        const plStringView sFileName = it.GetStats().m_sName.GetView();
        if (!sFileName.EndsWith_NoCase(".ttf") && !sFileName.EndsWith_NoCase(".otf"))
          continue;

        plStringView sBase = sFileName;
        sBase.Shrink(0, 4); // strip extension
        const plString sNormFile = NormalizeFontName(sBase);

        const plInt32 iScore = FontMatchScore(sNormFile, sNormFamily);
        if (iScore >= 0 && iScore < iBestScore)
        {
          plStringBuilder sFull;
          it.GetStats().GetFullPath(sFull);
          sResult = sFull;
          iBestScore = iScore;
        }
      }

      if (!sResult.IsEmpty())
        break; // the referenced folder wins over the fallback folders
    }

    if (sResult.IsEmpty())
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("Font family '{}' has no matching .ttf/.otf next to the file; assign a UI Font asset after import.", sFamilyPart);
      ctx.m_pReport->Info(sPath, sMsg);
    }

    ctx.m_FontFiles[plString(sFontFamily)] = sResult;
    return sResult;
  }

  void ApplyTextVisuals(plUiImportNode& ref_ir, const EffectiveAttributes& eff, XamlContext& ctx, plStringView sPath)
  {
    plColorGammaUB col;
    const plStringView sForeground = eff.Get("Foreground");
    if (!sForeground.IsEmpty() && ResolveBrushColor(sForeground, ctx, sPath, col).Succeeded())
      ref_ir.Set("TextColor", col);

    const plStringView sFontSize = eff.Get("FontSize");
    if (!sFontSize.IsEmpty())
      ref_ir.Set("FontSize", static_cast<plInt32>(plMath::Clamp(ParseFloatAttr(sFontSize, 16.0f), 4.0f, 256.0f)));

    // font: explicit FontFamily, else the document root's (WPF inherits font down the tree)
    const plStringView sFontFamily = eff.Get("FontFamily");
    if (!sFontFamily.IsEmpty())
      ref_ir.m_sFontFile = ResolveFontFile(sFontFamily, ctx, sPath);
    else
      ref_ir.m_sFontFile = ctx.m_sDefaultFontFile;
  }

  const plUiMarkupNode* FindPropertyElement(const plUiMarkupNode& node, plStringView sProperty)
  {
    plStringBuilder sFull;
    sFull.SetFormat("{}.{}", node.GetLocalTag(), sProperty);

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (pChild->GetLocalTag() == sFull.GetView())
        return pChild;
    }
    return nullptr;
  }

  const plUiMarkupNode* FindAttachedPropertyElement(const plUiMarkupNode& node, plStringView sProperty)
  {
    plStringBuilder sSuffix;
    sSuffix.SetFormat(".{}", sProperty);

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (pChild->GetLocalTag().EndsWith(sSuffix))
        return pChild;
    }
    return nullptr;
  }

  struct ImportedTransform
  {
    plVec3 m_vCenter = plVec3::MakeZero();
    plVec3 m_vTranslation = plVec3::MakeZero();
    plVec3 m_vRotation = plVec3::MakeZero();
    plVec3 m_vScale = plVec3(1.0f);
    float m_fPerspective = 1000.0f;
    bool m_bHasDynamicValue = false;
    bool m_bHasNonZeroSkew = false;
    bool m_bHasUnsupportedNode = false;
  };

  float ParseTransformValue(const plUiMarkupNode& node, plStringView sAttribute, float fFallback, ImportedTransform& ref_transform)
  {
    const plStringView sValue = node.GetAttribute(sAttribute);
    if (sValue.IsEmpty())
      return fFallback;

    if (ParseMarkupExtension(sValue).m_Kind != MarkupExtension::Kind::None)
    {
      ref_transform.m_bHasDynamicValue = true;
      return fFallback;
    }

    return ParseFloatAttr(sValue, fFallback);
  }

  void AccumulateTransformNode(const plUiMarkupNode& node, ImportedTransform& ref_transform)
  {
    const plStringView sTag = node.GetLocalTag();

    if (sTag == "TransformGroup" || sTag.EndsWith(".RenderTransform") || sTag.EndsWith(".LayoutTransform") || sTag.EndsWith(".Transform3D"))
    {
      for (const plUiMarkupNode* pChild : node.m_Children)
        AccumulateTransformNode(*pChild, ref_transform);
      return;
    }

    if (sTag == "ScaleTransform")
    {
      ref_transform.m_vScale.x *= ParseTransformValue(node, "ScaleX", 1.0f, ref_transform);
      ref_transform.m_vScale.y *= ParseTransformValue(node, "ScaleY", 1.0f, ref_transform);
      ref_transform.m_vCenter.x = ParseTransformValue(node, "CenterX", ref_transform.m_vCenter.x, ref_transform);
      ref_transform.m_vCenter.y = ParseTransformValue(node, "CenterY", ref_transform.m_vCenter.y, ref_transform);
      return;
    }

    if (sTag == "RotateTransform")
    {
      ref_transform.m_vRotation.z += ParseTransformValue(node, "Angle", 0.0f, ref_transform);
      ref_transform.m_vCenter.x = ParseTransformValue(node, "CenterX", ref_transform.m_vCenter.x, ref_transform);
      ref_transform.m_vCenter.y = ParseTransformValue(node, "CenterY", ref_transform.m_vCenter.y, ref_transform);
      return;
    }

    if (sTag == "TranslateTransform")
    {
      ref_transform.m_vTranslation.x += ParseTransformValue(node, "X", 0.0f, ref_transform);
      ref_transform.m_vTranslation.y += ParseTransformValue(node, "Y", 0.0f, ref_transform);
      return;
    }

    if (sTag == "SkewTransform")
    {
      const float fAngleX = ParseTransformValue(node, "AngleX", 0.0f, ref_transform);
      const float fAngleY = ParseTransformValue(node, "AngleY", 0.0f, ref_transform);
      ref_transform.m_bHasNonZeroSkew |= fAngleX != 0.0f || fAngleY != 0.0f;
      return;
    }

    if (sTag == "CompositeTransform")
    {
      ref_transform.m_vScale.x *= ParseTransformValue(node, "ScaleX", 1.0f, ref_transform);
      ref_transform.m_vScale.y *= ParseTransformValue(node, "ScaleY", 1.0f, ref_transform);
      ref_transform.m_vRotation.z += ParseTransformValue(node, "Rotation", 0.0f, ref_transform);
      ref_transform.m_vTranslation.x += ParseTransformValue(node, "TranslateX", 0.0f, ref_transform);
      ref_transform.m_vTranslation.y += ParseTransformValue(node, "TranslateY", 0.0f, ref_transform);
      ref_transform.m_vCenter.x = ParseTransformValue(node, "CenterX", ref_transform.m_vCenter.x, ref_transform);
      ref_transform.m_vCenter.y = ParseTransformValue(node, "CenterY", ref_transform.m_vCenter.y, ref_transform);
      return;
    }

    if (sTag == "CompositeTransform3D")
    {
      ref_transform.m_vScale.x *= ParseTransformValue(node, "ScaleX", 1.0f, ref_transform);
      ref_transform.m_vScale.y *= ParseTransformValue(node, "ScaleY", 1.0f, ref_transform);
      ref_transform.m_vScale.z *= ParseTransformValue(node, "ScaleZ", 1.0f, ref_transform);
      ref_transform.m_vRotation.x += ParseTransformValue(node, "RotationX", 0.0f, ref_transform);
      ref_transform.m_vRotation.y += ParseTransformValue(node, "RotationY", 0.0f, ref_transform);
      ref_transform.m_vRotation.z += ParseTransformValue(node, "RotationZ", 0.0f, ref_transform);
      ref_transform.m_vTranslation.x += ParseTransformValue(node, "TranslateX", 0.0f, ref_transform);
      ref_transform.m_vTranslation.y += ParseTransformValue(node, "TranslateY", 0.0f, ref_transform);
      ref_transform.m_vTranslation.z += ParseTransformValue(node, "TranslateZ", 0.0f, ref_transform);
      ref_transform.m_vCenter.x = ParseTransformValue(node, "CenterX", ref_transform.m_vCenter.x, ref_transform);
      ref_transform.m_vCenter.y = ParseTransformValue(node, "CenterY", ref_transform.m_vCenter.y, ref_transform);
      ref_transform.m_vCenter.z = ParseTransformValue(node, "CenterZ", ref_transform.m_vCenter.z, ref_transform);
      ref_transform.m_fPerspective = ParseTransformValue(node, "Perspective", ref_transform.m_fPerspective, ref_transform);
      ref_transform.m_fPerspective = ParseTransformValue(node, "Depth", ref_transform.m_fPerspective, ref_transform);
      return;
    }

    ref_transform.m_bHasUnsupportedNode = true;
  }

  void ApplyXamlTransforms(const plUiMarkupNode& node, plUiImportNode& ref_ir, const EffectiveAttributes& eff, XamlContext& ctx, plStringView sPath)
  {
    const plUiMarkupNode* pRenderTransform = FindAttachedPropertyElement(node, "RenderTransform");
    const plUiMarkupNode* pLayoutTransform = FindAttachedPropertyElement(node, "LayoutTransform");
    const plUiMarkupNode* pTransform3D = FindAttachedPropertyElement(node, "Transform3D");

    ImportedTransform transform;
    if (pRenderTransform != nullptr)
      AccumulateTransformNode(*pRenderTransform, transform);
    if (pLayoutTransform != nullptr)
      AccumulateTransformNode(*pLayoutTransform, transform);
    if (pTransform3D != nullptr)
      AccumulateTransformNode(*pTransform3D, transform);

    const bool bHasTransform =
      transform.m_vCenter != plVec3::MakeZero() ||
      transform.m_vTranslation != plVec3::MakeZero() ||
      transform.m_vRotation != plVec3::MakeZero() ||
      transform.m_vScale != plVec3(1.0f);

    if (bHasTransform)
    {
      const bool bIs3D = pTransform3D != nullptr;
      const plVec4 vOrigin = ParseThickness(eff.Get("RenderTransformOrigin", bIs3D ? "0.5,0.5" : "0,0"));
      ref_ir.Set("TransformOrigin", plVec2(vOrigin.x, vOrigin.y));
      ref_ir.Set("TransformCenter", transform.m_vCenter);
      ref_ir.Set("TransformTranslation", transform.m_vTranslation);
      ref_ir.Set("TransformRotation", transform.m_vRotation);
      ref_ir.Set("TransformScale", transform.m_vScale);
      ref_ir.Set("TransformPerspective", plMath::Max(transform.m_fPerspective, 1.0f));

      if (pLayoutTransform != nullptr)
        ctx.m_pReport->Info(sPath, "LayoutTransform imports as a post-layout subtree transform; sibling layout is unchanged.");
      if (bIs3D)
        ctx.m_pReport->Info(sPath, "Transform3D imports with perspective projection across the complete element subtree.");
    }

    if (transform.m_bHasDynamicValue)
      ctx.m_pReport->Info(sPath, "A transform uses a runtime binding; its static fallback value imported.");
    if (transform.m_bHasNonZeroSkew)
      ctx.m_pReport->Warning(sPath, "Non-zero SkewTransform is unsupported; the remaining transform components imported.");
    if (transform.m_bHasUnsupportedNode)
      ctx.m_pReport->Warning(sPath, "An unsupported transform node was skipped; supported transform components imported.");

    if ((!eff.Get("RenderTransform").IsEmpty() && pRenderTransform == nullptr) ||
        (!eff.Get("LayoutTransform").IsEmpty() && pLayoutTransform == nullptr))
    {
      ctx.m_pReport->Warning(sPath, "Inline/style RenderTransform or LayoutTransform could not be decoded; use a property element for import.");
    }
  }

  /// Handles <X.Background> and shape <X.Fill> property elements.
  void ApplyBrushPropertyElement(const plUiMarkupNode& node, plUiImportNode& ref_ir, XamlContext& ctx, plStringView sPath)
  {
    const plUiMarkupNode* pProp = FindPropertyElement(node, "Background");
    if (pProp == nullptr)
      pProp = FindPropertyElement(node, "Fill");
    if (pProp == nullptr)
      return;

    for (const plUiMarkupNode* pBrush : pProp->m_Children)
    {
      const plStringView sBrushTag = pBrush->GetLocalTag();
      plColorGammaUB col;

      if (sBrushTag == "SolidColorBrush")
      {
        if (ResolveBrushColor(pBrush->GetAttribute("Color"), ctx, sPath, col).Succeeded())
          ref_ir.Set("Color", col);
        return;
      }

      if (sBrushTag == "LinearGradientBrush")
      {
        ApplyGradientBrush(ref_ir, ReadGradientBrush(*pBrush, false), ctx, sPath);
        return;
      }

      if (sBrushTag == "RadialGradientBrush")
      {
        ApplyGradientBrush(ref_ir, ReadGradientBrush(*pBrush, true), ctx, sPath);
        return;
      }

      if (sBrushTag == "ImageBrush")
      {
        const plStringView sSource = pBrush->GetAttribute("ImageSource");
        if (!sSource.IsEmpty())
        {
          if (ApplyImageSource(ref_ir, sSource, ctx, sPath))
            return;
          return;
        }

        if (const plUiMarkupNode* pImageSource = FindPropertyElement(*pBrush, "ImageSource"))
        {
          for (const plUiMarkupNode* pImage : pImageSource->m_Children)
          {
            plStringView sNestedSource = pImage->GetAttribute("UriSource");
            if (sNestedSource.IsEmpty())
              sNestedSource = pImage->m_sText.GetView();
            if (!sNestedSource.IsEmpty() && ApplyImageSource(ref_ir, sNestedSource, ctx, sPath))
              return;
          }
        }

        ctx.m_pReport->Warning(sPath, "ImageBrush has no resolvable ImageSource.");
        return;
      }

      plStringBuilder sMsg;
      sMsg.SetFormat("Brush '{}' in a Background/Fill property element is unsupported.", sBrushTag);
      ctx.m_pReport->Warning(sPath, sMsg);
    }
  }

  /// Builds the content children of a container node (skipping property elements).
  void BuildContentChildren(const plUiMarkupNode& node, XamlContext& ctx, plUiImportNode& ref_irContainer, PlacementMode placement, plStringView sPath)
  {
    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (IsPropertyElement(*pChild))
        continue;
      BuildElement(*pChild, ctx, ref_irContainer, placement, sPath);
    }
  }

  plString GetXamlElementName(const plUiMarkupNode& node)
  {
    plStringView sName = node.GetAttribute("x:Name");
    if (sName.IsEmpty())
      sName = node.GetAttribute("Name");
    if (sName.IsEmpty())
      return plString(node.GetLocalTag());
    return sName;
  }

  void BuildElement(const plUiMarkupNode& node, XamlContext& ctx, plUiImportNode& ref_irParent, PlacementMode placement, plStringView sParentPath)
  {
    const plStringView sTag = node.GetLocalTag();

    plStringBuilder sPath = sParentPath;
    sPath.AppendPath(GetXamlElementName(node).GetData());

    EffectiveAttributes eff = MakeEffective(node, ctx, sPath);

    // resources can appear on any element
    if (const plUiMarkupNode* pResources = FindPropertyElement(node, "Resources"))
      CollectResources(*pResources, ctx, sPath);

    // ————— type mapping —————
    const plRTTI* pType = nullptr;
    bool bStackLike = false;
    bool bCanvasLike = false;
    bool bGridFractional = false;
    float fHeaderedContentWidth = -1.0f;
    plHybridArray<float, 9> gridRowStops; // anchor-fraction stops, size = rows + 1
    plHybridArray<float, 9> gridRowPx;    // px-offset stops (fixed/Auto parts)
    plHybridArray<float, 9> gridColStops;
    plHybridArray<float, 9> gridColPx;

    if (sTag == "TextBlock" || sTag == "Label")
      pType = plGetStaticRTTI<plUiText>();
    else if (sTag == "Image")
      pType = plGetStaticRTTI<plUiImage>();
    else if (sTag == "Button" || sTag == "RepeatButton")
      pType = plGetStaticRTTI<plUiButton>();
    else if (sTag == "ToggleButton" || sTag == "CheckBox" || sTag == "RadioButton")
      pType = plGetStaticRTTI<plUiToggle>();
    else if (sTag == "Slider")
      pType = plGetStaticRTTI<plUiSlider>();
    else if (sTag == "ProgressBar")
      pType = plGetStaticRTTI<plUiProgressBar>();
    else if (sTag == "TextBox" || sTag == "PasswordBox")
      pType = plGetStaticRTTI<plUiTextInput>();
    else if (sTag == "ScrollViewer")
    {
      pType = plGetStaticRTTI<plUiScrollView>();
      bStackLike = true;
    }
    else if (sTag == "StackPanel" || sTag == "WrapPanel" || sTag == "DockPanel" || sTag == "UniformGrid")
    {
      pType = plGetStaticRTTI<plUiStackPanel>();
      bStackLike = true;
      if (sTag != "StackPanel")
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("{} imports as a Stack Panel (its specific layout is unsupported).", sTag);
        ctx.m_pReport->Info(sPath, sMsg);
      }
    }
    else if (sTag == "Canvas")
    {
      pType = plGetStaticRTTI<plUiPanel>();
      bCanvasLike = true;
    }
    else if (sTag == "Border")
    {
      pType = plGetStaticRTTI<plUiPanel>();
    }
    else if (sTag == "Grid")
    {
      // every grid imports as a Panel whose children get EXACT (anchor fraction + px
      // offset) cell rectangles: star and fixed-px definitions are exact, Auto is
      // estimated from content; Row/ColumnSpan are honored
      pType = plGetStaticRTTI<plUiPanel>();
      bGridFractional = true;
      BuildGridStops(node, "RowDefinitions", "Height", true, ctx, sPath, gridRowStops, gridRowPx);
      BuildGridStops(node, "ColumnDefinitions", "Width", false, ctx, sPath, gridColStops, gridColPx);
    }
    else if (sTag == "ItemsControl" || sTag == "ListBox" || sTag == "ListView")
    {
      pType = plGetStaticRTTI<plUiItemList>();
    }
    else if (sTag == "Path" || sTag == "Polygon" || sTag == "Polyline")
    {
      pType = plGetStaticRTTI<plUiPath>(); // real vector geometry (tessellated fill)
    }
    else if (sTag == "Rectangle" || sTag == "Ellipse" || sTag == "Line")
    {
      pType = plGetStaticRTTI<plUiPanel>(); // simple shapes become colored panels (Fill/Stroke handled below)
    }
    else if (sTag == "ContentControl" || sTag == "HeaderedContentControl" || sTag == "Viewbox" || sTag == "UserControl" || sTag == "Page" ||
             sTag == "Decorator" || sTag == "ContentPresenter")
    {
      // Decorator/ContentPresenter are layout helpers (often size-reference stubs) - quiet panels
      pType = plGetStaticRTTI<plUiPanel>();
      if (sTag == "Viewbox")
        ctx.m_pReport->Info(sPath, "Viewbox scaling is unsupported; imports as a Panel.");
    }
    else
    {
      // unknown element: same-folder UserControl reference?
      plStringBuilder sCandidate = ctx.m_sFolderAbs;
      sCandidate.AppendPath(sTag);
      sCandidate.Append(".xaml");
      sCandidate.MakeCleanPath();

      if (plOSFile::ExistsFile(sCandidate))
      {
        plVec2 vReferenceSize(200.0f, 50.0f);
        TryGetCustomControlReferenceSize(node, ctx, vReferenceSize);

        const float fDefaultWidth = plMath::Max(vReferenceSize.x, ParseFloatAttr(eff.Get("MinWidth"), 0.0f));
        const float fDefaultHeight = plMath::Max(vReferenceSize.y, ParseFloatAttr(eff.Get("MinHeight"), 0.0f));
        const plString sGuid = ImportXamlFileAsDocument(sCandidate, ctx);
        if (!sGuid.IsEmpty())
        {
          plUiImportNode* pSub = ref_irParent.AddChild(plGetStaticRTTI<plUiSubDocument>(), GetXamlElementName(node));
          pSub->Set("Document", sGuid.GetView());

          // honor the reference's visibility (e.g. inactive menu screens start Hidden)
          const plStringView sVisibility = eff.Get("Visibility");
          if (sVisibility == "Hidden" || sVisibility == "Collapsed")
          {
            pSub->Set("Visible", false);
          }
          else if (sVisibility.StartsWith("{"))
          {
            MarkupExtension visExt = ParseMarkupExtension(sVisibility);
            if (visExt.m_Kind == MarkupExtension::Kind::Binding && !visExt.m_sArg.IsEmpty())
              pSub->Set("VisibleBinding", visExt.m_sArg.GetView());
          }

          if (ctx.m_pCurrentPlacements != nullptr)
          {
            XamlSubDocumentPlacement& placementInfo = ctx.m_pCurrentPlacements->ExpandAndGetRef();
            placementInfo.m_pNode = pSub;
            placementInfo.m_sSourcePath = sCandidate;
            placementInfo.m_bInitiallyVisible = sVisibility != "Hidden" && sVisibility != "Collapsed" && !sVisibility.StartsWith("{");
          }

          if (placement == PlacementMode::Stack)
          {
            pSub->m_bStackChild = true;
            pSub->Set("LayoutMargin", ParseThickness(eff.Get("Margin", "0")));
            pSub->m_vOffsets = plVec4(0, 0, ParseFloatAttr(eff.Get("Width"), fDefaultWidth), ParseFloatAttr(eff.Get("Height"), fDefaultHeight));
          }
          else if (placement == PlacementMode::Canvas)
          {
            pSub->m_vAnchorMin = pSub->m_vAnchorMax = plVec2::MakeZero();
            pSub->m_vOffsets = plVec4(ParseFloatAttr(node.GetAttribute("Canvas.Left"), 0.0f), ParseFloatAttr(node.GetAttribute("Canvas.Top"), 0.0f),
              ParseFloatAttr(eff.Get("Width"), fDefaultWidth), ParseFloatAttr(eff.Get("Height"), fDefaultHeight));
          }
          else
          {
            ApplyAlignmentPlacement(*pSub, eff, fDefaultWidth, fDefaultHeight);
          }
          ApplyXamlTransforms(node, *pSub, eff, ctx, sPath);
          return;
        }
      }

      plStringBuilder sMsg;
      sMsg.SetFormat("Unknown element <{}> imports as a Panel (children preserved).", sTag);
      ctx.m_pReport->Warning(sPath, sMsg);
      pType = plGetStaticRTTI<plUiPanel>();
    }

    plUiImportNode* pIr = ref_irParent.AddChild(pType, GetXamlElementName(node));

    // ————— placement —————
    const float fDefaultW = plMath::Max(EstimateXamlWidth(node, 0, &ctx), ParseFloatAttr(eff.Get("MinWidth"), 0.0f));
    const float fDefaultH = plMath::Max(EstimateXamlHeight(node, 0, &ctx), ParseFloatAttr(eff.Get("MinHeight"), 0.0f));

    switch (placement)
    {
      case PlacementMode::Stack:
      {
        pIr->m_bStackChild = true;

        // Stack layout consumes the XAML margin independently from the preferred size,
        // including cross-axis insets and asymmetric main-axis gaps.
        const plVec4 vMargin = ParseThickness(eff.Get("Margin", "0"));
        const float fW = plMath::Max(ParseFloatAttr(eff.Get("Width"), fDefaultW), 1.0f);
        const float fH = plMath::Max(ParseFloatAttr(eff.Get("Height"), fDefaultH), 1.0f);
        pIr->m_vOffsets = plVec4(0, 0, fW, fH);
        pIr->Set("LayoutMargin", vMargin);

        if (eff.Get("Width").StartsWith("{") || eff.Get("Height").StartsWith("{"))
          ctx.m_pReport->Info(sPath, "The size is computed at runtime in XAML (ActualWidth/Height binding); a placeholder size imported.");
        break;
      }

      case PlacementMode::Canvas:
      {
        const plStringView sLeft = node.GetAttribute("Canvas.Left");
        const plStringView sRight = node.GetAttribute("Canvas.Right");
        const plStringView sTop = node.GetAttribute("Canvas.Top");
        const plStringView sBottom = node.GetAttribute("Canvas.Bottom");
        const float fWidth = ParseFloatAttr(eff.Get("Width"), fDefaultW);
        const float fHeight = ParseFloatAttr(eff.Get("Height"), fDefaultH);

        pIr->m_vAnchorMin = pIr->m_vAnchorMax = plVec2::MakeZero();
        pIr->m_vOffsets = plVec4(0.0f, 0.0f, fWidth, fHeight);

        if (!sLeft.IsEmpty())
        {
          pIr->m_vOffsets.x = ParseFloatAttr(sLeft, 0.0f);

          if (ParseMarkupExtension(sLeft).m_Kind != MarkupExtension::Kind::None)
            ctx.m_pReport->Info(sPath, "Canvas.Left binding cannot be evaluated at import time; a zero offset is used.");
        }
        else if (!sRight.IsEmpty())
        {
          pIr->m_vAnchorMin.x = pIr->m_vAnchorMax.x = 1.0f;
          pIr->m_vOffsets.x = -fWidth - ParseFloatAttr(sRight, 0.0f);

          if (ParseMarkupExtension(sRight).m_Kind != MarkupExtension::Kind::None)
            ctx.m_pReport->Info(sPath, "Canvas.Right binding cannot be evaluated at import time; the element is right-aligned using its placeholder size.");
        }

        if (!sTop.IsEmpty())
        {
          pIr->m_vOffsets.y = ParseFloatAttr(sTop, 0.0f);

          if (ParseMarkupExtension(sTop).m_Kind != MarkupExtension::Kind::None)
            ctx.m_pReport->Info(sPath, "Canvas.Top binding cannot be evaluated at import time; a zero offset is used.");
        }
        else if (!sBottom.IsEmpty())
        {
          pIr->m_vAnchorMin.y = pIr->m_vAnchorMax.y = 1.0f;
          pIr->m_vOffsets.y = -fHeight - ParseFloatAttr(sBottom, 0.0f);

          if (ParseMarkupExtension(sBottom).m_Kind != MarkupExtension::Kind::None)
            ctx.m_pReport->Info(sPath, "Canvas.Bottom binding cannot be evaluated at import time; the element is bottom-aligned using its placeholder size.");
        }

        if (ParseMarkupExtension(eff.Get("Width")).m_Kind != MarkupExtension::Kind::None ||
            ParseMarkupExtension(eff.Get("Height")).m_Kind != MarkupExtension::Kind::None)
          ctx.m_pReport->Info(sPath, "Canvas child size is computed at runtime in XAML; a placeholder size imported.");
        break;
      }

      case PlacementMode::GridCell:
        pIr->m_vAnchorMin = plVec2::MakeZero();
        pIr->m_vAnchorMax = plVec2(1.0f, 1.0f);
        pIr->m_vOffsets = plVec4::MakeZero(); // the grid stretches children into their cells
        break;

      case PlacementMode::Free:
        ApplyAlignmentPlacement(*pIr, eff, fDefaultW, fDefaultH);
        break;
    }

    ApplyXamlTransforms(node, *pIr, eff, ctx, sPath);

    // ————— shared visuals —————
    const bool bIsVisualType = pType->IsDerivedFrom(plGetStaticRTTI<plUiVisualElement>());
    const bool bIsButtonType = pType->IsDerivedFrom(plGetStaticRTTI<plUiButton>());
    if (bIsVisualType)
    {
      const bool bContainerType = pType == plGetStaticRTTI<plUiPanel>() || pType == plGetStaticRTTI<plUiStackPanel>() || pType == plGetStaticRTTI<plUiGridPanel>() || pType == plGetStaticRTTI<plUiScrollView>();

      // buttons whose XAML defines no background got their look from a ControlTemplate we
      // cannot translate - a transparent base with subtle hover/press overlays is much
      // closer than the widget's default white slab
      const bool bHasBackground = !eff.Get("Background").IsEmpty() || FindPropertyElement(node, "Background") != nullptr;

      ApplyCommonVisuals(*pIr, eff, ctx, sPath, bContainerType || (bIsButtonType && !bHasBackground));
      ApplyBrushPropertyElement(node, *pIr, ctx, sPath);

      if (bIsButtonType && !bHasBackground)
      {
        pIr->Set("HoverColor", plColorGammaUB(255, 255, 255, 40));
        pIr->Set("PressedColor", plColorGammaUB(255, 255, 255, 80));
      }
    }
    else if (pType == plGetStaticRTTI<plUiText>())
    {
      // Visibility on text elements
      const plStringView sVisibility = eff.Get("Visibility");
      if (sVisibility == "Collapsed" || sVisibility == "Hidden")
        pIr->Set("Visible", false);
      else if (sVisibility.StartsWith("{"))
      {
        MarkupExtension ext = ParseMarkupExtension(sVisibility);
        if (ext.m_Kind == MarkupExtension::Kind::Binding && !ext.m_sArg.IsEmpty())
          pIr->Set("VisibleBinding", ext.m_sArg.GetView());
      }
    }

    // ————— per-type content —————
    if (pType == plGetStaticRTTI<plUiText>())
    {
      plStringView sText = eff.Get("Text");
      if (sText.IsEmpty() && !node.m_sText.IsEmpty())
        sText = node.m_sText.GetView();

      ApplyStringOrBinding(*pIr, sText, "Text", "TextBinding", ctx, sPath);
      ApplyTextVisuals(*pIr, eff, ctx, sPath);

      const plStringView sAlign = eff.Get("TextAlignment", eff.Get("HorizontalContentAlignment"));
      if (sAlign == "Center")
        pIr->Set("HAlign", (plInt32)plUiTextHAlign::Center);
      else if (sAlign == "Right")
        pIr->Set("HAlign", (plInt32)plUiTextHAlign::Right);

      const plStringView sVAlign = eff.Get("VerticalAlignment");
      if (sVAlign == "Center")
        pIr->Set("VAlign", (plInt32)plUiTextVAlign::Center);
      else if (sVAlign == "Bottom")
        pIr->Set("VAlign", (plInt32)plUiTextVAlign::Bottom);

      if (eff.Get("TextWrapping") == "Wrap")
        pIr->Set("Wrap", true);
      if (eff.Get("TextTrimming").FindSubString("Ellipsis") != nullptr)
        pIr->Set("Overflow", (plInt32)plUiTextOverflow::Ellipsis);
      return;
    }

    if (pType == plGetStaticRTTI<plUiImage>())
    {
      const plStringView sSource = eff.Get("Source");
      plStringBuilder sAbs;
      if (!sSource.IsEmpty() && ResolveImageFile(sSource, ctx, sPath, sAbs))
      {
        pIr->m_sTextureFile = sAbs;
        pIr->Set("BrushMode", (plInt32)plUiBrushMode::Image);

        // unsized images (stack/canvas placement) prefer their natural pixel size
        // over the generic defaults
        if ((placement == PlacementMode::Stack || placement == PlacementMode::Canvas) &&
            eff.Get("Width").IsEmpty() && eff.Get("Height").IsEmpty())
        {
          plImage image;
          if (image.LoadFrom(sAbs).Succeeded())
          {
            pIr->m_vOffsets.z = static_cast<float>(image.GetWidth());
            pIr->m_vOffsets.w = static_cast<float>(image.GetHeight());
          }
        }
      }

      const plStringView sStretch = eff.Get("Stretch");
      if (!sStretch.IsEmpty() && sStretch != "Fill")
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Image Stretch='{}' is unsupported; the texture stretches over the rect.", sStretch);
        ctx.m_pReport->Info(sPath, sMsg);
      }
      return;
    }

    if (pType == plGetStaticRTTI<plUiButton>() || pType == plGetStaticRTTI<plUiToggle>())
    {
      const plString sName = GetXamlElementName(node);
      if (node.FindAttribute("x:Name") != nullptr || node.FindAttribute("Name") != nullptr)
        pIr->Set("ClickEvent", sName.GetView());

      if (!eff.Get("Command").IsEmpty())
      {
        MarkupExtension cmd = ParseMarkupExtension(eff.Get("Command"));
        if (cmd.m_Kind == MarkupExtension::Kind::Binding && !cmd.m_sArg.IsEmpty())
        {
          pIr->Set("ClickEvent", cmd.m_sArg.GetView());
          ctx.m_pReport->Info(sPath, "Command binding imports as the ClickEvent name (handle plMsgUiEvent in code).");
        }
      }

      if (pType == plGetStaticRTTI<plUiToggle>())
      {
        const plStringView sChecked = eff.Get("IsChecked");
        MarkupExtension ext = ParseMarkupExtension(sChecked);
        if (ext.m_Kind == MarkupExtension::Kind::Binding && !ext.m_sArg.IsEmpty())
          pIr->Set("ClickEntryBinding", ext.m_sArg.GetView());
        else if (!sChecked.IsEmpty())
          ctx.m_pReport->Info(sPath, "Static IsChecked has no authored equivalent; bind ClickEntryBinding instead.");
      }

      // content: attribute, inner text or a single child element
      const plStringView sContent = eff.Get("Content");
      plStringView sLabelText = sContent.IsEmpty() ? node.m_sText.GetView() : sContent;

      plInt32 iLabelAlign = plUiTextHAlign::Center;
      {
        const plStringView sContentAlign = eff.Get("HorizontalContentAlignment");
        if (sContentAlign == "Left")
          iLabelAlign = plUiTextHAlign::Left;
        else if (sContentAlign == "Right")
          iLabelAlign = plUiTextHAlign::Right;
      }

      MarkupExtension contentExt = ParseMarkupExtension(sLabelText);
      if (contentExt.m_Kind == MarkupExtension::Kind::Binding)
      {
        plUiImportNode* pLabel = pIr->AddChild(plGetStaticRTTI<plUiText>(), "Label");
        pLabel->m_vAnchorMin = plVec2::MakeZero();
        pLabel->m_vAnchorMax = plVec2(1.0f, 1.0f);
        pLabel->m_vOffsets = plVec4::MakeZero();
        pLabel->Set("TextBinding", contentExt.m_sArg.GetView());
        pLabel->Set("HAlign", iLabelAlign);
        pLabel->Set("VAlign", (plInt32)plUiTextVAlign::Center);
        ApplyTextVisuals(*pLabel, eff, ctx, sPath);
      }
      else if (!sLabelText.IsEmpty())
      {
        plUiImportNode* pLabel = pIr->AddChild(plGetStaticRTTI<plUiText>(), "Label");
        pLabel->m_vAnchorMin = plVec2::MakeZero();
        pLabel->m_vAnchorMax = plVec2(1.0f, 1.0f);
        pLabel->m_vOffsets = plVec4::MakeZero();
        pLabel->Set("Text", sLabelText);
        pLabel->Set("HAlign", iLabelAlign);
        pLabel->Set("VAlign", (plInt32)plUiTextVAlign::Center);
        ApplyTextVisuals(*pLabel, eff, ctx, sPath);
      }

      BuildContentChildren(node, ctx, *pIr, PlacementMode::Free, sPath);
      return;
    }

    if (pType == plGetStaticRTTI<plUiSlider>())
    {
      pIr->Set("Min", ParseFloatAttr(eff.Get("Minimum"), 0.0f));
      pIr->Set("Max", ParseFloatAttr(eff.Get("Maximum"), 100.0f));

      const plStringView sValue = eff.Get("Value");
      MarkupExtension ext = ParseMarkupExtension(sValue);
      if (ext.m_Kind == MarkupExtension::Kind::Binding && !ext.m_sArg.IsEmpty())
        pIr->Set("ValueBinding", ext.m_sArg.GetView());
      else if (!sValue.IsEmpty())
        pIr->Set("Value", ParseFloatAttr(sValue, 0.0f));
      return;
    }

    if (pType == plGetStaticRTTI<plUiProgressBar>())
    {
      const float fMin = ParseFloatAttr(eff.Get("Minimum"), 0.0f);
      const float fMax = ParseFloatAttr(eff.Get("Maximum"), 100.0f);

      const plStringView sValue = eff.Get("Value");
      MarkupExtension ext = ParseMarkupExtension(sValue);
      if (ext.m_Kind == MarkupExtension::Kind::Binding && !ext.m_sArg.IsEmpty())
      {
        pIr->Set("FillAmountBinding", ext.m_sArg.GetView());
        pIr->Set("BindingMin", fMin);
        pIr->Set("BindingMax", fMax);
      }
      else if (!sValue.IsEmpty())
      {
        const float fValue = ParseFloatAttr(sValue, 0.0f);
        pIr->Set("FillAmount", fMax > fMin ? (fValue - fMin) / (fMax - fMin) : 0.0f);
      }

      plColorGammaUB col;
      if (!eff.Get("Foreground").IsEmpty() && ResolveBrushColor(eff.Get("Foreground"), ctx, sPath, col).Succeeded())
        pIr->Set("FillColor", col);
      return;
    }

    if (pType == plGetStaticRTTI<plUiTextInput>())
    {
      ApplyStringOrBinding(*pIr, eff.Get("Text"), "Text", "TextBinding", ctx, sPath);
      ApplyTextVisuals(*pIr, eff, ctx, sPath);

      const float fMaxLength = ParseFloatAttr(eff.Get("MaxLength"), 0.0f);
      if (fMaxLength > 0.0f)
        pIr->Set("MaxLength", static_cast<plInt32>(fMaxLength));

      if (sTag == "PasswordBox")
        ctx.m_pReport->Info(sPath, "PasswordBox imports as a plain TextInput (no masking support).");
      return;
    }

    if (pType == plGetStaticRTTI<plUiItemList>())
    {
      // ItemsSource -> ItemsBinding
      const plStringView sItemsSource = eff.Get("ItemsSource");
      MarkupExtension ext = ParseMarkupExtension(sItemsSource);
      if (ext.m_Kind == MarkupExtension::Kind::Binding && !ext.m_sArg.IsEmpty())
        pIr->Set("ItemsBinding", ext.m_sArg.GetView());
      else
        ctx.m_pReport->Warning(sPath, "ItemsControl without an ItemsSource binding; set ItemsBinding after import.");

      const plStringView sSelectedIndex = eff.Get("SelectedIndex");
      MarkupExtension selExt = ParseMarkupExtension(sSelectedIndex);
      if (selExt.m_Kind == MarkupExtension::Kind::Binding && !selExt.m_sArg.IsEmpty())
        pIr->Set("SelectedBinding", selExt.m_sArg.GetView());

      // inline ItemTemplate -> separate row document
      const plUiMarkupNode* pTemplateProp = FindPropertyElement(node, "ItemTemplate");
      const plUiMarkupNode* pDataTemplate = nullptr;
      if (pTemplateProp != nullptr)
      {
        for (const plUiMarkupNode* pChild : pTemplateProp->m_Children)
        {
          if (pChild->GetLocalTag() == "DataTemplate")
          {
            pDataTemplate = pChild;
            break;
          }
        }
      }

      if (pDataTemplate != nullptr && !pDataTemplate->m_Children.IsEmpty())
      {
        plUiImportDocument templateDoc;
        templateDoc.m_vReferenceSize = plVec2(400.0f, 60.0f);

        plStringBuilder sTemplatePath = ctx.m_sFolderAbs;
        plStringBuilder sTemplateName;
        sTemplateName.SetFormat("{}_{}Row", ctx.m_sMainName, GetXamlElementName(node));
        ctx.m_iDocCounter++;
        sTemplatePath.AppendPath(sTemplateName);
        sTemplatePath.Append(".plUiDocumentAsset");
        templateDoc.m_sTargetPathAbs = sTemplatePath;
        templateDoc.m_Root.m_sName = sTemplateName;

        for (const plUiMarkupNode* pTemplateChild : pDataTemplate->m_Children)
        {
          if (!IsPropertyElement(*pTemplateChild))
            BuildElement(*pTemplateChild, ctx, templateDoc.m_Root, PlacementMode::Free, sPath);
        }

        // template roots should fill the row
        for (plUiImportNode* pRootChild : templateDoc.m_Root.m_Children)
        {
          pRootChild->m_vAnchorMin = plVec2::MakeZero();
          pRootChild->m_vAnchorMax = plVec2(1.0f, 1.0f);
          pRootChild->m_vOffsets = plVec4::MakeZero();
        }

        const plUInt32 uiDocIndexBefore = ctx.m_pOutDocs->GetCount();
        const plStatus status = ctx.m_pWriter->WriteDocument(templateDoc, *ctx.m_pOutDocs);

        if (status.Succeeded() && ctx.m_pOutDocs->GetCount() > uiDocIndexBefore)
        {
          plStringBuilder sGuid;
          plConversionUtils::ToString((*ctx.m_pOutDocs)[uiDocIndexBefore]->GetGuid(), sGuid);
          pIr->Set("ItemTemplate", sGuid.GetView());
          ctx.m_pReport->Info(sPath, "The DataTemplate imported as a separate row document (see ItemTemplate).");
        }
        else
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("Item template document could not be created: {}", status.m_sMessage);
          ctx.m_pReport->Warning(sPath, sMsg);
        }
      }
      else
      {
        ctx.m_pReport->Warning(sPath, "No inline ItemTemplate found; assign an Item Template document after import.");
      }
      return;
    }

    // ————— vector shapes (Path/Polygon -> tessellated plUiPath) —————
    if (pType == plGetStaticRTTI<plUiPath>())
    {
      if (!eff.Get("Fill").IsEmpty())
        ApplyVisualBrush(*pIr, eff.Get("Fill"), ctx, sPath);

      plString sPathData;

      if (sTag == "Polygon" || sTag == "Polyline")
      {
        // Points="x1,y1 x2,y2 ..." -> "M x1,y1 L x2,y2 ... Z"
        const plStringView sPoints = eff.Get("Points");
        if (!sPoints.IsEmpty())
        {
          plStringBuilder sData("M ", sPoints);
          if (sTag == "Polygon")
            sData.Append(" Z");
          sPathData = sData;
        }
      }
      else
      {
        plStringView sData = eff.Get("Data");

        MarkupExtension dataExt = ParseMarkupExtension(sData);
        if (dataExt.m_Kind == MarkupExtension::Kind::StaticResource)
        {
          if (const plString* pGeometry = ctx.m_GeometryResources.GetValue(dataExt.m_sArg))
          {
            sPathData = *pGeometry;
          }
          else
          {
            plStringBuilder sMsg;
            sMsg.SetFormat("Geometry resource '{}' not found; the shape imports empty.", dataExt.m_sArg);
            ctx.m_pReport->Warning(sPath, sMsg);
          }
        }
        else if (dataExt.m_Kind == MarkupExtension::Kind::None && !sData.IsEmpty())
        {
          sPathData = sData;
        }

        // property-element form: <Path.Data><PathGeometry Figures="..."/></Path.Data>
        if (sPathData.IsEmpty())
        {
          if (const plUiMarkupNode* pDataProp = FindPropertyElement(node, "Data"))
          {
            for (const plUiMarkupNode* pGeom : pDataProp->m_Children)
            {
              plStringBuilder sGeomData = pGeom->m_sText.GetView();
              if (sGeomData.IsEmpty())
                sGeomData = pGeom->GetAttribute("Figures");
              sGeomData.Trim(" \t\r\n");

              if (!sGeomData.IsEmpty())
              {
                sPathData = sGeomData;
                break;
              }
            }
          }
        }
      }

      if (!sPathData.IsEmpty())
      {
        pIr->Set("PathData", sPathData.GetView());
      }
      else
      {
        ctx.m_pReport->Warning(sPath, "The shape has no resolvable geometry data.");
      }

      if (!eff.Get("Stroke").IsEmpty())
        ctx.m_pReport->Info(sPath, "Path strokes are unsupported (fill only).");

      return;
    }

    // ————— simple shapes (colored-panel approximation) —————
    if (sTag == "Rectangle" || sTag == "Ellipse" || sTag == "Line")
    {
      plColorGammaUB col;

      if (!eff.Get("Fill").IsEmpty())
        ApplyVisualBrush(*pIr, eff.Get("Fill"), ctx, sPath);

      if (!eff.Get("Stroke").IsEmpty() && ResolveBrushColor(eff.Get("Stroke"), ctx, sPath, col).Succeeded())
      {
        pIr->Set("BorderColor", col);
        pIr->Set("BorderWidth", ParseFloatAttr(eff.Get("StrokeThickness"), 1.0f));
      }

      if (sTag == "Rectangle")
      {
        const float fRadius = plMath::Max(ParseFloatAttr(eff.Get("RadiusX"), 0.0f), ParseFloatAttr(eff.Get("RadiusY"), 0.0f));
        if (fRadius > 0.0f)
          pIr->Set("CornerRadius", plVec4(fRadius));
      }
      else if (sTag == "Ellipse")
      {
        // rounded-rect approximation: radius = half the smaller known dimension
        const float fW = ParseFloatAttr(eff.Get("Width"), 0.0f);
        const float fH = ParseFloatAttr(eff.Get("Height"), 0.0f);
        float fRadius = 500.0f;
        if (fW > 0.0f && fH > 0.0f)
          fRadius = plMath::Min(fW, fH) * 0.5f;
        else if (fW > 0.0f || fH > 0.0f)
          fRadius = plMath::Max(fW, fH) * 0.5f;
        pIr->Set("CornerRadius", plVec4(fRadius));
      }
      return;
    }

    // ————— containers —————
    if (bGridFractional)
    {
      // children build with their own alignment/margins, then remap into their cell:
      // pos = anchorFrac * parentSize + pxOffset is exact for star/px definitions
      const plUInt32 uiRowCells = gridRowStops.GetCount() - 1;
      const plUInt32 uiColCells = gridColStops.GetCount() - 1;

      // one axis: remap the child's 0..1-relative anchors/offsets into the cell range
      // given by (frac0,px0)-(frac1,px1); handles point anchors (z/w = size) and the
      // degenerate stretch-into-fixed-px-cell case
      auto remapAxis = [](float fFrac0, float fFrac1, float fPx0, float fPx1, float& ref_fAnchorMin, float& ref_fAnchorMax, float& ref_fOffsetMin, float& ref_fOffsetMaxOrSize) {
        const bool bPoint = ref_fAnchorMin == ref_fAnchorMax;
        const float fNewMin = fFrac0 + ref_fAnchorMin * (fFrac1 - fFrac0);
        const float fNewMax = fFrac0 + ref_fAnchorMax * (fFrac1 - fFrac0);

        if (!bPoint && fNewMin == fNewMax)
        {
          // stretch child inside a purely fixed-px span: convert to point anchor + size
          const float fCellPx = fPx1 - fPx0;
          const float fSize = plMath::Max(fCellPx - ref_fOffsetMin + ref_fOffsetMaxOrSize, 1.0f);
          ref_fOffsetMin += fPx0;
          ref_fOffsetMaxOrSize = fSize;
          ref_fAnchorMin = ref_fAnchorMax = fNewMin;
          return;
        }

        ref_fOffsetMin += fPx0 + ref_fAnchorMin * (fPx1 - fPx0);
        if (!bPoint)
          ref_fOffsetMaxOrSize += fPx0 + ref_fAnchorMax * (fPx1 - fPx0);

        ref_fAnchorMin = fNewMin;
        ref_fAnchorMax = fNewMax;
      };

      for (const plUiMarkupNode* pChild : node.m_Children)
      {
        if (IsPropertyElement(*pChild))
          continue;

        const plUInt32 uiBefore = pIr->m_Children.GetCount();
        BuildElement(*pChild, ctx, *pIr, PlacementMode::Free, sPath);

        if (pIr->m_Children.GetCount() > uiBefore)
        {
          const plUInt32 uiRow = plMath::Clamp<plUInt32>(static_cast<plUInt32>(ParseFloatAttr(pChild->GetAttribute("Grid.Row", "0"), 0.0f)), 0, uiRowCells - 1);
          const plUInt32 uiCol = plMath::Clamp<plUInt32>(static_cast<plUInt32>(ParseFloatAttr(pChild->GetAttribute("Grid.Column", "0"), 0.0f)), 0, uiColCells - 1);
          const plUInt32 uiRowSpan = plMath::Clamp<plUInt32>(static_cast<plUInt32>(ParseFloatAttr(pChild->GetAttribute("Grid.RowSpan", "1"), 1.0f)), 1, uiRowCells - uiRow);
          const plUInt32 uiColSpan = plMath::Clamp<plUInt32>(static_cast<plUInt32>(ParseFloatAttr(pChild->GetAttribute("Grid.ColumnSpan", "1"), 1.0f)), 1, uiColCells - uiCol);

          plUiImportNode* pChildIr = pIr->m_Children.PeekBack();

          remapAxis(gridColStops[uiCol], gridColStops[uiCol + uiColSpan], gridColPx[uiCol], gridColPx[uiCol + uiColSpan],
            pChildIr->m_vAnchorMin.x, pChildIr->m_vAnchorMax.x, pChildIr->m_vOffsets.x, pChildIr->m_vOffsets.z);
          remapAxis(gridRowStops[uiRow], gridRowStops[uiRow + uiRowSpan], gridRowPx[uiRow], gridRowPx[uiRow + uiRowSpan],
            pChildIr->m_vAnchorMin.y, pChildIr->m_vAnchorMax.y, pChildIr->m_vOffsets.y, pChildIr->m_vOffsets.w);
        }
      }
      return;
    }

    if (bStackLike)
    {
      // XAML StackPanel has no implicit gap; authored margins provide all separation.
      pIr->Set("Spacing", 0.0f);

      if (sTag == "StackPanel" && eff.Get("Orientation") == "Horizontal")
        pIr->Set("Direction", (plInt32)plUiStackDirection::Horizontal);

      BuildContentChildren(node, ctx, *pIr, PlacementMode::Stack, sPath);
      return;
    }

    if (bCanvasLike)
    {
      BuildContentChildren(node, ctx, *pIr, PlacementMode::Canvas, sPath);
      return;
    }

    // plain Panel (Border, ContentControl, known control wrappers, unknown)
    const plUInt32 uiContentStart = pIr->m_Children.GetCount();

    if (sTag == "HeaderedContentControl")
    {
      const MarkupExtension templateExt = ParseMarkupExtension(eff.Get("Template"));
      if (templateExt.m_Kind == MarkupExtension::Kind::StaticResource)
      {
        if (const float* pWidth = ctx.m_TemplateContentWidths.GetValue(templateExt.m_sArg))
          fHeaderedContentWidth = *pWidth;
      }

      const plStringView sHeader = eff.Get("Header");
      MarkupExtension headerExt = ParseMarkupExtension(sHeader);

      plUiImportNode* pHeader = pIr->AddChild(plGetStaticRTTI<plUiText>(), "Header");
      pHeader->m_vAnchorMin = plVec2::MakeZero();
      pHeader->m_vAnchorMax = plVec2(fHeaderedContentWidth > 0.0f ? 1.0f : 0.65f, 1.0f);
      pHeader->m_vOffsets = plVec4(0.0f, 0.0f, fHeaderedContentWidth > 0.0f ? -fHeaderedContentWidth : 0.0f, 0.0f);
      pHeader->Set("VAlign", (plInt32)plUiTextVAlign::Center);

      if (headerExt.m_Kind == MarkupExtension::Kind::Binding && !headerExt.m_sArg.IsEmpty())
        pHeader->Set("TextBinding", headerExt.m_sArg.GetView());
      else if (headerExt.m_Kind == MarkupExtension::Kind::None && !sHeader.IsEmpty())
        pHeader->Set("Text", sHeader);

      ApplyTextVisuals(*pHeader, eff, ctx, sPath);
    }

    if (sTag == "Border")
    {
      const plStringView sPadding = eff.Get("Padding");
      PL_IGNORE_UNUSED(sPadding);
    }

    // ContentControl/Label-style text content
    {
      const plStringView sContent = eff.Get("Content");
      MarkupExtension contentExt = ParseMarkupExtension(sContent);

      if (contentExt.m_Kind == MarkupExtension::Kind::Binding && !contentExt.m_sArg.IsEmpty())
      {
        plUiImportNode* pText = pIr->AddChild(plGetStaticRTTI<plUiText>(), "Content");
        pText->m_vAnchorMin = plVec2::MakeZero();
        pText->m_vAnchorMax = plVec2(1.0f, 1.0f);
        pText->m_vOffsets = plVec4::MakeZero();
        pText->Set("TextBinding", contentExt.m_sArg.GetView());
        pText->Set("VAlign", (plInt32)plUiTextVAlign::Center);
        ApplyTextVisuals(*pText, eff, ctx, sPath);
      }
      else if (contentExt.m_Kind == MarkupExtension::Kind::None && !sContent.IsEmpty())
      {
        plUiImportNode* pText = pIr->AddChild(plGetStaticRTTI<plUiText>(), "Content");
        pText->m_vAnchorMin = plVec2::MakeZero();
        pText->m_vAnchorMax = plVec2(1.0f, 1.0f);
        pText->m_vOffsets = plVec4::MakeZero();
        pText->Set("Text", sContent);
        pText->Set("VAlign", (plInt32)plUiTextVAlign::Center);
        pText->Set("Wrap", true);
        ApplyTextVisuals(*pText, eff, ctx, sPath);
      }
    }

    BuildContentChildren(node, ctx, *pIr, PlacementMode::Free, sPath);

    if (sTag == "HeaderedContentControl")
    {
      // Keep the header on the left and put source children in the template's content
      // column. Without a recoverable fixed-width hint, use a proportional right slot.
      for (plUInt32 i = uiContentStart + 1; i < pIr->m_Children.GetCount(); ++i)
      {
        plUiImportNode* pContent = pIr->m_Children[i];
        if (fHeaderedContentWidth > 0.0f)
        {
          pContent->m_vAnchorMin.x = pContent->m_vAnchorMax.x = 1.0f;
          pContent->m_vOffsets.x = -fHeaderedContentWidth;
          pContent->m_vOffsets.z = fHeaderedContentWidth;
        }
        else
        {
          pContent->m_vAnchorMin.x = 0.65f;
          pContent->m_vAnchorMax.x = 1.0f;
          pContent->m_vOffsets.x = 0.0f;
          pContent->m_vOffsets.z = 0.0f;
        }
      }
    }

    if (sTag == "Border")
    {
      const plStringView sPadding = eff.Get("Padding");
      if (!sPadding.IsEmpty())
      {
        const plVec4 vPadding = ParseThickness(sPadding);

        // Child placement is authored against the padded content box in WPF. Fold that
        // content-box transform into every imported child's anchor offsets.
        for (plUInt32 i = uiContentStart; i < pIr->m_Children.GetCount(); ++i)
        {
          plUiImportNode* pChild = pIr->m_Children[i];
          const bool bPointX = pChild->m_vAnchorMin.x == pChild->m_vAnchorMax.x;
          const bool bPointY = pChild->m_vAnchorMin.y == pChild->m_vAnchorMax.y;

          pChild->m_vOffsets.x += vPadding.x * (1.0f - pChild->m_vAnchorMin.x) - vPadding.z * pChild->m_vAnchorMin.x;
          pChild->m_vOffsets.y += vPadding.y * (1.0f - pChild->m_vAnchorMin.y) - vPadding.w * pChild->m_vAnchorMin.y;

          if (!bPointX)
            pChild->m_vOffsets.z += vPadding.x * (1.0f - pChild->m_vAnchorMax.x) - vPadding.z * pChild->m_vAnchorMax.x;
          if (!bPointY)
            pChild->m_vOffsets.w += vPadding.y * (1.0f - pChild->m_vAnchorMax.y) - vPadding.w * pChild->m_vAnchorMax.y;
        }
      }
    }
  }

  const plUiMarkupNode* FindDocumentRoot(const plUiMarkupNode& parseRoot)
  {
    for (const plUiMarkupNode* pChild : parseRoot.m_Children)
    {
      const plStringView sTag = pChild->GetLocalTag();
      if (sTag == "UserControl" || sTag == "Page" || sTag == "Window" || sTag == "Grid" || sTag == "Canvas" || sTag == "StackPanel" || sTag == "ResourceDictionary" || sTag == "Application")
        return pChild;
    }
    return parseRoot.m_Children.IsEmpty() ? nullptr : parseRoot.m_Children[0];
  }

  plString FindStoryboardAction(const plUiMarkupNode& triggerNode)
  {
    for (const plUiMarkupNode* pChild : triggerNode.m_Children)
    {
      if (pChild->GetLocalTag() == "ControlStoryboardAction" || pChild->GetLocalTag() == "BeginStoryboard")
      {
        const MarkupExtension storyboard = ParseMarkupExtension(pChild->GetAttribute("Storyboard"));
        if (storyboard.m_Kind == MarkupExtension::Kind::StaticResource)
          return storyboard.m_sArg;
      }

      const plString sNested = FindStoryboardAction(*pChild);
      if (!sNested.IsEmpty())
        return sNested;
    }

    return {};
  }

  bool HasAnimation(const plUiImportDocument& importDoc, plStringView sName)
  {
    for (const plUiImportAnimation& animation : importDoc.m_Animations)
    {
      if (animation.m_sName == sName)
        return true;
    }
    return false;
  }

  plString NormalizeTriggerValue(plStringView sValue)
  {
    MarkupExtension ext = ParseMarkupExtension(sValue);
    plStringBuilder sResult = ext.m_Kind == MarkupExtension::Kind::Other ? ext.m_sArg.GetView() : sValue;
    sResult.Trim(" \t{}");
    sResult.TrimWordStart("x:Static");
    sResult.Trim(" \t");

    if (const char* szDot = sResult.FindLastSubString("."))
      sResult = plStringView(szDot + 1);

    return plString(sResult.GetView());
  }

  void CollectCommandBindings(const plUiMarkupNode& node, plHybridArray<plString, 8>& out_commands)
  {
    const plStringView sTag = node.GetLocalTag();
    if (sTag == "Button" || sTag == "ToggleButton" || sTag == "RadioButton" || sTag == "RepeatButton")
    {
      const MarkupExtension command = ParseMarkupExtension(node.GetAttribute("Command"));
      if (command.m_Kind == MarkupExtension::Kind::Binding && !command.m_sArg.IsEmpty() && !out_commands.Contains(command.m_sArg))
      {
        out_commands.PushBack(command.m_sArg);
      }
    }

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      CollectCommandBindings(*pChild, out_commands);
    }
  }

  const plUiMarkupNode* FindStoryboardResource(const plUiMarkupNode& node, plStringView sName)
  {
    if (node.GetLocalTag() == "Storyboard" && node.GetAttribute("x:Key") == sName)
      return &node;

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (const plUiMarkupNode* pFound = FindStoryboardResource(*pChild, sName))
        return pFound;
    }

    return nullptr;
  }

  float ComputeStoryboardDuration(const plUiMarkupNode& node)
  {
    float fDuration = ParseKeyTime(node.GetAttribute("Duration"));

    const plStringView sKeyTime = node.GetAttribute("KeyTime");
    if (!sKeyTime.IsEmpty())
      fDuration = plMath::Max(fDuration, ParseKeyTime(sKeyTime));

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      fDuration = plMath::Max(fDuration, ComputeStoryboardDuration(*pChild));
    }

    return fDuration;
  }

  XamlFlowContract AnalyzeFlowContract(const plUiMarkupNode& xamlRoot)
  {
    XamlFlowContract result;
    CollectCommandBindings(xamlRoot, result.m_Commands);

    for (plUInt32 uiPass = 0; uiPass < 2; ++uiPass)
    {
      for (const plUiMarkupNode* pProperty : xamlRoot.m_Children)
      {
        if (!pProperty->GetLocalTag().EndsWith("Interaction.Triggers"))
          continue;

        for (const plUiMarkupNode* pTrigger : pProperty->m_Children)
        {
          if (pTrigger->GetLocalTag() != "DataTrigger")
            continue;

          const bool bNotEqual = pTrigger->GetAttribute("Comparison").IsEqual_NoCase("NotEqual");
          if ((uiPass == 0 && bNotEqual) || (uiPass == 1 && !bNotEqual))
            continue;

          const MarkupExtension binding = ParseMarkupExtension(pTrigger->GetAttribute("Binding"));
          const plString sClip = FindStoryboardAction(*pTrigger);
          const plString sValue = NormalizeTriggerValue(pTrigger->GetAttribute("Value"));
          if (binding.m_Kind != MarkupExtension::Kind::Binding || binding.m_sArg.IsEmpty() || sValue.IsEmpty() || sClip.IsEmpty())
            continue;

          if (!bNotEqual)
          {
            if (result.m_sEntry.IsEmpty())
            {
              result.m_sEntry = binding.m_sArg;
              result.m_sValue = sValue;
              result.m_sEnterClip = sClip;
            }
            else if (result.m_sEntry != binding.m_sArg || result.m_sValue != sValue || result.m_sEnterClip != sClip)
            {
              result.m_bConflict = true;
            }
          }
          else if (result.m_sEntry == binding.m_sArg && result.m_sValue == sValue)
          {
            if (!result.m_sExitClip.IsEmpty() && result.m_sExitClip != sClip)
              result.m_bConflict = true;
            else
              result.m_sExitClip = sClip;
          }
        }
      }
    }

    if (!result.m_sExitClip.IsEmpty())
    {
      if (const plUiMarkupNode* pStoryboard = FindStoryboardResource(xamlRoot, result.m_sExitClip))
      {
        result.m_fExitDuration = ComputeStoryboardDuration(*pStoryboard);
      }
    }

    return result;
  }

  void CollectImportNodes(plUiImportNode& node, plDynamicArray<plUiImportNode*>& out_nodes)
  {
    for (plUiImportNode* pChild : node.m_Children)
    {
      out_nodes.PushBack(pChild);
      CollectImportNodes(*pChild, out_nodes);
    }
  }

  void ApplyInteractionTriggers(const plUiMarkupNode& xamlRoot, plUiImportDocument& importDoc, XamlContext& ctx, plStringView sPath)
  {
    if (importDoc.m_Root.m_Children.IsEmpty())
      return;

    plDynamicArray<plUiImportNode*> stateHostCandidates;
    CollectImportNodes(importDoc.m_Root, stateHostCandidates);

    // States on one element are mutually exclusive (first match wins). Triggers over
    // the same entry can share that state machine, while independent entries need
    // separate hosts so their clips can run concurrently.
    plMap<plString, plUiImportNode*> stateHosts;

    for (const plUiMarkupNode* pProperty : xamlRoot.m_Children)
    {
      if (!pProperty->GetLocalTag().EndsWith("Interaction.Triggers"))
        continue;

      // Equality triggers are handled first so a complementary NotEqual trigger can
      // always attach its storyboard as ExitClip, regardless of source ordering.
      for (plUInt32 uiPass = 0; uiPass < 2; ++uiPass)
      {
        for (const plUiMarkupNode* pTrigger : pProperty->m_Children)
        {
          if (pTrigger->GetLocalTag() != "DataTrigger")
            continue;

          const bool bNotEqual = pTrigger->GetAttribute("Comparison").IsEqual_NoCase("NotEqual");
          if ((uiPass == 0 && bNotEqual) || (uiPass == 1 && !bNotEqual))
            continue;

          const MarkupExtension binding = ParseMarkupExtension(pTrigger->GetAttribute("Binding"));
          const plString sClip = FindStoryboardAction(*pTrigger);
          if (binding.m_Kind != MarkupExtension::Kind::Binding || binding.m_sArg.IsEmpty() || sClip.IsEmpty() || !HasAnimation(importDoc, sClip))
            continue;

          plUiImportNode* pStateHost = nullptr;
          if (plUiImportNode** ppExistingHost = stateHosts.GetValue(binding.m_sArg))
          {
            pStateHost = *ppExistingHost;
          }
          else
          {
            const plUInt32 uiHostIndex = plMath::Min(stateHosts.GetCount(), stateHostCandidates.GetCount() - 1);
            pStateHost = stateHostCandidates[uiHostIndex];
            stateHosts[binding.m_sArg] = pStateHost;
          }

          const plString sValue = NormalizeTriggerValue(pTrigger->GetAttribute("Value"));

          // FadeIn + complementary FadeOut is the common Noesis menu pattern. Keep them
          // as one state transition so the enter and exit clips never fight over channels.
          plUiImportState* pComplementaryState = nullptr;
          if (bNotEqual)
          {
            double fTriggerNumeric = 0.0;
            const bool bNumericTrigger = plConversionUtils::StringToFloat(sValue, fTriggerNumeric).Succeeded();

            for (plUiImportState& state : pStateHost->m_States)
            {
              const bool bSameOperand =
                (bNumericTrigger && state.m_sValueText.IsEmpty() && plMath::IsEqual(state.m_fValue, fTriggerNumeric, 0.000001)) ||
                (!bNumericTrigger && state.m_sValueText == sValue);

              if (state.m_sEntry == binding.m_sArg && bSameOperand && state.m_iCondition == plUiStateCondition::Equal)
              {
                pComplementaryState = &state;
                break;
              }
            }
          }
          if (pComplementaryState != nullptr)
          {
            pComplementaryState->m_sExitClip = sClip;

            plStringBuilder sMsg;
            sMsg.SetFormat("Complementary animation '{}' imported as the exit clip for state '{}'.", sClip, pComplementaryState->m_sName);
            ctx.m_pReport->Info(sPath, sMsg);
            continue;
          }

          plUiImportState& state = pStateHost->m_States.ExpandAndGetRef();
          plStringBuilder sStateName;
          sStateName.SetFormat("Xaml_{}", sClip);
          state.m_sName = sStateName;
          state.m_sEntry = binding.m_sArg;
          state.m_iCondition = bNotEqual ? plUiStateCondition::NotEqual : plUiStateCondition::Equal;
          state.m_sPlayClip = sClip;

          double fNumericValue = 0.0;
          if (plConversionUtils::StringToFloat(sValue, fNumericValue).Succeeded())
            state.m_fValue = fNumericValue;
          else
            state.m_sValueText = sValue;

          plStringBuilder sMsg;
          sMsg.SetFormat("Data trigger imported as a state that drives animation clip '{}' while {} {} '{}'.",
            sClip, binding.m_sArg, bNotEqual ? "!=" : "==", sValue);
          ctx.m_pReport->Info(sPath, sMsg);
        }
      }
    }
  }

  void ApplySubDocumentNavigation(plUiImportDocument& importDoc, plArrayPtr<XamlSubDocumentPlacement> placements, XamlContext& ctx, plStringView sPath)
  {
    plSet<plString> processedEntries;
    plSet<plString> discoveredEntries;

    for (const XamlSubDocumentPlacement& placement : placements)
    {
      const XamlFlowContract* pFlow = ctx.m_SubDocFlows.GetValue(placement.m_sSourcePath);
      if (pFlow != nullptr && pFlow->IsValid())
        discoveredEntries.Insert(pFlow->m_sEntry);
    }

    if (discoveredEntries.GetCount() > 1)
    {
      ctx.m_pReport->Warning(sPath, "Sub-Document controls advertise different state bindings; each binding is inferred only within its own navigation group.");
    }

    for (const XamlSubDocumentPlacement& seedPlacement : placements)
    {
      const XamlFlowContract* pSeedFlow = ctx.m_SubDocFlows.GetValue(seedPlacement.m_sSourcePath);
      if (pSeedFlow == nullptr || !pSeedFlow->IsValid() || processedEntries.Contains(pSeedFlow->m_sEntry))
        continue;

      processedEntries.Insert(pSeedFlow->m_sEntry);

      plHybridArray<plUInt32, 8> group;
      plInt32 iDefaultPlacement = -1;
      bool bDefaultAmbiguous = false;

      for (plUInt32 i = 0; i < placements.GetCount(); ++i)
      {
        const XamlFlowContract* pFlow = ctx.m_SubDocFlows.GetValue(placements[i].m_sSourcePath);
        if (pFlow == nullptr || !pFlow->IsValid() || pFlow->m_sEntry != pSeedFlow->m_sEntry)
          continue;

        group.PushBack(i);
        if (placements[i].m_bInitiallyVisible)
        {
          if (iDefaultPlacement >= 0)
            bDefaultAmbiguous = true;
          iDefaultPlacement = static_cast<plInt32>(i);
        }
      }

      if (group.GetCount() < 2)
        continue;

      const bool bHasDefault = iDefaultPlacement >= 0 && !bDefaultAmbiguous;
      plString sDefaultValue;
      if (bHasDefault)
      {
        const XamlFlowContract* pDefaultFlow = ctx.m_SubDocFlows.GetValue(placements[iDefaultPlacement].m_sSourcePath);
        sDefaultValue = pDefaultFlow->m_sValue;
      }
      else
      {
        plStringBuilder sWarning;
        sWarning.SetFormat("Navigation group '{}' has no unique initially visible page; Back was not inferred.", pSeedFlow->m_sEntry);
        ctx.m_pReport->Warning(sPath, sWarning);
      }

      for (plUInt32 uiGroupIndex : group)
      {
        XamlSubDocumentPlacement& placement = placements[uiGroupIndex];
        const XamlFlowContract* pFlow = ctx.m_SubDocFlows.GetValue(placement.m_sSourcePath);

        plUiImportState& showState = placement.m_pNode->m_States.ExpandAndGetRef();
        plStringBuilder sShowName;
        sShowName.SetFormat("XamlFlow_Show_{}", pFlow->m_sValue);
        showState.m_sName = sShowName;
        showState.m_sEntry = pFlow->m_sEntry;
        showState.m_iCondition = plUiStateCondition::Equal;
        showState.m_sValueText = pFlow->m_sValue;
        showState.m_iVisibility = plUiStateVisibility::Show;

        plUiImportState& hideState = placement.m_pNode->m_States.ExpandAndGetRef();
        plStringBuilder sHideName;
        sHideName.SetFormat("XamlFlow_Hide_{}", pFlow->m_sValue);
        hideState.m_sName = sHideName;
        hideState.m_sEntry = pFlow->m_sEntry;
        hideState.m_iCondition = plUiStateCondition::NotEqual;
        hideState.m_sValueText = pFlow->m_sValue;
        hideState.m_iVisibility = plUiStateVisibility::Hide;
        hideState.m_fHideDelay = pFlow->m_fExitDuration;

        plStringBuilder sVisibilityMessage;
        sVisibilityMessage.SetFormat("Generated parent flow: placement '{}' shows while {} = '{}', hides otherwise, and delays hiding by {} seconds.",
          placement.m_pNode->m_sName, pFlow->m_sEntry, pFlow->m_sValue, plArgF(pFlow->m_fExitDuration, 2));
        ctx.m_pReport->Info(sPath, sVisibilityMessage);

        for (const plString& sCommand : pFlow->m_Commands)
        {
          plString sTargetValue;

          for (plUInt32 uiTargetIndex : group)
          {
            const XamlFlowContract* pTargetFlow = ctx.m_SubDocFlows.GetValue(placements[uiTargetIndex].m_sSourcePath);
            if (sCommand.IsEqual_NoCase(pTargetFlow->m_sValue))
            {
              sTargetValue = pTargetFlow->m_sValue;
              break;
            }
          }

          if (sTargetValue.IsEmpty() && bHasDefault && sCommand.IsEqual_NoCase("Back"))
          {
            sTargetValue = sDefaultValue;
          }

          if (sTargetValue.IsEmpty())
            continue;

          plUiImportEventReaction& reaction = placement.m_pNode->m_Reactions.ExpandAndGetRef();
          reaction.m_sEvent = sCommand;

          plUiImportEventAction& action = reaction.m_Actions.ExpandAndGetRef();
          action.m_iType = plUiEventActionType::SetState;
          action.m_sEntry = pFlow->m_sEntry;
          action.m_Value = sTargetValue;

          plStringBuilder sMessage;
          sMessage.SetFormat("Generated parent flow: '{}' sets {} = '{}'.", sCommand, pFlow->m_sEntry, sTargetValue);
          ctx.m_pReport->Info(sPath, sMessage);
        }
      }
    }
  }

  plStatus BuildXamlImportDocument(const plUiMarkupNode& xamlRoot, plStringView sTargetPathAbs, plStringView sMainName, XamlContext& ctx)
  {
    plUiImportDocument importDoc;
    importDoc.m_sTargetPathAbs = sTargetPathAbs;
    importDoc.m_Root.m_sName = sMainName;

    plUiImportDocument* pOuterDocument = ctx.m_pCurrentDocument;
    ctx.m_pCurrentDocument = &importDoc;
    PL_SCOPE_EXIT(ctx.m_pCurrentDocument = pOuterDocument);

    plHybridArray<XamlSubDocumentPlacement, 16> placements;
    plHybridArray<XamlSubDocumentPlacement, 16>* pOuterPlacements = ctx.m_pCurrentPlacements;
    ctx.m_pCurrentPlacements = &placements;
    PL_SCOPE_EXIT(ctx.m_pCurrentPlacements = pOuterPlacements);

    const plStringView sRootTag = xamlRoot.GetLocalTag();

    if (sRootTag == "ResourceDictionary")
    {
      CollectResources(xamlRoot, ctx, sMainName);
      return plStatus("The file is a ResourceDictionary; there is no UI tree to import.");
    }

    if (sRootTag == "Application")
    {
      for (const plUiMarkupNode* pChild : xamlRoot.m_Children)
      {
        if (pChild->GetLocalTag().EndsWith(".Resources"))
          CollectResources(*pChild, ctx, sMainName);
      }
      return plStatus("App.xaml defines application resources, not a UI tree - import the Window/UserControl instead (App.xaml resources load automatically alongside it).");
    }

    // reference size from DesignWidth/DesignHeight or Width/Height
    float fW = ParseFloatAttr(xamlRoot.GetAttribute("d:DesignWidth", xamlRoot.GetAttribute("Width")), 1920.0f);
    float fH = ParseFloatAttr(xamlRoot.GetAttribute("d:DesignHeight", xamlRoot.GetAttribute("Height")), 1080.0f);
    importDoc.m_vReferenceSize = plVec2(fW, fH);

    // the root's FontFamily is the document default (WPF inherits fonts down the tree);
    // scoped per document - sub-imports restore the outer default afterwards
    const plString sOuterDefaultFont = ctx.m_sDefaultFontFile;
    const plStringView sRootFont = xamlRoot.GetAttribute("FontFamily");
    if (!sRootFont.IsEmpty())
      ctx.m_sDefaultFontFile = ResolveFontFile(sRootFont, ctx, sMainName);
    PL_SCOPE_EXIT(ctx.m_sDefaultFontFile = sOuterDefaultFont);

    if (sRootTag == "UserControl" || sRootTag == "Page" || sRootTag == "Window")
    {
      if (const plUiMarkupNode* pResources = FindPropertyElement(xamlRoot, "Resources"))
        CollectResources(*pResources, ctx, sMainName);

      for (const plUiMarkupNode* pChild : xamlRoot.m_Children)
      {
        if (!IsPropertyElement(*pChild))
          BuildElement(*pChild, ctx, importDoc.m_Root, PlacementMode::Free, sMainName);
      }
    }
    else
    {
      BuildElement(xamlRoot, ctx, importDoc.m_Root, PlacementMode::Free, sMainName);
    }

    ApplyInteractionTriggers(xamlRoot, importDoc, ctx, sMainName);
    ApplySubDocumentNavigation(importDoc, placements, ctx, sMainName);

    return ctx.m_pWriter->WriteDocument(importDoc, *ctx.m_pOutDocs);
  }

  plString ImportXamlFileAsDocument(plStringView sXamlPathAbs, XamlContext& ctx)
  {
    plStringBuilder sCleanPath = sXamlPathAbs;
    sCleanPath.MakeCleanPath();
    const plString sPathKey = sCleanPath;

    if (const plString* pGuid = ctx.m_SubDocs.GetValue(sPathKey))
      return *pGuid;

    if (ctx.m_iDepth >= 8)
    {
      ctx.m_pReport->Warning(sCleanPath, "UserControl nesting deeper than 8 levels; reference skipped.");
      return {};
    }

    plStringBuilder sContent;
    {
      plFileReader file;
      if (file.Open(sCleanPath).Failed())
      {
        ctx.m_SubDocs[sPathKey] = plString();
        return {};
      }
      sContent.ReadAll(file);
    }

    const plStringBuilder sName = plPathUtils::GetFileName(sCleanPath);

    plUniquePtr<plUiMarkupNode> pParsed = plUiMarkupParser::Parse(sContent.GetView(), false, *ctx.m_pReport, sName);
    const plUiMarkupNode* pRoot = FindDocumentRoot(*pParsed);
    if (pRoot == nullptr)
    {
      ctx.m_SubDocs[sPathKey] = plString();
      return {};
    }

    XamlFlowContract flow = AnalyzeFlowContract(*pRoot);
    if (flow.m_bConflict)
    {
      ctx.m_pReport->Warning(sName, "Conflicting DataTrigger binding/value contracts were found; parent navigation was not inferred for this UserControl.");
    }
    ctx.m_SubDocFlows[sPathKey] = std::move(flow);

    const float fReferenceWidth = ParseFloatAttr(pRoot->GetAttribute("Width", pRoot->GetAttribute("d:DesignWidth")), 200.0f);
    const float fReferenceHeight = ParseFloatAttr(pRoot->GetAttribute("Height", pRoot->GetAttribute("d:DesignHeight")), 50.0f);
    ctx.m_SubDocReferenceSizes[sPathKey] = plVec2(plMath::Max(fReferenceWidth, 1.0f), plMath::Max(fReferenceHeight, 1.0f));

    plStringBuilder sTargetPath = sCleanPath;
    sTargetPath.ChangeFileExtension("plUiDocumentAsset");

    // Sub-documents are reusable assets. A previous top-level or parent import may
    // already have created one; preserve it (and any subsequent manual edits) rather
    // than failing the reference and degrading the custom control into a plain Panel.
    if (plOSFile::ExistsFile(sTargetPath))
    {
      plAssetCurator::plLockedSubAsset existing = plAssetCurator::GetSingleton()->FindSubAsset(sTargetPath);
      if (existing.isValid())
      {
        plStringBuilder sGuid;
        plConversionUtils::ToString(existing->m_Data.m_Guid, sGuid);
        ctx.m_SubDocs[sPathKey] = plString(sGuid.GetView());

        plStringBuilder sMsg;
        sMsg.SetFormat("Reused existing UI document for UserControl '{}'.", sName);
        ctx.m_pReport->Info(sCleanPath, sMsg);
        return ctx.m_SubDocs[sPathKey];
      }

      ctx.m_pReport->Warning(sCleanPath, "The UserControl asset exists but is not indexed by the Asset Curator yet; reference skipped.");
      ctx.m_SubDocs[sPathKey] = plString();
      return {};
    }

    ctx.m_iDepth++;
    const plUInt32 uiDocIndexBefore = ctx.m_pOutDocs->GetCount();
    const plStatus status = BuildXamlImportDocument(*pRoot, sTargetPath, sName, ctx);
    ctx.m_iDepth--;

    plString sGuid;
    if (status.Succeeded() && ctx.m_pOutDocs->GetCount() > uiDocIndexBefore)
    {
      plStringBuilder sTmp;
      plConversionUtils::ToString((*ctx.m_pOutDocs)[uiDocIndexBefore]->GetGuid(), sTmp);
      sGuid = sTmp;

      plStringBuilder sMsg;
      sMsg.SetFormat("UserControl '{}' imported as its own document.", sName);
      ctx.m_pReport->Info(sCleanPath, sMsg);
    }
    else if (status.Failed())
    {
      plStringBuilder sMsg;
      sMsg.SetFormat("UserControl import failed: {}", status.m_sMessage);
      ctx.m_pReport->Warning(sCleanPath, sMsg);
    }

    ctx.m_SubDocs[sPathKey] = sGuid;
    return sGuid;
  }
} // namespace

plStatus plUiXamlDocumentGenerator::Generate(plStringView sInputFileAbs, plStringView sMode, plDynamicArray<plDocument*>& out_generatedDocuments)
{
  PL_IGNORE_UNUSED(sMode);

  plStringBuilder sContent;
  {
    plFileReader file;
    if (file.Open(sInputFileAbs).Failed())
      return plStatus(plFmt("Could not open '{}'.", sInputFileAbs));
    sContent.ReadAll(file);
  }

  plUiImportReport report;
  plUiImportWriter writer(report);

  const plStringBuilder sSourceName = plPathUtils::GetFileNameAndExtension(sInputFileAbs);
  const plStringBuilder sMainName = plPathUtils::GetFileName(sInputFileAbs);

  plUniquePtr<plUiMarkupNode> pParsed = plUiMarkupParser::Parse(sContent.GetView(), false, report, sSourceName);

  const plUiMarkupNode* pRoot = FindDocumentRoot(*pParsed);
  if (pRoot == nullptr)
    return plStatus("The XAML file contains no root element.");

  XamlContext ctx;
  ctx.m_pReport = &report;
  ctx.m_pWriter = &writer;
  ctx.m_pOutDocs = &out_generatedDocuments;
  ctx.m_sMainName = sMainName;
  {
    plStringBuilder sFolder = sInputFileAbs;
    sFolder.PathParentDirectory();
    ctx.m_sFolderAbs = sFolder;
  }

  plStringBuilder sTargetPath = sInputFileAbs;
  sTargetPath.ChangeFileExtension("plUiDocumentAsset");

  // guard against circular UserControl references back to the file being imported
  {
    plStringBuilder sMainClean = sInputFileAbs;
    sMainClean.MakeCleanPath();
    ctx.m_SubDocs[plString(sMainClean.GetView())] = plString();
  }

  // App.xaml next to the file usually merges the project's resource dictionaries
  PreloadApplicationResources(ctx);

  const plStatus status = BuildXamlImportDocument(*pRoot, sTargetPath, sMainName, ctx);

  plStringBuilder sTitle;
  sTitle.SetFormat("XAML Import — {}", sSourceName);
  plUiImportShowReport(report, sTitle);

  return status;
}
