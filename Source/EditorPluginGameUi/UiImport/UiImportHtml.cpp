#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiImport/UiImportHtml.h>
#include <EditorPluginGameUi/UiImport/UiImportWriter.h>
#include <EditorPluginGameUi/UiImport/UiMarkupParser.h>

#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/IO/OSFile.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <ToolsFoundation/Network/HttpClient.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiHtmlDocumentGenerator, 1, plRTTIDefaultAllocator<plUiHtmlDocumentGenerator>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plUiHtmlDocumentGenerator::plUiHtmlDocumentGenerator()
{
  AddSupportedFileType("html");
  AddSupportedFileType("htm");
}

plUiHtmlDocumentGenerator::~plUiHtmlDocumentGenerator() = default;

void plUiHtmlDocumentGenerator::GetImportModes(plStringView sAbsInputFile, plDynamicArray<plAssetDocumentGenerator::ImportMode>& out_modes) const
{
  plAssetDocumentGenerator::ImportMode& info = out_modes.ExpandAndGetRef();
  info.m_Priority = plAssetDocGeneratorPriority::DefaultPriority;
  info.m_sName = "HtmlToUiDocument";
  info.m_sIcon = ":/AssetIcons/GameUi.svg";
}

// ═══════════════════════════════════════════════════════════════════════════════════════
// CSS subset
// ═══════════════════════════════════════════════════════════════════════════════════════

namespace
{
  using ComputedStyle = plMap<plString, plString>;

  struct RemoteFontFace
  {
    plString m_sFamily;
    plInt32 m_iWeight = 400;
    bool m_bItalic = false;
    plString m_sUrl;
    plString m_sLocalFile;
  };

  /// A GET of an http(s) URL; redirects are followed, except from https to http. Empty with out_sError on failure.
  plDynamicArray<plUInt8> FetchHttpBytes(plStringView sUrl, plStringBuilder& out_sError, plStringView sAccept = "text/css,*/*;q=0.8")
  {
    plDynamicArray<plUInt8> bytes;

    if (!plHttpClient::IsRemoteUrl(sUrl))
    {
      out_sError = "unsupported or invalid URL";
      return bytes;
    }

    plHttpClient::Request request;
    request.m_sUrl = sUrl;
    request.m_Timeout = plTime::MakeFromSeconds(15);
    request.m_Headers.PushBack("User-Agent: Mozilla/5.0 PlasmaEngine-UiImporter/1.0");
    request.m_Headers.PushBack(plStringBuilder("Accept: ", sAccept));

    plUInt32 uiStatus = 0;
    if (plHttpClient::Send(request, uiStatus, bytes, out_sError).Failed())
    {
      bytes.Clear();
      return bytes;
    }

    if (uiStatus >= 400)
    {
      out_sError.SetFormat("HTTP status {}", uiStatus);
      bytes.Clear();
      return bytes;
    }

    if (bytes.GetCount() > 16 * 1024 * 1024)
    {
      out_sError = "response exceeds the 16 MiB importer limit";
      bytes.Clear();
    }

    return bytes;
  }

  /// The scheme and host of an absolute URL ("https://fonts.example.com"), or empty.
  plStringView GetUrlOrigin(plStringView sUrl)
  {
    const char* szScheme = sUrl.FindSubString("://");
    if (szScheme == nullptr)
      return {};

    plStringView sAfterScheme(szScheme + 3, sUrl.GetEndPointer());
    const char* szPath = sAfterScheme.FindSubString("/");
    return plStringView(sUrl.GetStartPointer(), szPath != nullptr ? szPath : sUrl.GetEndPointer());
  }

  /// Removes "." and ".." segments from the path of an absolute URL.
  plString RemoveDotSegments(plStringView sUrl)
  {
    const plStringView sOrigin = GetUrlOrigin(sUrl);
    const plStringView sPath(sOrigin.GetEndPointer(), sUrl.GetEndPointer());

    plHybridArray<plStringView, 16> segments;
    sPath.Split(true, segments, "/");

    plHybridArray<plStringView, 16> kept;
    for (plUInt32 i = 0; i < segments.GetCount(); ++i)
    {
      const plStringView sSegment = segments[i];
      if (sSegment == "..")
      {
        if (kept.GetCount() > 1)
          kept.PopBack();
      }
      else if (sSegment != "." && (i == 0 || !sSegment.IsEmpty() || i + 1 == segments.GetCount()))
      {
        kept.PushBack(sSegment);
      }
    }

    plStringBuilder sResult(sOrigin);
    for (plUInt32 i = 0; i < kept.GetCount(); ++i)
    {
      if (i > 0)
        sResult.Append("/");
      sResult.Append(kept[i]);
    }

    return sResult;
  }

  /// A URL a stylesheet refers to, resolved against the stylesheet's own URL: absolute, scheme-relative, host-relative
  /// or relative to the stylesheet's folder.
  plString ResolveUrl(plStringView sBaseUrl, plStringView sReference)
  {
    if (plHttpClient::IsRemoteUrl(sReference) || sReference.StartsWith("data:"))
      return sReference;

    if (sReference.StartsWith("//"))
    {
      const char* szScheme = sBaseUrl.FindSubString("//");
      return plStringBuilder(plStringView(sBaseUrl.GetStartPointer(), szScheme != nullptr ? szScheme : sBaseUrl.GetStartPointer()), sReference);
    }

    if (sReference.StartsWith("/"))
      return RemoveDotSegments(plStringBuilder(GetUrlOrigin(sBaseUrl), sReference));

    // the base without its query, fragment and file name
    plStringView sBase = sBaseUrl;
    if (const char* szQuery = sBase.FindSubString("?"))
      sBase = plStringView(sBase.GetStartPointer(), szQuery);
    if (const char* szFragment = sBase.FindSubString("#"))
      sBase = plStringView(sBase.GetStartPointer(), szFragment);

    const plStringView sOrigin = GetUrlOrigin(sBase);
    const char* szLastSlash = sBase.FindLastSubString("/");

    plStringBuilder sFolder;
    if (szLastSlash != nullptr && szLastSlash >= sOrigin.GetEndPointer())
      sFolder = plStringView(sBase.GetStartPointer(), szLastSlash + 1);
    else
      sFolder.Set(sOrigin, "/");

    sFolder.Append(sReference);
    return RemoveDotSegments(sFolder);
  }

  /// The lower-case extension of the path of a URL (without query and fragment), or empty.
  plString GetUrlExtension(plStringView sUrl)
  {
    plStringView sPath = sUrl;
    if (const char* szQuery = sPath.FindSubString("?"))
      sPath = plStringView(sPath.GetStartPointer(), szQuery);
    if (const char* szFragment = sPath.FindSubString("#"))
      sPath = plStringView(sPath.GetStartPointer(), szFragment);

    const char* szDot = sPath.FindLastSubString(".");
    const char* szSlash = sPath.FindLastSubString("/");
    if (szDot == nullptr || (szSlash != nullptr && szDot < szSlash))
      return {};

    plStringBuilder sExtension(plStringView(szDot + 1, sPath.GetEndPointer()));
    sExtension.ToLower();
    return sExtension;
  }

  plString NormalizeFontFamily(plStringView sFamily)
  {
    plStringBuilder s = sFamily;
    s.Trim(" \t\r\n'\"");
    s.ToLower();
    return plString(s);
  }

  plString MakeFontFaceKey(plStringView sFamily, plInt32 iWeight, bool bItalic)
  {
    plStringBuilder sKey;
    sKey.SetFormat("{}|{}|{}", NormalizeFontFamily(sFamily), iWeight, bItalic ? 1 : 0);
    return plString(sKey);
  }

  plString ParseFirstFontFamily(plStringView sValue)
  {
    plStringBuilder sFamily = sValue;
    sFamily.Trim(" \t\r\n");
    if (sFamily.IsEmpty())
      return {};

    const char cQuote = (sFamily.StartsWith("'") || sFamily.StartsWith("\"")) ? sFamily.GetData()[0] : '\0';
    const char* szEnd = nullptr;
    if (cQuote != '\0')
    {
      for (const char* sz = sFamily.GetData() + 1; *sz != '\0'; ++sz)
      {
        if (*sz == cQuote)
        {
          szEnd = sz + 1;
          break;
        }
      }
    }
    else
    {
      szEnd = sFamily.FindSubString(",");
      if (szEnd == nullptr)
        szEnd = sFamily.GetView().GetEndPointer();
    }

    if (szEnd == nullptr)
      szEnd = sFamily.GetView().GetEndPointer();

    plStringBuilder sFirst;
    sFirst.SetSubString_FromTo(sFamily.GetData(), szEnd);
    sFirst.Trim(" \t\r\n'\"");
    return plString(sFirst);
  }

  struct CssDeclaration
  {
    plString m_sName;
    plString m_sValue;
  };

  struct CssSelectorPart
  {
    plString m_sTag; // empty / "*" = any
    plString m_sId;
    plHybridArray<plString, 2> m_Classes;
  };

  struct CssRule
  {
    /// Descendant chain from outermost to innermost (".hints .player b").
    plHybridArray<CssSelectorPart, 3> m_Selector;
    plString m_sPseudo;
    plInt32 m_iSpecificity = 0;
    plInt32 m_iOrder = 0;
    plHybridArray<CssDeclaration, 8> m_Declarations;
  };

  struct CssSheet
  {
    plDynamicArray<CssRule> m_Rules;
    plInt32 m_iNextOrder = 0;
    plUiImportReport* m_pReport = nullptr;

    static bool HasClass(const plUiMarkupNode& node, plStringView sClass)
    {
      plHybridArray<plStringView, 8> classes;
      plStringBuilder sClasses = node.GetAttribute("class");
      sClasses.Split(false, classes, " ", "\t");

      for (plStringView sC : classes)
      {
        if (sC == sClass)
          return true;
      }
      return false;
    }

    static bool ParseSelectorPart(plStringView sToken, CssSelectorPart& out_part, plInt32& inout_iSpecificity)
    {
      plStringBuilder sPart = sToken;
      sPart.Trim(" \t\r\n");
      if (sPart.IsEmpty())
        return false;

      const char* szCur = sPart.GetData();
      const char* szEnd = sPart.GetView().GetEndPointer();

      if (*szCur != '.' && *szCur != '#' && *szCur != '*')
      {
        const char* szTagEnd = szCur;
        while (szTagEnd < szEnd && *szTagEnd != '.' && *szTagEnd != '#')
          ++szTagEnd;
        out_part.m_sTag = plStringView(szCur, szTagEnd);
        plStringBuilder sLower = out_part.m_sTag;
        sLower.ToLower();
        out_part.m_sTag = sLower;
        ++inout_iSpecificity;
        szCur = szTagEnd;
      }
      else if (*szCur == '*')
      {
        ++szCur;
      }

      while (szCur < szEnd)
      {
        const char cKind = *szCur++;
        if (cKind != '.' && cKind != '#')
          return false;

        const char* szValue = szCur;
        while (szCur < szEnd && *szCur != '.' && *szCur != '#')
          ++szCur;
        if (szValue == szCur)
          return false;

        if (cKind == '#')
        {
          if (!out_part.m_sId.IsEmpty())
            return false;
          out_part.m_sId = plStringView(szValue, szCur);
          inout_iSpecificity += 100;
        }
        else
        {
          out_part.m_Classes.PushBack(plString(plStringView(szValue, szCur)));
          inout_iSpecificity += 10;
        }
      }

      return true;
    }

    static bool MatchesPart(const plUiMarkupNode& node, const CssSelectorPart& part)
    {
      if (!part.m_sTag.IsEmpty() && part.m_sTag != node.m_sTag)
        return false;
      if (!part.m_sId.IsEmpty() && part.m_sId != node.GetAttribute("id"))
        return false;
      for (const plString& sClass : part.m_Classes)
      {
        if (!HasClass(node, sClass))
          return false;
      }
      return true;
    }

    static bool MatchesRule(const plUiMarkupNode& node, const CssRule& rule)
    {
      if (rule.m_Selector.IsEmpty())
        return false;

      const plUiMarkupNode* pCandidate = &node;
      const plInt32 iLast = static_cast<plInt32>(rule.m_Selector.GetCount()) - 1;
      if (!MatchesPart(*pCandidate, rule.m_Selector[iLast]))
        return false;

      // Each preceding part may match any ancestor, preserving descendant order.
      for (plInt32 i = iLast - 1; i >= 0; --i)
      {
        pCandidate = pCandidate->m_pParent;
        while (pCandidate != nullptr && !MatchesPart(*pCandidate, rule.m_Selector[i]))
          pCandidate = pCandidate->m_pParent;
        if (pCandidate == nullptr)
          return false;
      }

      return true;
    }

    void ParseDeclarations(plStringView sBlock, plHybridArray<CssDeclaration, 8>& out_decls)
    {
      plHybridArray<plStringView, 16> parts;
      plStringBuilder sBlockCopy = sBlock;
      sBlockCopy.Split(false, parts, ";");

      for (plStringView sPart : parts)
      {
        const char* szColon = sPart.FindSubString(":");
        if (szColon == nullptr)
          continue;

        plStringBuilder sName;
        sName.SetSubString_FromTo(sPart.GetStartPointer(), szColon);
        sName.Trim(" \t\r\n");
        sName.ToLower();

        plStringBuilder sValue;
        sValue.SetSubString_FromTo(szColon + 1, sPart.GetEndPointer());
        sValue.Trim(" \t\r\n");
        if (sValue.EndsWith_NoCase("!important"))
        {
          sValue.Shrink(0, 10);
          sValue.Trim(" \t");
        }

        if (sName.IsEmpty() || sValue.IsEmpty())
          continue;

        CssDeclaration& decl = out_decls.ExpandAndGetRef();
        decl.m_sName = sName;
        decl.m_sValue = sValue;
      }
    }

    void AddRulesFromText(plStringView sCss, plStringView sSourceName)
    {
      const char* szCur = sCss.GetStartPointer();
      const char* szEnd = sCss.GetEndPointer();

      plStringBuilder sSelector;

      while (szCur < szEnd)
      {
        // CSS commonly indents comments. Consume whitespace before checking for one so
        // a comment can never become part of the following selector.
        while (szCur < szEnd && (*szCur == ' ' || *szCur == '\t' || *szCur == '\r' || *szCur == '\n'))
          ++szCur;
        if (szCur >= szEnd)
          break;

        if (szCur + 2 <= szEnd && szCur[0] == '/' && szCur[1] == '*')
        {
          szCur += 2;
          while (szCur + 2 <= szEnd && !(szCur[0] == '*' && szCur[1] == '/'))
            ++szCur;
          szCur = plMath::Min(szCur + 2, szEnd);
          continue;
        }

        if (*szCur == '@')
        {
          // @media/@keyframes/@font-face/... - skip the whole construct
          const char* szAt = szCur;
          while (szCur < szEnd && *szCur != '{' && *szCur != ';')
            ++szCur;

          if (szCur < szEnd && *szCur == '{')
          {
            plInt32 iDepth = 1;
            ++szCur;
            while (szCur < szEnd && iDepth > 0)
            {
              if (*szCur == '{')
                ++iDepth;
              else if (*szCur == '}')
                --iDepth;
              ++szCur;
            }
          }
          else if (szCur < szEnd)
          {
            ++szCur; // ';'
          }

          plStringBuilder sMsg, sAt;
          sAt.SetSubString_ElementCount(szAt, plMath::Min<plUInt32>(24, static_cast<plUInt32>(szEnd - szAt)));
          if (!sAt.StartsWith_NoCase("@font-face"))
          {
            sMsg.SetFormat("Unsupported CSS at-rule skipped: '{}...'", sAt);
            m_pReport->Info(sSourceName, sMsg);
          }
          continue;
        }

        const char* szBrace = szCur;
        while (szBrace < szEnd && *szBrace != '{')
          ++szBrace;
        if (szBrace >= szEnd)
          break;

        sSelector.SetSubString_FromTo(szCur, szBrace);

        const char* szClose = szBrace + 1;
        plInt32 iDepth = 1;
        while (szClose < szEnd && iDepth > 0)
        {
          if (*szClose == '{')
            ++iDepth;
          else if (*szClose == '}')
            --iDepth;
          ++szClose;
        }

        plStringView sBlock(szBrace + 1, szClose > szBrace + 1 ? szClose - 1 : szBrace + 1);
        szCur = szClose;

        plHybridArray<CssDeclaration, 8> decls;
        ParseDeclarations(sBlock, decls);
        if (decls.IsEmpty())
          continue;

        plHybridArray<plStringView, 4> selectors;
        sSelector.Split(false, selectors, ",");

        for (plStringView sSel : selectors)
        {
          plStringBuilder sOne = sSel;
          sOne.Trim(" \t\r\n");

          if (sOne.IsEmpty())
            continue;

          if (sOne.FindSubString(">") != nullptr || sOne.FindSubString("+") != nullptr ||
              sOne.FindSubString("~") != nullptr || sOne.FindSubString("[") != nullptr ||
              sOne.FindSubString("::") != nullptr)
          {
            plStringBuilder sMsg;
            sMsg.SetFormat("Unsupported CSS selector skipped: '{}' (descendant tag/.class/#id selectors are supported).", sOne);
            m_pReport->Info(sSourceName, sMsg);
            continue;
          }

          plStringBuilder sPseudo;
          const char* szColon = sOne.FindSubString(":");
          if (szColon != nullptr)
          {
            sPseudo.SetSubString_FromTo(szColon + 1, sOne.GetView().GetEndPointer());
            sPseudo.ToLower();
            if (sPseudo != "hover" && sPseudo != "active")
            {
              plStringBuilder sMsg;
              sMsg.SetFormat("CSS pseudo-class selector skipped: '{}'.", sOne);
              m_pReport->Info(sSourceName, sMsg);
              continue;
            }

            plStringBuilder sWithoutPseudo;
            sWithoutPseudo.SetSubString_FromTo(sOne.GetData(), szColon);
            sOne = sWithoutPseudo;
          }

          CssRule& rule = m_Rules.ExpandAndGetRef();
          rule.m_iOrder = m_iNextOrder++;
          rule.m_Declarations = decls;
          rule.m_sPseudo = sPseudo;

          plHybridArray<plStringView, 4> selectorParts;
          sOne.Split(false, selectorParts, " ", "\t", "\r", "\n");
          bool bValid = !selectorParts.IsEmpty();
          for (plStringView sPart : selectorParts)
          {
            CssSelectorPart& part = rule.m_Selector.ExpandAndGetRef();
            if (!ParseSelectorPart(sPart, part, rule.m_iSpecificity))
            {
              bValid = false;
              break;
            }
          }

          if (!bValid)
          {
            m_Rules.PopBack();
            plStringBuilder sMsg;
            sMsg.SetFormat("Unsupported CSS selector skipped: '{}'.", sSel);
            m_pReport->Info(sSourceName, sMsg);
          }
        }
      }
    }

    void Collect(const plUiMarkupNode& node, ComputedStyle& out_style, plStringView sPseudo = {}) const
    {
      plHybridArray<const CssRule*, 16> matches;

      for (const CssRule& rule : m_Rules)
      {
        if (rule.m_sPseudo != sPseudo)
          continue;
        if (!MatchesRule(node, rule))
          continue;

        matches.PushBack(&rule);
      }

      matches.Sort([](const CssRule* a, const CssRule* b)
    {
      if (a->m_iSpecificity != b->m_iSpecificity)
        return a->m_iSpecificity < b->m_iSpecificity;
      return a->m_iOrder < b->m_iOrder;
    });

      for (const CssRule* pRule : matches)
      {
        for (const CssDeclaration& decl : pRule->m_Declarations)
        {
          out_style[decl.m_sName] = decl.m_sValue;
        }
      }

      // inline style wins
      const plString* pInline = node.FindAttribute("style");
      if (sPseudo.IsEmpty() && pInline != nullptr)
      {
        plHybridArray<CssDeclaration, 8> decls;
        const_cast<CssSheet*>(this)->ParseDeclarations(*pInline, decls);
        for (const CssDeclaration& decl : decls)
        {
          out_style[decl.m_sName] = decl.m_sValue;
        }
      }

      // Expand the CSS positioning shorthand after the cascade so all layout code can
      // continue to consume top/right/bottom/left independently.
      if (sPseudo.IsEmpty())
      {
        const plString* pInset = out_style.GetValue("inset");
        if (pInset != nullptr)
        {
          plHybridArray<plStringView, 4> values;
          plStringBuilder sValues = *pInset;
          sValues.Split(false, values, " ", "\t");
          if (!values.IsEmpty())
          {
            plStringView top = values[0];
            plStringView right = values.GetCount() > 1 ? values[1] : top;
            plStringView bottom = values.GetCount() > 2 ? values[2] : top;
            plStringView left = values.GetCount() > 3 ? values[3] : right;
            if (!out_style.Contains("top"))
              out_style["top"] = top;
            if (!out_style.Contains("right"))
              out_style["right"] = right;
            if (!out_style.Contains("bottom"))
              out_style["bottom"] = bottom;
            if (!out_style.Contains("left"))
              out_style["left"] = left;
          }
        }
      }
    }
  };

  const plString* FindDeclaration(const plHybridArray<CssDeclaration, 8>& declarations, plStringView sName)
  {
    for (const CssDeclaration& declaration : declarations)
    {
      if (declaration.m_sName == sName)
        return &declaration.m_sValue;
    }
    return nullptr;
  }

  void CollectRemoteFontFaces(
    CssSheet& sheet, plStringView sCss, plStringView sBaseUrl, plMap<plString, RemoteFontFace>& inout_faces)
  {
    const char* szCur = sCss.GetStartPointer();
    const char* szEnd = sCss.GetEndPointer();

    while (szCur < szEnd)
    {
      const char* szAt = plStringView(szCur, szEnd).FindSubString_NoCase("@font-face");
      if (szAt == nullptr)
        break;

      const char* szOpen = szAt;
      while (szOpen < szEnd && *szOpen != '{')
        ++szOpen;
      if (szOpen >= szEnd)
        break;

      const char* szClose = szOpen + 1;
      plInt32 iDepth = 1;
      while (szClose < szEnd && iDepth > 0)
      {
        if (*szClose == '{')
          ++iDepth;
        else if (*szClose == '}')
          --iDepth;
        ++szClose;
      }

      plHybridArray<CssDeclaration, 8> declarations;
      sheet.ParseDeclarations(plStringView(szOpen + 1, szClose > szOpen + 1 ? szClose - 1 : szOpen + 1), declarations);

      const plString* pFamily = FindDeclaration(declarations, "font-family");
      const plString* pSource = FindDeclaration(declarations, "src");
      if (pFamily != nullptr && pSource != nullptr)
      {
        const char* szUrl = pSource->FindSubString_NoCase("url(");
        if (szUrl != nullptr)
        {
          szUrl += 4;
          const char* szUrlEnd = szUrl;
          char cQuote = '\0';
          while (szUrl < pSource->GetView().GetEndPointer() && (*szUrl == ' ' || *szUrl == '\t'))
            ++szUrl;
          if (szUrl < pSource->GetView().GetEndPointer() && (*szUrl == '\'' || *szUrl == '"'))
            cQuote = *szUrl++;

          szUrlEnd = szUrl;
          while (szUrlEnd < pSource->GetView().GetEndPointer())
          {
            if ((cQuote != '\0' && *szUrlEnd == cQuote) || (cQuote == '\0' && *szUrlEnd == ')'))
              break;
            ++szUrlEnd;
          }

          plStringBuilder sUrl(plStringView(szUrl, szUrlEnd));
          sUrl.Trim(" \t\r\n");

          RemoteFontFace face;
          face.m_sFamily = ParseFirstFontFamily(*pFamily);
          face.m_sUrl = ResolveUrl(sBaseUrl, sUrl);

          if (const plString* pWeight = FindDeclaration(declarations, "font-weight"))
          {
            if (pWeight->IsEqual_NoCase("bold"))
            {
              face.m_iWeight = 700;
            }
            else
            {
              plInt32 iWeight = 400;
              if (plConversionUtils::StringToInt(*pWeight, iWeight).Succeeded())
                face.m_iWeight = plMath::Clamp(iWeight, 100, 900);
            }
          }

          if (const plString* pStyle = FindDeclaration(declarations, "font-style"))
            face.m_bItalic = pStyle->IsEqual_NoCase("italic") || pStyle->IsEqual_NoCase("oblique");

          if (!face.m_sFamily.IsEmpty() && !face.m_sUrl.IsEmpty())
            inout_faces[MakeFontFaceKey(face.m_sFamily, face.m_iWeight, face.m_bItalic)] = face;
        }
      }

      szCur = szClose;
    }
  }

  plString SanitizeFontFilePart(plStringView sValue)
  {
    plStringBuilder s(sValue);
    for (const char* szChar : {":", ";", "/", "\\", "?", "*", "\"", "<", ">", "|", " "})
      s.ReplaceAll(szChar, "_");
    return s;
  }

  plUInt32 CacheRemoteFontFaces(plMap<plString, RemoteFontFace>& inout_faces, plStringView sFolderAbs, plUiImportReport& report, plStringView sSourceName)
  {
    if (inout_faces.IsEmpty())
      return 0;

    plStringBuilder sCacheFolder = sFolderAbs;
    sCacheFolder.AppendPath("ImportedFonts");
    if (plOSFile::CreateDirectoryStructure(sCacheFolder).Failed())
    {
      report.Warning(sSourceName, "Remote fonts were found, but the ImportedFonts cache folder could not be created.");
      return 0;
    }

    plUInt32 uiCached = 0;
    plSet<plString> failedFamilies;
    for (auto it = inout_faces.GetIterator(); it.IsValid(); ++it)
    {
      RemoteFontFace& face = it.Value();
      plString sExtension = GetUrlExtension(face.m_sUrl);
      if (sExtension.IsEmpty() || sExtension.GetCharacterCount() > 5)
        sExtension = "woff2";

      plStringBuilder sFileName;
      sFileName.SetFormat("{}-{}-{}.{}", SanitizeFontFilePart(face.m_sFamily), face.m_iWeight, face.m_bItalic ? "italic" : "normal", sExtension);

      plStringBuilder sLocalFile = sCacheFolder;
      sLocalFile.AppendPath(sFileName);

      plFileStats stats;
      if (plOSFile::GetFileStats(sLocalFile, stats).Succeeded() && stats.m_uiFileSize > 0)
      {
        face.m_sLocalFile = sLocalFile;
        ++uiCached;
        continue;
      }

      plStringBuilder sError;
      const plDynamicArray<plUInt8> bytes = FetchHttpBytes(face.m_sUrl, sError, "font/woff2,font/ttf,font/otf,application/octet-stream;q=0.8");
      if (bytes.IsEmpty())
      {
        failedFamilies.Insert(face.m_sFamily);
        continue;
      }

      plOSFile file;
      if (file.Open(sLocalFile, plFileOpenMode::Write).Failed() ||
          file.Write(bytes.GetData(), bytes.GetCount()).Failed())
      {
        failedFamilies.Insert(face.m_sFamily);
        continue;
      }
      file.Close();

      face.m_sLocalFile = sLocalFile;
      ++uiCached;
    }

    for (const plString& sFamily : failedFamilies)
    {
      plStringBuilder sMessage;
      sMessage.SetFormat("Remote font family '{}' could not be cached; the engine fallback font will be used.", sFamily);
      report.Warning(sSourceName, sMessage);
    }

    return uiCached;
  }

  struct BundledFallbackFonts
  {
    plString m_sRegular;
    plString m_sBold;
    plString m_sItalic;
    plString m_sBoldItalic;

    bool IsUsable() const
    {
      return !m_sRegular.IsEmpty() && !m_sBold.IsEmpty() && !m_sItalic.IsEmpty() && !m_sBoldItalic.IsEmpty();
    }

    plStringView Select(plInt32 iWeight, bool bItalic) const
    {
      const bool bBold = iWeight >= 600;
      if (bItalic)
        return bBold ? m_sBoldItalic.GetView() : m_sItalic.GetView();

      return bBold ? m_sBold.GetView() : m_sRegular.GetView();
    }
  };

  BundledFallbackFonts CacheBundledFallbackFonts(plStringView sFolderAbs, plUiImportReport& report, plStringView sSourceName)
  {
    BundledFallbackFonts result;

    // The stylesheet library these fonts come from ships in plasma.rmlui rather than in the engine,
    // so where it sits depends on where that package is installed. Resolving a known file through
    // the virtual file system finds it under whichever data directory provides it, and fails
    // cleanly when the package is not installed at all.
    plStringBuilder sSourceFolder;
    if (plFileSystem::ResolvePath("RmlUI/LatoLatin-Regular.ttf", &sSourceFolder, nullptr).Failed())
    {
      report.Warning(sSourceName, "The bundled fallback fonts were not found; install the RmlUi package to import web fonts. "
                                  "Text without an imported font will use the bitmap debug font.");
      return result;
    }

    sSourceFolder.PathParentDirectory();

    plStringBuilder sTargetFolder = sFolderAbs;
    sTargetFolder.AppendPath("ImportedFonts");
    if (plOSFile::CreateDirectoryStructure(sTargetFolder).Failed())
    {
      report.Warning(sSourceName, "The ImportedFonts folder could not be created; text without an imported web font will use the bitmap debug font.");
      return result;
    }

    struct FontCopy
    {
      const char* m_szSource;
      const char* m_szTarget;
      plString* m_pResult;
    };

    FontCopy copies[] = {
      {"LatoLatin-Regular.ttf", "PlasmaFallback-LatoLatin-Regular.ttf", &result.m_sRegular},
      {"LatoLatin-Bold.ttf", "PlasmaFallback-LatoLatin-Bold.ttf", &result.m_sBold},
      {"LatoLatin-Italic.ttf", "PlasmaFallback-LatoLatin-Italic.ttf", &result.m_sItalic},
      {"LatoLatin-BoldItalic.ttf", "PlasmaFallback-LatoLatin-BoldItalic.ttf", &result.m_sBoldItalic},
    };

    for (const FontCopy& copy : copies)
    {
      plStringBuilder sSource = sSourceFolder;
      sSource.AppendPath(copy.m_szSource);

      plStringBuilder sTarget = sTargetFolder;
      sTarget.AppendPath(copy.m_szTarget);

      plFileStats stats;
      const bool bTargetExists = plOSFile::GetFileStats(sTarget, stats).Succeeded() && stats.m_uiFileSize > 0;
      if (!bTargetExists && plOSFile::CopyFile(sSource, sTarget).Failed())
      {
        plStringBuilder sMessage;
        sMessage.SetFormat("Bundled fallback font '{}' could not be copied; some text may use the bitmap debug font.", copy.m_szSource);
        report.Warning(sSourceName, sMessage);
        continue;
      }

      *copy.m_pResult = sTarget;
    }

    // Lato is redistributed under the SIL Open Font License. Keep the license beside
    // copied fallback files so generated projects remain self-contained and compliant.
    {
      plStringBuilder sSource = sSourceFolder;
      sSource.AppendPath("LICENSE.txt");

      plStringBuilder sTarget = sTargetFolder;
      sTarget.AppendPath("PlasmaFallback-Lato-LICENSE.txt");

      plFileStats stats;
      if (!(plOSFile::GetFileStats(sTarget, stats).Succeeded() && stats.m_uiFileSize > 0))
        plOSFile::CopyFile(sSource, sTarget).IgnoreResult();
    }

    return result;
  }

  plStringView GetStyle(const ComputedStyle& style, plStringView sName, plStringView sFallback = {})
  {
    const plString* pValue = style.GetValue(plString(sName));
    return pValue != nullptr ? pValue->GetView() : sFallback;
  }

  constexpr float s_fHtmlReferenceWidth = 1920.0f;
  constexpr float s_fHtmlReferenceHeight = 1080.0f;

  plResult ParseLength(plStringView sValue, float fReference, float fFontSize, float& out_fPx)
  {
    plStringBuilder sVal = sValue;
    sVal.Trim(" \t");

    if (sVal.IsEmpty() || sVal.IsEqual_NoCase("auto"))
      return PL_FAILURE;

    if (sVal.StartsWith_NoCase("calc(") && sVal.EndsWith(")"))
    {
      sVal.Shrink(5, 1);
      sVal.Trim(" \t");

      const char* szOperator = sVal.FindSubString(" + ");
      bool bSubtract = false;
      if (szOperator == nullptr)
      {
        szOperator = sVal.FindSubString(" - ");
        bSubtract = szOperator != nullptr;
      }

      if (szOperator != nullptr)
      {
        plStringBuilder sLeft, sRight;
        sLeft.SetSubString_FromTo(sVal.GetData(), szOperator);
        sRight.SetSubString_FromTo(szOperator + 3, sVal.GetView().GetEndPointer());

        float fLeft = 0.0f;
        float fRight = 0.0f;
        if (ParseLength(sLeft, fReference, fFontSize, fLeft).Succeeded() &&
            ParseLength(sRight, fReference, fFontSize, fRight).Succeeded())
        {
          out_fPx = bSubtract ? fLeft - fRight : fLeft + fRight;
          return PL_SUCCESS;
        }
      }
      return PL_FAILURE;
    }

    float fScale = 1.0f;
    if (sVal.EndsWith_NoCase("px"))
      sVal.Shrink(0, 2);
    else if (sVal.EndsWith("%"))
    {
      sVal.Shrink(0, 1);
      fScale = fReference / 100.0f;
    }
    else if (sVal.EndsWith_NoCase("rem"))
    {
      sVal.Shrink(0, 3);
      fScale = 16.0f;
    }
    else if (sVal.EndsWith_NoCase("em"))
    {
      sVal.Shrink(0, 2);
      fScale = fFontSize;
    }
    else if (sVal.EndsWith_NoCase("pt"))
    {
      sVal.Shrink(0, 2);
      fScale = 96.0f / 72.0f;
    }
    else if (sVal.EndsWith_NoCase("vmin"))
    {
      sVal.Shrink(0, 4);
      fScale = plMath::Min(s_fHtmlReferenceWidth, s_fHtmlReferenceHeight) / 100.0f;
    }
    else if (sVal.EndsWith_NoCase("vmax"))
    {
      sVal.Shrink(0, 4);
      fScale = plMath::Max(s_fHtmlReferenceWidth, s_fHtmlReferenceHeight) / 100.0f;
    }
    else if (sVal.EndsWith_NoCase("vw"))
    {
      sVal.Shrink(0, 2);
      fScale = s_fHtmlReferenceWidth / 100.0f;
    }
    else if (sVal.EndsWith_NoCase("vh"))
    {
      sVal.Shrink(0, 2);
      fScale = s_fHtmlReferenceHeight / 100.0f;
    }

    double fVal = 0.0;
    if (plConversionUtils::StringToFloat(sVal, fVal).Failed())
      return PL_FAILURE;

    out_fPx = static_cast<float>(fVal) * fScale;
    return PL_SUCCESS;
  }

  /// CSS shorthand order is Top Right Bottom Left; the engine's padding vectors are Left/Top/Right/Bottom.
  plVec4 ParseFourValues(plStringView sValue, float fFontSize, float fReferenceWidth = s_fHtmlReferenceWidth, float fReferenceHeight = s_fHtmlReferenceHeight)
  {
    plHybridArray<plStringView, 4> parts;
    plStringBuilder sVal = sValue;
    sVal.Split(false, parts, " ", "\t");

    float v[4] = {0, 0, 0, 0}; // t r b l
    const plUInt32 uiCount = plMath::Min<plUInt32>(parts.GetCount(), 4);

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      float fPx = 0.0f;
      const float fReference = (i == 0 || i == 2) ? fReferenceHeight : fReferenceWidth;
      if (ParseLength(parts[i], fReference, fFontSize, fPx).Succeeded())
        v[i] = fPx;
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
    else if (uiCount == 3)
    {
      v[3] = v[1];
    }

    return plVec4(v[3], v[0], v[1], v[2]); // -> left, top, right, bottom
  }

  plVec4 ParseBoxValues(const ComputedStyle& style, plStringView sProperty, float fFontSize, float fReferenceWidth, float fReferenceHeight)
  {
    plVec4 result = ParseFourValues(GetStyle(style, sProperty, "0"), fFontSize, fReferenceWidth, fReferenceHeight);

    const char* names[4] = {"left", "top", "right", "bottom"};
    for (plUInt32 i = 0; i < 4; ++i)
    {
      plStringBuilder sLonghand;
      sLonghand.SetFormat("{}-{}", sProperty, names[i]);
      float fPx = 0.0f;
      const float fReference = (i == 0 || i == 2) ? fReferenceWidth : fReferenceHeight;
      if (ParseLength(GetStyle(style, sLonghand), fReference, fFontSize, fPx).Succeeded())
      {
        if (i == 0)
          result.x = fPx;
        else if (i == 1)
          result.y = fPx;
        else if (i == 2)
          result.z = fPx;
        else
          result.w = fPx;
      }
    }

    return result;
  }

  struct Inherited
  {
    plColorGammaUB m_TextColor = plColorGammaUB(255, 255, 255, 255);
    float m_fFontSize = 16.0f;
    float m_fLineHeight = 1.2f;
    plInt32 m_iTextAlign = plUiTextHAlign::Left;
    plString m_sFontFamily;
    plString m_sFontFile;
    plInt32 m_iFontWeight = 400;
    bool m_bItalic = false;
  };

  struct HtmlImportContext
  {
    CssSheet m_Css;
    plUiImportReport* m_pReport = nullptr;
    plString m_sHtmlFolderAbs;
    plMap<plString, RemoteFontFace> m_RemoteFontFaces;
    BundledFallbackFonts m_FallbackFonts;
    bool m_bFetchRemote = false; ///< remote stylesheets and fonts are downloaded
    bool m_bUsedFallbackFont = false;

    plString ResolveFontFile(plStringView sFamily, plInt32 iWeight, bool bItalic)
    {
      if (!sFamily.IsEmpty())
      {
        const RemoteFontFace* pExact = m_RemoteFontFaces.GetValue(MakeFontFaceKey(sFamily, iWeight, bItalic));
        if (pExact != nullptr && !pExact->m_sLocalFile.IsEmpty())
          return pExact->m_sLocalFile;

        const plString sNormalized = NormalizeFontFamily(sFamily);
        const RemoteFontFace* pBest = nullptr;
        plInt32 iBestScore = plMath::MaxValue<plInt32>();
        for (auto it = m_RemoteFontFaces.GetIterator(); it.IsValid(); ++it)
        {
          const RemoteFontFace& face = it.Value();
          if (NormalizeFontFamily(face.m_sFamily) != sNormalized || face.m_sLocalFile.IsEmpty())
            continue;

          const plInt32 iScore = plMath::Abs(face.m_iWeight - iWeight) + (face.m_bItalic == bItalic ? 0 : 1000);
          if (iScore < iBestScore)
          {
            pBest = &face;
            iBestScore = iScore;
          }
        }

        if (pBest != nullptr)
          return pBest->m_sLocalFile;
      }

      if (m_FallbackFonts.IsUsable())
      {
        m_bUsedFallbackFont = true;
        return plString(m_FallbackFonts.Select(iWeight, bItalic));
      }

      return {};
    }

    void UpdateInherited(const ComputedStyle& style, Inherited& ref_inherited)
    {
      plStringView sColor = GetStyle(style, "color");
      if (!sColor.IsEmpty())
      {
        plColorGammaUB col;
        if (plUiImportUtils::ParseColor(sColor, false, col).Succeeded())
          ref_inherited.m_TextColor = col;
      }

      // CSS font shorthand: [style] [weight] <size>[/<line-height>] <family>.
      // The engine currently uses its assigned UI font, but size and line-height are
      // still essential to preserve authored layout.
      plStringView sFont = GetStyle(style, "font");
      if (!sFont.IsEmpty())
      {
        plHybridArray<plStringView, 16> tokens;
        plStringBuilder sFontCopy = sFont;
        sFontCopy.Split(false, tokens, " ", "\t");

        ref_inherited.m_iFontWeight = 400;
        ref_inherited.m_bItalic = false;

        for (plUInt32 uiToken = 0; uiToken < tokens.GetCount(); ++uiToken)
        {
          const plStringView sToken = tokens[uiToken];
          if (sToken.IsEqual_NoCase("italic") || sToken.IsEqual_NoCase("oblique"))
          {
            ref_inherited.m_bItalic = true;
            continue;
          }
          if (sToken.IsEqual_NoCase("bold"))
          {
            ref_inherited.m_iFontWeight = 700;
            continue;
          }

          plInt32 iWeight = 0;
          if (plConversionUtils::StringToInt(sToken, iWeight).Succeeded() && iWeight >= 100 && iWeight <= 900)
          {
            ref_inherited.m_iFontWeight = iWeight;
            continue;
          }

          plStringBuilder sSize = sToken;
          plStringBuilder sLineHeight;
          const char* szSlash = sSize.FindSubString("/");
          if (szSlash != nullptr)
          {
            sLineHeight.SetSubString_FromTo(szSlash + 1, sSize.GetView().GetEndPointer());
            plStringBuilder sOnlySize;
            sOnlySize.SetSubString_FromTo(sSize.GetData(), szSlash);
            sSize = sOnlySize;
          }

          float fPx = 0.0f;
          if (ParseLength(sSize, ref_inherited.m_fFontSize, ref_inherited.m_fFontSize, fPx).Succeeded() &&
              (sSize.FindSubString("px") != nullptr || sSize.FindSubString("pt") != nullptr ||
                sSize.FindSubString("em") != nullptr || sSize.FindSubString("%") != nullptr ||
                sSize.FindSubString("vw") != nullptr || sSize.FindSubString("vh") != nullptr))
          {
            ref_inherited.m_fFontSize = fPx;

            if (!sLineHeight.IsEmpty())
            {
              double fMultiplier = 0.0;
              if (plConversionUtils::StringToFloat(sLineHeight, fMultiplier).Succeeded())
              {
                ref_inherited.m_fLineHeight = static_cast<float>(fMultiplier);
              }
              else
              {
                float fLinePx = 0.0f;
                if (ParseLength(sLineHeight, ref_inherited.m_fFontSize, ref_inherited.m_fFontSize, fLinePx).Succeeded())
                  ref_inherited.m_fLineHeight = fLinePx / plMath::Max(ref_inherited.m_fFontSize, 1.0f);
              }
            }

            if (uiToken + 1 < tokens.GetCount())
            {
              plStringBuilder sFamilyRemainder;
              sFamilyRemainder.SetSubString_FromTo(tokens[uiToken + 1].GetStartPointer(), sFontCopy.GetView().GetEndPointer());
              ref_inherited.m_sFontFamily = ParseFirstFontFamily(sFamilyRemainder);
            }
            break;
          }
        }
      }

      plStringView sFontFamily = GetStyle(style, "font-family");
      if (!sFontFamily.IsEmpty())
        ref_inherited.m_sFontFamily = ParseFirstFontFamily(sFontFamily);

      plStringView sFontWeight = GetStyle(style, "font-weight");
      if (!sFontWeight.IsEmpty())
      {
        if (sFontWeight.IsEqual_NoCase("bold"))
        {
          ref_inherited.m_iFontWeight = 700;
        }
        else if (sFontWeight.IsEqual_NoCase("normal"))
        {
          ref_inherited.m_iFontWeight = 400;
        }
        else
        {
          plInt32 iWeight = ref_inherited.m_iFontWeight;
          if (plConversionUtils::StringToInt(sFontWeight, iWeight).Succeeded())
            ref_inherited.m_iFontWeight = plMath::Clamp(iWeight, 100, 900);
        }
      }

      plStringView sFontStyle = GetStyle(style, "font-style");
      if (!sFontStyle.IsEmpty())
        ref_inherited.m_bItalic = sFontStyle.IsEqual_NoCase("italic") || sFontStyle.IsEqual_NoCase("oblique");

      plStringView sFontSize = GetStyle(style, "font-size");
      if (!sFontSize.IsEmpty())
      {
        float fPx = 0.0f;
        if (ParseLength(sFontSize, ref_inherited.m_fFontSize, ref_inherited.m_fFontSize, fPx).Succeeded())
          ref_inherited.m_fFontSize = fPx;
      }

      plStringView sLineHeight = GetStyle(style, "line-height");
      if (!sLineHeight.IsEmpty() && sLineHeight != "normal")
      {
        double fMultiplier = 0.0;
        if (plConversionUtils::StringToFloat(sLineHeight, fMultiplier).Succeeded())
        {
          ref_inherited.m_fLineHeight = static_cast<float>(fMultiplier);
        }
        else
        {
          float fLinePx = 0.0f;
          if (ParseLength(sLineHeight, ref_inherited.m_fFontSize, ref_inherited.m_fFontSize, fLinePx).Succeeded())
            ref_inherited.m_fLineHeight = fLinePx / plMath::Max(ref_inherited.m_fFontSize, 1.0f);
        }
      }

      plStringView sAlign = GetStyle(style, "text-align");
      if (sAlign == "center")
        ref_inherited.m_iTextAlign = plUiTextHAlign::Center;
      else if (sAlign == "right")
        ref_inherited.m_iTextAlign = plUiTextHAlign::Right;
      else if (sAlign == "left")
        ref_inherited.m_iTextAlign = plUiTextHAlign::Left;

      if (!ref_inherited.m_sFontFamily.IsEmpty())
        ref_inherited.m_sFontFile = ResolveFontFile(ref_inherited.m_sFontFamily, ref_inherited.m_iFontWeight, ref_inherited.m_bItalic);
    }
  };

  bool IsTextTag(plStringView sTag)
  {
    return sTag == "p" || sTag == "span" || sTag == "label" || sTag == "b" || sTag == "i" ||
           sTag == "strong" || sTag == "em" || sTag == "small" || sTag == "li" || sTag == "td" || sTag == "th" ||
           sTag == "h1" || sTag == "h2" || sTag == "h3" || sTag == "h4" || sTag == "h5" || sTag == "h6";
  }

  bool IsIgnoredTag(plStringView sTag)
  {
    return sTag == "script" || sTag == "style" || sTag == "head" || sTag == "meta" || sTag == "link" ||
           sTag == "title" || sTag == "br" || sTag == "template" || sTag == "noscript";
  }

  float DefaultFontSizeForTag(plStringView sTag, float fInherited)
  {
    if (sTag == "h1")
      return 32.0f;
    if (sTag == "h2")
      return 28.0f;
    if (sTag == "h3")
      return 24.0f;
    if (sTag == "h4")
      return 20.0f;
    if (sTag == "h5")
      return 18.0f;
    if (sTag == "h6")
      return 16.0f;
    return fInherited;
  }

  // forward
  float EstimateHeight(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, float fAvailWidth);
  float EstimateWidth(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, float fAvailWidth);

  plUInt32 CountTextLines(plStringView sText)
  {
    plUInt32 uiLines = 1;
    for (const char* sz = sText.GetStartPointer(); sz < sText.GetEndPointer(); ++sz)
    {
      if (*sz == '\n')
        ++uiLines;
    }
    return uiLines;
  }

  float EstimateChildrenHeight(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, float fAvailWidth)
  {
    ComputedStyle nodeStyle;
    ctx.m_Css.Collect(node, nodeStyle);
    const bool bHorizontal = GetStyle(nodeStyle, "display") == "flex" &&
                             (GetStyle(nodeStyle, "flex-direction", "row") == "row" || GetStyle(nodeStyle, "flex-direction") == "row-reverse");

    float fTotal = 0.0f;
    plUInt32 uiVisibleChildren = 0;
    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (IsIgnoredTag(pChild->m_sTag))
        continue;

      ComputedStyle childStyle;
      ctx.m_Css.Collect(*pChild, childStyle);
      Inherited childInherited = inherited;
      childInherited.m_fFontSize = DefaultFontSizeForTag(pChild->m_sTag, childInherited.m_fFontSize);
      ctx.UpdateInherited(childStyle, childInherited);
      const plVec4 margin = ParseBoxValues(childStyle, "margin", childInherited.m_fFontSize, fAvailWidth, s_fHtmlReferenceHeight);
      const float fChildHeight = EstimateHeight(*pChild, ctx, inherited, fAvailWidth) + margin.y + margin.w;

      if (bHorizontal)
        fTotal = plMath::Max(fTotal, fChildHeight);
      else
        fTotal += fChildHeight;
      ++uiVisibleChildren;
    }

    float fGap = 0.0f;
    ParseLength(GetStyle(nodeStyle, "gap", "0"), bHorizontal ? fAvailWidth : s_fHtmlReferenceHeight, inherited.m_fFontSize, fGap).IgnoreResult();
    if (!bHorizontal && uiVisibleChildren > 1)
      fTotal += (uiVisibleChildren - 1) * fGap;

    if (!node.m_sText.IsEmpty())
    {
      const float fTextHeight = CountTextLines(node.m_sText.GetView()) * inherited.m_fFontSize * inherited.m_fLineHeight;
      fTotal = bHorizontal ? plMath::Max(fTotal, fTextHeight) : fTotal + fTextHeight;
    }
    return fTotal;
  }

  float EstimateHeight(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, float fAvailWidth)
  {
    ComputedStyle style;
    ctx.m_Css.Collect(node, style);

    Inherited local = inherited;
    local.m_fFontSize = DefaultFontSizeForTag(node.m_sTag, local.m_fFontSize);
    ctx.UpdateInherited(style, local);

    float fHeight = 0.0f;
    if (ParseLength(GetStyle(style, "height"), s_fHtmlReferenceHeight, local.m_fFontSize, fHeight).Succeeded())
      return fHeight;

    const plVec4 vPadding = ParseBoxValues(style, "padding", local.m_fFontSize, fAvailWidth, s_fHtmlReferenceHeight);

    if (IsTextTag(node.m_sTag) || node.m_sTag == "a")
      return CountTextLines(node.m_sText.GetView()) * local.m_fFontSize * local.m_fLineHeight + vPadding.y + vPadding.w;
    if (node.m_sTag == "button")
      return plMath::Max(44.0f, local.m_fFontSize * local.m_fLineHeight + vPadding.y + vPadding.w);
    if (node.m_sTag == "input")
      return 36.0f;
    if (node.m_sTag == "progress")
      return 22.0f;
    if (node.m_sTag == "img")
    {
      float fH = 0.0f;
      if (ParseLength(node.GetAttribute("height"), 0.0f, local.m_fFontSize, fH).Succeeded())
        return fH;
      return 100.0f;
    }
    if (node.m_sTag == "svg")
    {
      float fH = 0.0f;
      if (ParseLength(node.GetAttribute("height"), s_fHtmlReferenceHeight, local.m_fFontSize, fH).Succeeded())
        return fH;
    }

    // container: sum of the (estimated) vertical flow + padding
    return EstimateChildrenHeight(node, ctx, local, fAvailWidth) + vPadding.y + vPadding.w;
  }

  float EstimateWidth(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, float fAvailWidth)
  {
    ComputedStyle style;
    ctx.m_Css.Collect(node, style);

    Inherited local = inherited;
    local.m_fFontSize = DefaultFontSizeForTag(node.m_sTag, local.m_fFontSize);
    ctx.UpdateInherited(style, local);

    float fWidth = 0.0f;
    if (ParseLength(GetStyle(style, "width"), fAvailWidth, local.m_fFontSize, fWidth).Succeeded())
      return fWidth;

    const plVec4 vPadding = ParseBoxValues(style, "padding", local.m_fFontSize, fAvailWidth, s_fHtmlReferenceHeight);

    if (node.m_sTag == "img")
    {
      float fW = 0.0f;
      if (ParseLength(node.GetAttribute("width"), fAvailWidth, inherited.m_fFontSize, fW).Succeeded())
        return fW;
      return 100.0f;
    }
    if (node.m_sTag == "svg")
    {
      float fW = 0.0f;
      if (ParseLength(node.GetAttribute("width"), fAvailWidth, local.m_fFontSize, fW).Succeeded())
        return fW;
    }
    if (node.m_sTag == "button" || node.m_sTag == "a")
    {
      const float fHorizontalPadding = vPadding.x + vPadding.z;
      return plMath::Max(node.m_sTag == "button" ? 120.0f : 1.0f,
        static_cast<float>(node.m_sText.GetCharacterCount()) * local.m_fFontSize * 0.6f + fHorizontalPadding);
    }
    if (IsTextTag(node.m_sTag) || node.m_sTag == "span")
      return plMath::Max(1.0f, static_cast<float>(node.m_sText.GetCharacterCount()) * local.m_fFontSize * 0.55f);

    const bool bHorizontal = GetStyle(style, "display") == "flex" &&
                             (GetStyle(style, "flex-direction", "row") == "row" || GetStyle(style, "flex-direction") == "row-reverse");

    bool bParentHorizontalFlex = false;
    if (node.m_pParent != nullptr)
    {
      ComputedStyle parentStyle;
      ctx.m_Css.Collect(*node.m_pParent, parentStyle);
      bParentHorizontalFlex = GetStyle(parentStyle, "display") == "flex" &&
                              (GetStyle(parentStyle, "flex-direction", "row") == "row" || GetStyle(parentStyle, "flex-direction") == "row-reverse");
    }

    if (bHorizontal || bParentHorizontalFlex)
    {
      float fIntrinsic = 0.0f;
      plUInt32 uiChildren = 0;
      for (const plUiMarkupNode* pChild : node.m_Children)
      {
        if (IsIgnoredTag(pChild->m_sTag))
          continue;

        ComputedStyle childStyle;
        ctx.m_Css.Collect(*pChild, childStyle);
        Inherited childInherited = local;
        childInherited.m_fFontSize = DefaultFontSizeForTag(pChild->m_sTag, childInherited.m_fFontSize);
        ctx.UpdateInherited(childStyle, childInherited);
        const plVec4 margin = ParseBoxValues(childStyle, "margin", childInherited.m_fFontSize, fAvailWidth, s_fHtmlReferenceHeight);
        const float fChildWidth = EstimateWidth(*pChild, ctx, local, fAvailWidth) + margin.x + margin.z;

        fIntrinsic = bHorizontal ? fIntrinsic + fChildWidth : plMath::Max(fIntrinsic, fChildWidth);
        ++uiChildren;
      }

      if (!node.m_sText.IsEmpty())
      {
        const float fTextWidth = static_cast<float>(node.m_sText.GetCharacterCount()) * local.m_fFontSize * 0.55f;
        fIntrinsic = bHorizontal ? fIntrinsic + fTextWidth : plMath::Max(fIntrinsic, fTextWidth);
      }

      float fGap = 0.0f;
      ParseLength(GetStyle(style, "gap", "0"), fAvailWidth, local.m_fFontSize, fGap).IgnoreResult();
      if (bHorizontal && uiChildren > 1)
        fIntrinsic += (uiChildren - 1) * fGap;
      fWidth = fIntrinsic + vPadding.x + vPadding.z;
    }
    else
    {
      fWidth = fAvailWidth;
    }

    float fMaxWidth = 0.0f;
    if (ParseLength(GetStyle(style, "max-width"), fAvailWidth, local.m_fFontSize, fMaxWidth).Succeeded())
      fWidth = plMath::Min(fWidth, fMaxWidth);

    return plMath::Max(fWidth, 1.0f);
  }

  void SplitCssFunctionArguments(plStringView sArguments, plHybridArray<plString, 8>& out_parts)
  {
    const char* szStart = sArguments.GetStartPointer();
    const char* szCur = szStart;
    plInt32 iDepth = 0;

    while (szCur < sArguments.GetEndPointer())
    {
      if (*szCur == '(')
        ++iDepth;
      else if (*szCur == ')')
        --iDepth;
      else if (*szCur == ',' && iDepth == 0)
      {
        plStringBuilder sPart(plStringView(szStart, szCur));
        sPart.Trim(" \t\r\n");
        out_parts.PushBack(plString(sPart));
        szStart = szCur + 1;
      }
      ++szCur;
    }

    plStringBuilder sPart(plStringView(szStart, sArguments.GetEndPointer()));
    sPart.Trim(" \t\r\n");
    if (!sPart.IsEmpty())
      out_parts.PushBack(plString(sPart));
  }

  bool ParseGradientStopColor(plStringView sStop, plColorGammaUB& out_color)
  {
    plStringBuilder sColor = sStop;
    sColor.Trim(" \t\r\n");

    if (sColor.StartsWith_NoCase("rgb(") || sColor.StartsWith_NoCase("rgba(") ||
        sColor.StartsWith_NoCase("hsl(") || sColor.StartsWith_NoCase("hsla("))
    {
      plInt32 iDepth = 0;
      const char* szEnd = nullptr;
      for (const char* sz = sColor.GetData(); *sz != '\0'; ++sz)
      {
        if (*sz == '(')
          ++iDepth;
        else if (*sz == ')' && --iDepth == 0)
        {
          szEnd = sz + 1;
          break;
        }
      }
      if (szEnd != nullptr)
      {
        plStringBuilder sFunction;
        sFunction.SetSubString_FromTo(sColor.GetData(), szEnd);
        sColor = sFunction;
      }
    }
    else
    {
      const char* szSpace = sColor.FindSubString(" ");
      if (szSpace != nullptr)
      {
        plStringBuilder sHead;
        sHead.SetSubString_FromTo(sColor.GetData(), szSpace);
        sColor = sHead;
      }
    }

    return plUiImportUtils::ParseColor(sColor, false, out_color).Succeeded();
  }

  /// Applies the shared visual-element styling (background, border, radius, shadow, gradient, opacity).
  void ApplyVisualStyle(plUiImportNode& ref_ir, const ComputedStyle& style, HtmlImportContext& ctx, plStringView sPath, float fFontSize, bool bDefaultTransparent)
  {
    bool bHasBackground = false;

    plStringView sBackground = GetStyle(style, "background-color");
    if (sBackground.IsEmpty())
    {
      plStringView sShorthand = GetStyle(style, "background");
      if (!sShorthand.IsEmpty() && sShorthand.FindSubString("(") == nullptr)
        sBackground = sShorthand; // plain color shorthand
    }

    if (!sBackground.IsEmpty())
    {
      plColorGammaUB col;
      if (plUiImportUtils::ParseColor(sBackground, false, col).Succeeded())
      {
        ref_ir.Set("Color", col);
        bHasBackground = true;
      }
      else
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Background color '{}' not understood.", sBackground);
        ctx.m_pReport->Warning(sPath, sMsg);
      }
    }

    // background-image: url() or linear-gradient()
    plStringView sBgImage = GetStyle(style, "background-image");
    if (sBgImage.IsEmpty())
    {
      plStringView sShorthand = GetStyle(style, "background");
      if (sShorthand.FindSubString("(") != nullptr)
        sBgImage = sShorthand;
    }

    if (!sBgImage.IsEmpty())
    {
      if (sBgImage.StartsWith_NoCase("url"))
      {
        plStringBuilder sUrl = sBgImage;
        sUrl.TrimWordStart("url");
        sUrl.Trim(" \t()'\"");

        plStringBuilder sAbs = ctx.m_sHtmlFolderAbs;
        sAbs.AppendPath(sUrl);
        sAbs.MakeCleanPath();

        if (plOSFile::ExistsFile(sAbs))
        {
          ref_ir.m_sTextureFile = sAbs;
          ref_ir.Set("BrushMode", (plInt32)plUiBrushMode::Image);
          if (!bHasBackground)
            ref_ir.Set("Color", plColorGammaUB(255, 255, 255, 255));
          bHasBackground = true;
        }
        else
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("background-image file not found: '{}'.", sUrl);
          ctx.m_pReport->Warning(sPath, sMsg);
        }
      }
      else if (sBgImage.StartsWith_NoCase("linear-gradient") || sBgImage.StartsWith_NoCase("repeating-linear-gradient"))
      {
        const bool bRepeating = sBgImage.StartsWith_NoCase("repeating-linear-gradient");
        plStringBuilder sArgs = sBgImage;
        sArgs.TrimWordStart(bRepeating ? "repeating-linear-gradient" : "linear-gradient");
        sArgs.Trim(" \t");
        if (sArgs.StartsWith("(") && sArgs.EndsWith(")"))
          sArgs.Shrink(1, 1);

        plHybridArray<plString, 8> parts;
        SplitCssFunctionArguments(sArgs, parts);

        plInt32 iMode = plUiGradientMode::Vertical; // CSS default: to bottom
        plUInt32 uiFirstColor = 0;

        if (parts.GetCount() > 0)
        {
          plStringBuilder sFirst = parts[0];
          sFirst.Trim(" \t");
          if (sFirst.StartsWith("to ") || sFirst.EndsWith("deg"))
          {
            uiFirstColor = 1;
            if (sFirst.FindSubString("right") != nullptr || sFirst.FindSubString("left") != nullptr)
              iMode = plUiGradientMode::Horizontal;
            else if (sFirst.EndsWith("deg"))
            {
              double fDeg = 0.0;
              plStringBuilder sDeg = sFirst;
              sDeg.TrimRight("deg");
              plConversionUtils::StringToFloat(sDeg, fDeg).IgnoreResult();
              const double fMod = plMath::Mod(plMath::Abs(fDeg), 360.0);
              iMode = (fMod > 45.0 && fMod < 135.0) || (fMod > 225.0 && fMod < 315.0) ? plUiGradientMode::Horizontal : plUiGradientMode::Vertical;
              const double fAxisRemainder = plMath::Mod(fMod, 90.0);
              const double fAxisDistance = plMath::Min(fAxisRemainder, 90.0 - fAxisRemainder);
              if (fAxisDistance > 0.01)
                ctx.m_pReport->Info(sPath, "Angled gradient snapped to the nearest axis.");
            }
          }
        }

        plColorGammaUB c1, c2;
        bool bOk = false;

        if (parts.GetCount() >= uiFirstColor + 2)
        {
          bOk = ParseGradientStopColor(parts[uiFirstColor], c1) &&
                ParseGradientStopColor(parts[parts.GetCount() - 1], c2);
        }

        if (bOk)
        {
          ref_ir.Set("Color", c1);
          ref_ir.Set("GradientMode", iMode);
          ref_ir.Set("GradientColor", c2);
          if (parts.GetCount() > uiFirstColor + 2)
            ctx.m_pReport->Info(sPath, "Gradient has more than two stops; only the first and last import.");
          if (bRepeating)
            ctx.m_pReport->Info(sPath, "Repeating gradient imported as a non-repeating two-color gradient.");
          bHasBackground = true;
        }
        else
        {
          ctx.m_pReport->Warning(sPath, "CSS gradient could not be parsed; using flat color.");
        }
      }
      else
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Unsupported background-image '{}'.", sBgImage);
        ctx.m_pReport->Warning(sPath, sMsg);
      }
    }

    if (!bHasBackground && bDefaultTransparent)
    {
      ref_ir.Set("Color", plColorGammaUB(0, 0, 0, 0));
    }

    // border: "1px solid #fff"
    plStringView sBorder = GetStyle(style, "border");
    if (!sBorder.IsEmpty() && sBorder != "none" && sBorder != "0")
    {
      plHybridArray<plStringView, 4> parts;
      plStringBuilder sB = sBorder;
      sB.Split(false, parts, " ");

      float fWidth = 1.0f;
      plColorGammaUB col(255, 255, 255, 255);
      for (plStringView sPart : parts)
      {
        float fPx = 0.0f;
        plColorGammaUB c;
        if (ParseLength(sPart, 0.0f, fFontSize, fPx).Succeeded())
          fWidth = fPx;
        else if (plUiImportUtils::ParseColor(sPart, false, c).Succeeded())
          col = c;
      }

      ref_ir.Set("BorderWidth", fWidth);
      ref_ir.Set("BorderColor", col);
    }

    plStringView sRadius = GetStyle(style, "border-radius");
    if (!sRadius.IsEmpty())
    {
      // CSS radius shorthand order: tl, tr, br, bl - matches the engine's CornerRadius order
      plHybridArray<plStringView, 4> parts;
      plStringBuilder sR = sRadius;
      sR.Split(false, parts, " ");
      float v[4] = {0, 0, 0, 0};
      for (plUInt32 i = 0; i < parts.GetCount() && i < 4; ++i)
      {
        ParseLength(parts[i], 0.0f, fFontSize, v[i]).IgnoreResult();
      }
      if (parts.GetCount() == 1)
        v[1] = v[2] = v[3] = v[0];
      else if (parts.GetCount() == 2)
      {
        v[2] = v[0];
        v[3] = v[1];
      }
      else if (parts.GetCount() == 3)
        v[3] = v[1];

      ref_ir.Set("CornerRadius", plVec4(v[0], v[1], v[2], v[3])); // tl, tr, br, bl
    }

    plStringView sShadow = GetStyle(style, "box-shadow");
    if (!sShadow.IsEmpty() && sShadow != "none")
    {
      if (sShadow.FindSubString("inset") != nullptr)
      {
        ctx.m_pReport->Warning(sPath, "inset box-shadow is unsupported; dropped.");
      }
      else
      {
        if (sShadow.FindSubString(",") != nullptr)
          ctx.m_pReport->Info(sPath, "Multiple box-shadows; only the first imports.");

        plStringBuilder sFirst = sShadow;
        {
          // careful: rgba(...) contains commas - only cut at a comma outside parentheses
          plInt32 iDepth = 0;
          const char* szCut = nullptr;
          for (const char* s = sFirst.GetData(); *s != '\0'; ++s)
          {
            if (*s == '(')
              ++iDepth;
            else if (*s == ')')
              --iDepth;
            else if (*s == ',' && iDepth == 0)
            {
              szCut = s;
              break;
            }
          }
          if (szCut != nullptr)
          {
            plStringBuilder sHead;
            sHead.SetSubString_FromTo(sFirst.GetData(), szCut);
            sFirst = sHead;
          }
        }

        // tokenize while keeping function calls together
        plHybridArray<plString, 6> tokens;
        {
          plStringBuilder sToken;
          plInt32 iDepth = 0;
          for (const char* s = sFirst.GetData(); *s != '\0'; ++s)
          {
            if (*s == '(')
              ++iDepth;
            else if (*s == ')')
              --iDepth;

            if ((*s == ' ' || *s == '\t') && iDepth == 0)
            {
              if (!sToken.IsEmpty())
                tokens.PushBack(plString(sToken.GetView()));
              sToken.Clear();
            }
            else
            {
              sToken.Append(plStringView(s, s + 1));
            }
          }
          if (!sToken.IsEmpty())
            tokens.PushBack(plString(sToken.GetView()));
        }

        float fLengths[3] = {0, 4, 8}; // x, y, blur
        plUInt32 uiLengths = 0;
        plColorGammaUB col(0, 0, 0, 150);

        for (const plString& sToken : tokens)
        {
          float fPx = 0.0f;
          plColorGammaUB c;
          if (ParseLength(sToken, 0.0f, fFontSize, fPx).Succeeded())
          {
            if (uiLengths < 3)
              fLengths[uiLengths] = fPx;
            ++uiLengths;
          }
          else if (plUiImportUtils::ParseColor(sToken, false, c).Succeeded())
          {
            col = c;
          }
        }

        ref_ir.Set("ShadowOffset", plVec2(fLengths[0], fLengths[1]));
        ref_ir.Set("ShadowSize", plMath::Max(fLengths[2], 1.0f));
        ref_ir.Set("ShadowColor", col);
      }
    }

    plStringView sOpacity = GetStyle(style, "opacity");
    if (!sOpacity.IsEmpty())
    {
      double fOpacity = 1.0;
      if (plConversionUtils::StringToFloat(sOpacity, fOpacity).Succeeded())
        ref_ir.Set("Opacity", static_cast<float>(plMath::Clamp(fOpacity, 0.0, 1.0)));
    }
  }

  plString MakeElementName(const plUiMarkupNode& node)
  {
    plStringView sId = node.GetAttribute("id");
    if (!sId.IsEmpty())
      return sId;

    plStringView sName = node.GetAttribute("name");
    if (!sName.IsEmpty())
      return sName;

    if ((node.m_sTag == "a" || node.m_sTag == "button") && !node.m_sText.IsEmpty())
    {
      plStringBuilder sText = node.m_sText;
      sText.Trim(" \t\r\n");
      sText.ReplaceAll(" ", "_");
      return sText;
    }

    return node.m_sTag;
  }

  // forward
  void ImportElement(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, plUiImportNode& ref_irParent, const plRectFloat& parentRect, bool bStackChild, plStringView sParentPath);

  void ApplyTextProperties(plUiImportNode& ref_ir, const ComputedStyle& style, const Inherited& inherited)
  {
    ref_ir.Set("TextColor", inherited.m_TextColor);
    ref_ir.Set("FontSize", static_cast<plInt32>(plMath::Clamp(inherited.m_fFontSize, 4.0f, 256.0f)));
    ref_ir.Set("HAlign", inherited.m_iTextAlign);
    ref_ir.m_sFontFile = inherited.m_sFontFile;

    if (GetStyle(style, "white-space") != "nowrap")
      ref_ir.Set("Wrap", true);

    if (GetStyle(style, "text-overflow") == "ellipsis")
      ref_ir.Set("Overflow", (plInt32)plUiTextOverflow::Ellipsis);
    else if (GetStyle(style, "overflow") == "hidden")
      ref_ir.Set("Overflow", (plInt32)plUiTextOverflow::Clip);
  }

  /// Imports the children of a container markup node into ref_irContainer.
  /// If any child is position:absolute, the container must offer free placement (Panel);
  /// otherwise the children flow as stack children.
  void ImportChildren(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, plUiImportNode& ref_irContainer, const plRectFloat& contentRect, bool bContainerIsStack, plStringView sPath)
  {
    ComputedStyle containerStyle;
    ctx.m_Css.Collect(node, containerStyle);
    const bool bHorizontalStack = bContainerIsStack && GetStyle(containerStyle, "display") == "flex" &&
                                  (GetStyle(containerStyle, "flex-direction", "row") == "row" || GetStyle(containerStyle, "flex-direction") == "row-reverse");

    // direct text content of a mixed container becomes a leading Text element
    bool bHasRenderableChild = false;
    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (!IsIgnoredTag(pChild->m_sTag))
      {
        bHasRenderableChild = true;
        break;
      }
    }

    if (!node.m_sText.IsEmpty() && bHasRenderableChild)
    {
      ctx.m_pReport->Info(sPath, "Mixed text and elements; the text imports as a separate Text element first.");
    }

    if (!node.m_sText.IsEmpty())
    {
      plUiImportNode* pText = ref_irContainer.AddChild(plGetStaticRTTI<plUiText>(), "Text");
      pText->Set("Text", node.m_sText.GetView());

      ComputedStyle style;
      ctx.m_Css.Collect(node, style);
      ApplyTextProperties(*pText, style, inherited);

      if (bContainerIsStack)
      {
        pText->m_bStackChild = true;
        pText->m_vOffsets = plVec4(0, 0, contentRect.width, CountTextLines(node.m_sText.GetView()) * inherited.m_fFontSize * inherited.m_fLineHeight);
      }
      else
      {
        pText->m_vAnchorMin = plVec2::MakeZero();
        pText->m_vAnchorMax = plVec2::MakeZero();
        pText->m_vOffsets = plVec4(0, 0, contentRect.width, CountTextLines(node.m_sText.GetView()) * inherited.m_fFontSize * inherited.m_fLineHeight);
      }
    }

    float fFlowY = contentRect.y;

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (IsIgnoredTag(pChild->m_sTag))
        continue;

      ComputedStyle childStyle;
      ctx.m_Css.Collect(*pChild, childStyle);
      const bool bAbsolute = GetStyle(childStyle, "position") == "absolute" || GetStyle(childStyle, "position") == "fixed";

      if (bContainerIsStack)
      {
        const bool bAutoMarginBefore = bHorizontalStack && GetStyle(childStyle, "margin-left") == "auto";
        if (bAutoMarginBefore)
        {
          plUiImportNode* pSpacer = ref_irContainer.AddChild(plGetStaticRTTI<plUiSpacer>(), "AutoMargin");
          pSpacer->m_bStackChild = true;
          pSpacer->m_vOffsets = plVec4(0, 0, 0, contentRect.height);
          pSpacer->Set("Width", 0.0f);
          pSpacer->Set("Height", contentRect.height);
          pSpacer->Set("Expand", true);
        }

        if (bAbsolute)
        {
          // stack containers cannot free-place; import as a stack child anyway
          ctx.m_pReport->Warning(sPath, "position:absolute inside a flex/flow container imports as a stacked child.");
        }
        ImportElement(*pChild, ctx, inherited, ref_irContainer, contentRect, true, sPath);

        const bool bAutoMarginAfter = bHorizontalStack && GetStyle(childStyle, "margin-right") == "auto";
        if (bAutoMarginAfter)
        {
          plUiImportNode* pSpacer = ref_irContainer.AddChild(plGetStaticRTTI<plUiSpacer>(), "AutoMargin");
          pSpacer->m_bStackChild = true;
          pSpacer->m_vOffsets = plVec4(0, 0, 0, contentRect.height);
          pSpacer->Set("Width", 0.0f);
          pSpacer->Set("Height", contentRect.height);
          pSpacer->Set("Expand", true);
        }
      }
      else
      {
        if (node.m_sTag == "svg")
        {
          const plUInt32 uiBefore = ref_irContainer.m_Children.GetCount();
          ImportElement(*pChild, ctx, inherited, ref_irContainer, contentRect, false, sPath);
          if (ref_irContainer.m_Children.GetCount() > uiBefore)
          {
            plUiImportNode* pIr = ref_irContainer.m_Children.PeekBack();
            pIr->m_vAnchorMin = plVec2::MakeZero();
            pIr->m_vAnchorMax = plVec2(1.0f);
            pIr->m_vOffsets = plVec4::MakeZero();
          }
          continue;
        }

        if (bAbsolute)
        {
          ImportElement(*pChild, ctx, inherited, ref_irContainer, contentRect, false, sPath);
        }
        else
        {
          // bake the vertical flow position for this child (the container is a free-placement
          // Panel because it also holds absolute children - flow cannot stay live)
          const float fH = EstimateHeight(*pChild, ctx, inherited, contentRect.width);
          const plRectFloat flowRect(contentRect.x, fFlowY, contentRect.width, fH);
          fFlowY += fH;

          const plUInt32 uiBefore = ref_irContainer.m_Children.GetCount();
          ImportElement(*pChild, ctx, inherited, ref_irContainer, flowRect, false, sPath);

          if (ref_irContainer.m_Children.GetCount() > uiBefore)
          {
            plUiImportNode* pIr = ref_irContainer.m_Children.PeekBack();
            pIr->m_vAnchorMin = plVec2::MakeZero();
            pIr->m_vAnchorMax = plVec2::MakeZero();
            pIr->m_vOffsets = plUiImportUtils::ComputePlacement(flowRect, contentRect, plVec2::MakeZero(), plVec2::MakeZero());
          }
        }
      }
    }
  }

  void ImportElement(const plUiMarkupNode& node, HtmlImportContext& ctx, const Inherited& inherited, plUiImportNode& ref_irParent, const plRectFloat& parentRect, bool bStackChild, plStringView sParentPath)
  {
    plStringBuilder sPath = sParentPath;
    sPath.AppendPath(node.m_sTag.GetData());

    ComputedStyle style;
    ctx.m_Css.Collect(node, style);

    Inherited local = inherited;
    local.m_fFontSize = DefaultFontSizeForTag(node.m_sTag, local.m_fFontSize);
    ctx.UpdateInherited(style, local);

    if (GetStyle(style, "display") == "none")
    {
      ctx.m_pReport->Info(sPath, "display:none imports with Visible = false.");
    }

    // ————— determine the target element type —————
    const plRTTI* pType = nullptr;
    const plStringView sTag = node.m_sTag;

    bool bIsContainer = false;
    bool bContainerIsStack = false;
    bool bIsScroll = false;

    plStringView sInputType;

    if (IsTextTag(sTag))
    {
      pType = plGetStaticRTTI<plUiText>();
    }
    else if (sTag == "img")
    {
      pType = plGetStaticRTTI<plUiImage>();
    }
    else if (sTag == "button")
    {
      pType = plGetStaticRTTI<plUiButton>();
    }
    else if (sTag == "a")
    {
      pType = plGetStaticRTTI<plUiButton>();
    }
    else if (sTag == "path")
    {
      pType = plGetStaticRTTI<plUiPath>();
    }
    else if (sTag == "progress")
    {
      pType = plGetStaticRTTI<plUiProgressBar>();
    }
    else if (sTag == "input")
    {
      sInputType = node.GetAttribute("type", "text");
      if (sInputType == "checkbox" || sInputType == "radio")
        pType = plGetStaticRTTI<plUiToggle>();
      else if (sInputType == "range")
        pType = plGetStaticRTTI<plUiSlider>();
      else if (sInputType == "text" || sInputType == "password" || sInputType == "search" || sInputType == "email" || sInputType == "number")
        pType = plGetStaticRTTI<plUiTextInput>();
      else
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("input type '{}' is unsupported; imported as a plain panel.", sInputType);
        ctx.m_pReport->Warning(sPath, sMsg);
        pType = plGetStaticRTTI<plUiPanel>();
      }
    }
    else
    {
      // generic container (div/section/ul/form/...)
      bIsContainer = true;

      const plStringView sOverflow = GetStyle(style, "overflow");
      const plStringView sOverflowY = GetStyle(style, "overflow-y");
      bIsScroll = sOverflow == "auto" || sOverflow == "scroll" || sOverflowY == "auto" || sOverflowY == "scroll";

      // absolute children force free placement (Panel)
      bool bHasAbsoluteChild = false;
      for (const plUiMarkupNode* pChild : node.m_Children)
      {
        ComputedStyle childStyle;
        ctx.m_Css.Collect(*pChild, childStyle);
        const plStringView sPos = GetStyle(childStyle, "position");
        if (sPos == "absolute" || sPos == "fixed")
        {
          bHasAbsoluteChild = true;
          break;
        }
      }

      if (sTag == "svg")
      {
        pType = plGetStaticRTTI<plUiPanel>();
        bContainerIsStack = false;
      }
      else if (bIsScroll)
      {
        pType = plGetStaticRTTI<plUiScrollView>();
        bContainerIsStack = true;
        if (bHasAbsoluteChild)
          ctx.m_pReport->Warning(sPath, "Scroll container has absolute-positioned children; they will stack.");
      }
      else if (bHasAbsoluteChild)
      {
        pType = plGetStaticRTTI<plUiPanel>();
        bContainerIsStack = false;
        if (GetStyle(style, "display") == "flex")
          ctx.m_pReport->Info(sPath, "Flex container has absolute children; the flow is baked at import time.");
      }
      else
      {
        pType = plGetStaticRTTI<plUiStackPanel>();
        bContainerIsStack = true;
      }
    }

    // ————— compute the element rect —————
    plRectFloat rect = parentRect;

    const bool bAbsolute = GetStyle(style, "position") == "absolute" || GetStyle(style, "position") == "fixed";

    plUiImportNode* pIr = ref_irParent.AddChild(pType, MakeElementName(node));
    if (bStackChild)
    {
      const plVec4 margin = ParseBoxValues(style, "margin", local.m_fFontSize, parentRect.width, parentRect.height);
      pIr->Set("LayoutMargin", margin);
    }

    if (bStackChild)
    {
      pIr->m_bStackChild = true;
      const float fW = EstimateWidth(node, ctx, local, parentRect.width);
      const float fH = EstimateHeight(node, ctx, local, parentRect.width);
      pIr->m_vOffsets = plVec4(0, 0, fW, fH);
      rect = plRectFloat(parentRect.x, parentRect.y, parentRect.width, fH);
    }
    else if (bAbsolute)
    {
      // anchors from left/right/top/bottom (% -> anchor fraction, px -> offset)
      plVec2 vAnchorMin = plVec2::MakeZero();
      plVec2 vAnchorMax = plVec2::MakeZero();
      float fLeft = 0, fRight = 0, fTop = 0, fBottom = 0, fWidth = 0, fHeight = 0;

      const bool bHasLeft = ParseLength(GetStyle(style, "left"), parentRect.width, local.m_fFontSize, fLeft).Succeeded();
      const bool bHasRight = ParseLength(GetStyle(style, "right"), parentRect.width, local.m_fFontSize, fRight).Succeeded();
      const bool bHasTop = ParseLength(GetStyle(style, "top"), parentRect.height, local.m_fFontSize, fTop).Succeeded();
      const bool bHasBottom = ParseLength(GetStyle(style, "bottom"), parentRect.height, local.m_fFontSize, fBottom).Succeeded();
      const bool bHasWidth = ParseLength(GetStyle(style, "width"), parentRect.width, local.m_fFontSize, fWidth).Succeeded();
      const bool bHasHeight = ParseLength(GetStyle(style, "height"), parentRect.height, local.m_fFontSize, fHeight).Succeeded();

      // percentages become anchors; px stay offsets
      auto isPercent = [&style](const char* szProp)
      { return GetStyle(style, szProp).EndsWith("%"); };

      float x0, x1, y0, y1;

      if (bHasLeft && bHasRight)
      {
        vAnchorMin.x = isPercent("left") ? fLeft / plMath::Max(parentRect.width, 1.0f) : 0.0f;
        vAnchorMax.x = isPercent("right") ? 1.0f - fRight / plMath::Max(parentRect.width, 1.0f) : 1.0f;
        x0 = parentRect.x + fLeft;
        x1 = parentRect.x + parentRect.width - fRight;
      }
      else if (bHasRight)
      {
        vAnchorMin.x = vAnchorMax.x = 1.0f;
        const float fW = bHasWidth ? fWidth : EstimateWidth(node, ctx, local, parentRect.width);
        x1 = parentRect.x + parentRect.width - fRight;
        x0 = x1 - fW;
      }
      else
      {
        vAnchorMin.x = vAnchorMax.x = 0.0f;
        const float fW = bHasWidth ? fWidth : EstimateWidth(node, ctx, local, parentRect.width);
        x0 = parentRect.x + (bHasLeft ? fLeft : 0.0f);
        x1 = x0 + fW;
      }

      if (bHasTop && bHasBottom)
      {
        vAnchorMin.y = isPercent("top") ? fTop / plMath::Max(parentRect.height, 1.0f) : 0.0f;
        vAnchorMax.y = isPercent("bottom") ? 1.0f - fBottom / plMath::Max(parentRect.height, 1.0f) : 1.0f;
        y0 = parentRect.y + fTop;
        y1 = parentRect.y + parentRect.height - fBottom;
      }
      else if (bHasBottom)
      {
        vAnchorMin.y = vAnchorMax.y = 1.0f;
        const float fH = bHasHeight ? fHeight : EstimateHeight(node, ctx, local, parentRect.width);
        y1 = parentRect.y + parentRect.height - fBottom;
        y0 = y1 - fH;
      }
      else
      {
        vAnchorMin.y = vAnchorMax.y = 0.0f;
        const float fH = bHasHeight ? fHeight : EstimateHeight(node, ctx, local, parentRect.width);
        y0 = parentRect.y + (bHasTop ? fTop : 0.0f);
        y1 = y0 + fH;
      }

      rect = plRectFloat(x0, y0, plMath::Max(x1 - x0, 1.0f), plMath::Max(y1 - y0, 1.0f));

      // anchors that stayed at a point keep px semantics; stretch axes need matching anchors
      if (vAnchorMin.x != vAnchorMax.x || vAnchorMin.y != vAnchorMax.y)
      {
        // fine - ComputePlacement handles mixed axes
      }

      pIr->m_vAnchorMin = vAnchorMin;
      pIr->m_vAnchorMax = vAnchorMax;
      pIr->m_vOffsets = plUiImportUtils::ComputePlacement(rect, parentRect, vAnchorMin, vAnchorMax);
    }
    else
    {
      // free placement without absolute positioning: top-left anchored, estimated size
      const float fW = EstimateWidth(node, ctx, local, parentRect.width);
      const float fH = EstimateHeight(node, ctx, local, parentRect.width);
      rect = plRectFloat(parentRect.x, parentRect.y, fW, fH);
      pIr->m_vAnchorMin = plVec2::MakeZero();
      pIr->m_vAnchorMax = plVec2::MakeZero();
      pIr->m_vOffsets = plVec4(0, 0, fW, fH);
    }

    const plStringView sTransform = GetStyle(style, "transform");
    if (!sTransform.IsEmpty() && sTransform != "none")
    {
      if (sTransform.StartsWith_NoCase("translate(") && sTransform.EndsWith(")"))
      {
        plStringBuilder sArgs = sTransform;
        sArgs.Shrink(10, 1);
        plHybridArray<plStringView, 2> values;
        sArgs.Split(false, values, ",");

        plVec3 translation = plVec3::MakeZero();
        if (!values.IsEmpty())
          ParseLength(values[0], rect.width, local.m_fFontSize, translation.x).IgnoreResult();
        if (values.GetCount() > 1)
          ParseLength(values[1], rect.height, local.m_fFontSize, translation.y).IgnoreResult();
        pIr->Set("TransformTranslation", translation);
      }
      else
      {
        ctx.m_pReport->Info(sPath, "CSS transform is not yet supported by the HTML importer; static layout is preserved.");
      }
    }

    if (GetStyle(style, "display") == "none")
      pIr->Set("Visible", false);

    // ————— per-type properties —————
    if (pType == plGetStaticRTTI<plUiText>())
    {
      pIr->Set("Text", node.m_sText.GetView());
      ApplyTextProperties(*pIr, style, local);
      return;
    }

    if (pType == plGetStaticRTTI<plUiImage>())
    {
      plStringView sSrc = node.GetAttribute("src");
      if (sSrc.IsEmpty())
      {
        ctx.m_pReport->Warning(sPath, "img without src imports as an empty Image.");
      }
      else if (sSrc.StartsWith_NoCase("http"))
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("Remote image '{}' is not downloaded; assign a texture manually.", sSrc);
        ctx.m_pReport->Warning(sPath, sMsg);
      }
      else
      {
        plStringBuilder sAbs = ctx.m_sHtmlFolderAbs;
        sAbs.AppendPath(sSrc);
        sAbs.MakeCleanPath();

        if (plOSFile::ExistsFile(sAbs))
        {
          pIr->m_sTextureFile = sAbs;
          pIr->Set("BrushMode", (plInt32)plUiBrushMode::Image);
        }
        else
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("Image file not found: '{}'.", sSrc);
          ctx.m_pReport->Warning(sPath, sMsg);
        }
      }

      ApplyVisualStyle(*pIr, style, ctx, sPath, local.m_fFontSize, false);
      return;
    }

    if (pType == plGetStaticRTTI<plUiButton>())
    {
      const bool bAnchor = sTag == "a";
      ApplyVisualStyle(*pIr, style, ctx, sPath, local.m_fFontSize, bAnchor);
      if (bAnchor)
      {
        pIr->Set("Color", plColorGammaUB(0, 0, 0, 0));
        pIr->Set("HoverColor", plColorGammaUB(0, 0, 0, 0));
        pIr->Set("PressedColor", plColorGammaUB(0, 0, 0, 0));
      }

      plStringView sId = node.GetAttribute("id");
      if (!sId.IsEmpty())
        pIr->Set("ClickEvent", sId);
      else if (bAnchor && !node.m_sText.IsEmpty())
      {
        plStringBuilder sEvent = node.m_sText;
        sEvent.Trim(" \t\r\n");
        pIr->Set("ClickEvent", sEvent.GetView());
      }

      if (!node.m_sText.IsEmpty())
      {
        // label alignment: the button's own text-align wins, browser default is centered
        plInt32 iLabelAlign = bAnchor ? plUiTextHAlign::Left : plUiTextHAlign::Center;
        const plStringView sAlign = GetStyle(style, "text-align");
        if (sAlign == "left")
          iLabelAlign = plUiTextHAlign::Left;
        else if (sAlign == "right")
          iLabelAlign = plUiTextHAlign::Right;

        plUiImportNode* pLabel = pIr->AddChild(plGetStaticRTTI<plUiText>(), "Label");
        pLabel->m_vAnchorMin = plVec2::MakeZero();
        pLabel->m_vAnchorMax = plVec2(1.0f, 1.0f);
        pLabel->m_vOffsets = plVec4::MakeZero();
        pLabel->Set("Text", node.m_sText.GetView());
        pLabel->Set("TextColor", local.m_TextColor);
        pLabel->Set("FontSize", static_cast<plInt32>(plMath::Clamp(local.m_fFontSize, 4.0f, 256.0f)));
        pLabel->Set("HAlign", iLabelAlign);
        pLabel->Set("VAlign", (plInt32)plUiTextVAlign::Center);
        pLabel->m_sFontFile = local.m_sFontFile;
      }

      // nested content (icons etc.)
      for (const plUiMarkupNode* pChild : node.m_Children)
      {
        if (IsIgnoredTag(pChild->m_sTag))
          continue;
        ImportElement(*pChild, ctx, local, *pIr, rect, false, sPath);
      }
      return;
    }

    if (pType == plGetStaticRTTI<plUiPath>())
    {
      pIr->Set("PathData", node.GetAttribute("d"));
      pIr->Set("Stretch", (plInt32)plUiPathStretch::Uniform);
      pIr->Set("Color", local.m_TextColor);
      return;
    }

    if (pType == plGetStaticRTTI<plUiProgressBar>())
    {
      double fValue = 0.5, fMax = 1.0;
      plConversionUtils::StringToFloat(node.GetAttribute("value", "0.5"), fValue).IgnoreResult();
      plConversionUtils::StringToFloat(node.GetAttribute("max", "1"), fMax).IgnoreResult();
      pIr->Set("FillAmount", static_cast<float>(fMax > 0.0 ? fValue / fMax : fValue));

      ApplyVisualStyle(*pIr, style, ctx, sPath, local.m_fFontSize, false);

      plColorGammaUB accent;
      if (plUiImportUtils::ParseColor(GetStyle(style, "accent-color"), false, accent).Succeeded())
        pIr->Set("FillColor", accent);
      return;
    }

    if (pType == plGetStaticRTTI<plUiToggle>())
    {
      ApplyVisualStyle(*pIr, style, ctx, sPath, local.m_fFontSize, false);
      if (node.FindAttribute("checked") != nullptr)
        ctx.m_pReport->Info(sPath, "'checked' has no authored equivalent (toggle state is runtime state); bind ClickEntryBinding instead.");
      return;
    }

    if (pType == plGetStaticRTTI<plUiSlider>())
    {
      double fMin = 0.0, fMax = 100.0, fValue = 50.0;
      plConversionUtils::StringToFloat(node.GetAttribute("min", "0"), fMin).IgnoreResult();
      plConversionUtils::StringToFloat(node.GetAttribute("max", "100"), fMax).IgnoreResult();
      plConversionUtils::StringToFloat(node.GetAttribute("value", "50"), fValue).IgnoreResult();
      pIr->Set("Min", static_cast<float>(fMin));
      pIr->Set("Max", static_cast<float>(fMax));
      pIr->Set("Value", static_cast<float>(fValue));
      ApplyVisualStyle(*pIr, style, ctx, sPath, local.m_fFontSize, false);
      return;
    }

    if (pType == plGetStaticRTTI<plUiTextInput>())
    {
      pIr->Set("Text", node.GetAttribute("value"));
      pIr->Set("Placeholder", node.GetAttribute("placeholder"));
      pIr->Set("TextColor", local.m_TextColor);
      pIr->Set("FontSize", static_cast<plInt32>(plMath::Clamp(local.m_fFontSize, 4.0f, 256.0f)));
      pIr->m_sFontFile = local.m_sFontFile;

      double fMaxLen = 0.0;
      if (plConversionUtils::StringToFloat(node.GetAttribute("maxlength"), fMaxLen).Succeeded())
        pIr->Set("MaxLength", static_cast<plInt32>(fMaxLen));

      ApplyVisualStyle(*pIr, style, ctx, sPath, local.m_fFontSize, false);
      return;
    }

    // ————— containers —————
    ApplyVisualStyle(*pIr, style, ctx, sPath, local.m_fFontSize, true);

    plVec4 vPadding = ParseBoxValues(style, "padding", local.m_fFontSize, rect.width, rect.height);

    if (bContainerIsStack)
    {
      const bool bFlex = GetStyle(style, "display") == "flex";
      const plStringView sFlexDir = GetStyle(style, "flex-direction", "column");
      const bool bHorizontal = bFlex && (sFlexDir == "row" || sFlexDir == "row-reverse");

      if (sFlexDir == "row-reverse" || sFlexDir == "column-reverse")
        ctx.m_pReport->Info(sPath, "Reversed flex direction imports in source order.");

      pIr->Set("Direction", bHorizontal ? (plInt32)plUiStackDirection::Horizontal : (plInt32)plUiStackDirection::Vertical);

      float fGap = 0.0f;
      ParseLength(GetStyle(style, "gap", "0"), bHorizontal ? rect.width : rect.height, local.m_fFontSize, fGap).IgnoreResult();
      pIr->Set("Spacing", fGap);
      pIr->Set("Padding", vPadding);

      if (bFlex)
      {
        const plStringView sJustify = GetStyle(style, "justify-content");
        const plStringView sAlignItems = GetStyle(style, "align-items");

        plInt32 iJustification = plUiStackJustification::Start;
        if (sJustify == "center")
          iJustification = plUiStackJustification::Center;
        else if (sJustify == "flex-end" || sJustify == "end")
          iJustification = plUiStackJustification::End;
        else if (sJustify == "space-between")
          iJustification = plUiStackJustification::SpaceBetween;
        else if (!sJustify.IsEmpty() && sJustify != "flex-start" && sJustify != "start")
          ctx.m_pReport->Info(sPath, "justify-content value is unsupported; children flow from the start.");
        pIr->Set("Justification", iJustification);

        plInt32 iAlignment = plUiStackAlignment::Stretch;
        if (sAlignItems == "center")
          iAlignment = plUiStackAlignment::Center;
        else if (sAlignItems == "flex-start" || sAlignItems == "start")
          iAlignment = plUiStackAlignment::Start;
        else if (sAlignItems == "flex-end" || sAlignItems == "end")
          iAlignment = plUiStackAlignment::End;
        else if (!sAlignItems.IsEmpty() && sAlignItems != "stretch")
          ctx.m_pReport->Info(sPath, "align-items value is unsupported; children stretch on the cross axis.");
        pIr->Set("Alignment", iAlignment);
      }

      const plRectFloat contentRect(rect.x + vPadding.x, rect.y + vPadding.y,
        plMath::Max(rect.width - vPadding.x - vPadding.z, 1.0f), plMath::Max(rect.height - vPadding.y - vPadding.w, 1.0f));

      ImportChildren(node, ctx, local, *pIr, contentRect, true, sPath);
    }
    else
    {
      const plRectFloat contentRect(rect.x + vPadding.x, rect.y + vPadding.y,
        plMath::Max(rect.width - vPadding.x - vPadding.z, 1.0f), plMath::Max(rect.height - vPadding.y - vPadding.w, 1.0f));

      ImportChildren(node, ctx, local, *pIr, contentRect, false, sPath);
    }
  }

  const plUiMarkupNode* FindFirstTag(const plUiMarkupNode& node, plStringView sTag)
  {
    if (node.m_sTag == sTag)
      return &node;

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      if (const plUiMarkupNode* pFound = FindFirstTag(*pChild, sTag))
        return pFound;
    }
    return nullptr;
  }

  void CollectCss(const plUiMarkupNode& node, HtmlImportContext& ctx, plStringView sSourceName)
  {
    if (node.m_sTag == "style")
    {
      ctx.m_Css.AddRulesFromText(node.m_sText.GetView(), sSourceName);
    }
    else if (node.m_sTag == "link" && node.GetAttribute("rel").IsEqual_NoCase("stylesheet"))
    {
      plStringView sHref = node.GetAttribute("href");
      if (sHref.StartsWith_NoCase("http"))
      {
        plStringBuilder sError;
        const plDynamicArray<plUInt8> bytes = ctx.m_bFetchRemote ? FetchHttpBytes(sHref, sError) : plDynamicArray<plUInt8>();
        if (bytes.IsEmpty())
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("Remote stylesheet '{}' could not be fetched: {}.", sHref, sError);
          ctx.m_pReport->Warning(sSourceName, sMsg);
        }
        else
        {
          const char* szCss = reinterpret_cast<const char*>(bytes.GetData());
          const plStringView sRemoteCss(szCss, szCss + bytes.GetCount());
          CollectRemoteFontFaces(ctx.m_Css, sRemoteCss, sHref, ctx.m_RemoteFontFaces);
          ctx.m_Css.AddRulesFromText(sRemoteCss, sHref);
        }
      }
      else if (!sHref.IsEmpty())
      {
        plStringBuilder sCssPath = ctx.m_sHtmlFolderAbs;
        sCssPath.AppendPath(sHref);
        sCssPath.MakeCleanPath();

        plFileReader file;
        if (file.Open(sCssPath).Succeeded())
        {
          plStringBuilder sContent;
          sContent.ReadAll(file);
          ctx.m_Css.AddRulesFromText(sContent.GetView(), sHref);
        }
        else
        {
          plStringBuilder sMsg;
          sMsg.SetFormat("Linked stylesheet not found: '{}'.", sHref);
          ctx.m_pReport->Warning(sSourceName, sMsg);
        }
      }
    }

    for (const plUiMarkupNode* pChild : node.m_Children)
    {
      CollectCss(*pChild, ctx, sSourceName);
    }
  }
} // namespace

plStatus plUiHtmlDocumentGenerator::Generate(plStringView sInputFileAbs, plStringView sMode, plDynamicArray<plDocument*>& out_generatedDocuments)
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

  plStringBuilder sSourceName = plPathUtils::GetFileNameAndExtension(sInputFileAbs);

  plUniquePtr<plUiMarkupNode> pRoot = plUiMarkupParser::Parse(sContent.GetView(), true, report, sSourceName);

  HtmlImportContext ctx;
  ctx.m_pReport = &report;
  ctx.m_Css.m_pReport = &report;
  ctx.m_bFetchRemote = true;
  {
    plStringBuilder sFolder = sInputFileAbs;
    sFolder.PathParentDirectory();
    ctx.m_sHtmlFolderAbs = sFolder;
  }

  CollectCss(*pRoot, ctx, sSourceName);

  const plUInt32 uiRemoteFonts = CacheRemoteFontFaces(ctx.m_RemoteFontFaces, ctx.m_sHtmlFolderAbs, report, sSourceName);
  if (uiRemoteFonts > 0)
  {
    plStringBuilder sMessage;
    sMessage.SetFormat("Cached {} remote font face(s) in ImportedFonts and assigned them to matching text.", uiRemoteFonts);
    report.Info(sSourceName, sMessage);
  }

  // A missing web font must never send imported text through the debug renderer. That
  // renderer uses a small bitmap alphabet and visibly pixelates when a 1920x1080
  // document is scaled. Copy the bundled Lato faces into the project and let the normal
  // UI Font asset pipeline bake them into scalable SDF atlases instead.
  ctx.m_FallbackFonts = CacheBundledFallbackFonts(ctx.m_sHtmlFolderAbs, report, sSourceName);

  const plUiMarkupNode* pBody = FindFirstTag(*pRoot, "body");
  if (pBody == nullptr)
    pBody = pRoot.Borrow();

  plUiImportDocument importDoc;
  {
    plStringBuilder sTarget = sInputFileAbs;
    sTarget.ChangeFileExtension("plUiDocumentAsset");
    importDoc.m_sTargetPathAbs = sTarget;
    importDoc.m_Root.m_sName = sSourceName;
  }

  const plRectFloat referenceRect(0, 0, importDoc.m_vReferenceSize.x, importDoc.m_vReferenceSize.y);
  report.Info(sSourceName, "Imported at a 1920x1080 reference size; px values map 1:1.");

  Inherited inherited;

  // the body imports as one full-screen container element
  {
    ComputedStyle bodyStyle;
    ctx.m_Css.Collect(*pBody, bodyStyle);
    ctx.UpdateInherited(bodyStyle, inherited);

    ImportElement(*pBody, ctx, inherited, importDoc.m_Root, referenceRect, false, "");

    if (!importDoc.m_Root.m_Children.IsEmpty())
    {
      // stretch the body element over the whole canvas
      plUiImportNode* pBodyIr = importDoc.m_Root.m_Children.PeekBack();
      pBodyIr->m_sName = "Body";
      pBodyIr->m_vAnchorMin = plVec2::MakeZero();
      pBodyIr->m_vAnchorMax = plVec2(1.0f, 1.0f);
      pBodyIr->m_vOffsets = plVec4::MakeZero();
      pBodyIr->m_bStackChild = false;
    }
  }

  if (ctx.m_bUsedFallbackFont)
    report.Info(sSourceName, "Unresolved web/system font faces were replaced with the bundled scalable UI fallback font.");

  plUiImportWriter writer(report);
  const plStatus status = writer.WriteDocument(importDoc, out_generatedDocuments);

  plStringBuilder sTitle;
  sTitle.SetFormat("HTML Import — {}", sSourceName);
  plUiImportShowReport(report, sTitle);

  if (status.Failed())
    return status;

  return plStatus(PL_SUCCESS);
}
