#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiImport/UiMarkupParser.h>

namespace
{
  bool IsVoidElement(plStringView sTag)
  {
    return sTag == "area" || sTag == "base" || sTag == "br" || sTag == "col" || sTag == "embed" ||
           sTag == "hr" || sTag == "img" || sTag == "input" || sTag == "link" || sTag == "meta" ||
           sTag == "source" || sTag == "track" || sTag == "wbr";
  }

  bool IsRawTextElement(plStringView sTag)
  {
    return sTag == "script" || sTag == "style";
  }

  bool IsNameChar(char c)
  {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == ':' || c == '.';
  }

  bool IsWhitespace(char c)
  {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }

  struct Parser
  {
    const char* m_szCur = nullptr;
    const char* m_szEnd = nullptr;
    bool m_bLenient = false;
    plUiImportReport* m_pReport = nullptr;
    plString m_sSourceName;

    bool AtEnd() const { return m_szCur >= m_szEnd; }

    void SkipWhitespace()
    {
      while (!AtEnd() && IsWhitespace(*m_szCur))
        ++m_szCur;
    }

    bool SkipUntil(const char* szNeedle)
    {
      const plUInt32 uiLen = plStringUtils::GetStringElementCount(szNeedle);
      while (m_szCur + uiLen <= m_szEnd)
      {
        if (plStringUtils::CompareN(m_szCur, szNeedle, uiLen) == 0)
        {
          m_szCur += uiLen;
          return true;
        }
        ++m_szCur;
      }
      m_szCur = m_szEnd;
      return false;
    }

    plStringView ReadName()
    {
      const char* szStart = m_szCur;
      while (!AtEnd() && IsNameChar(*m_szCur))
        ++m_szCur;
      return plStringView(szStart, m_szCur);
    }

    /// Parses the attribute list of a start tag; stops before '>' or '/>'.
    void ReadAttributes(plUiMarkupNode& ref_node)
    {
      while (true)
      {
        SkipWhitespace();

        if (AtEnd() || *m_szCur == '>' || *m_szCur == '/' || *m_szCur == '?')
          return;

        const plStringView sName = ReadName();
        if (sName.IsEmpty())
        {
          ++m_szCur; // stray character; skip to avoid an endless loop
          continue;
        }

        plUiMarkupNode::Attribute& attr = ref_node.m_Attributes.ExpandAndGetRef();
        attr.m_sName = sName;
        if (m_bLenient)
        {
          plStringBuilder sLower = sName;
          sLower.ToLower();
          attr.m_sName = sLower;
        }

        SkipWhitespace();

        if (AtEnd() || *m_szCur != '=')
          continue; // value-less attribute (e.g. "checked")

        ++m_szCur; // '='
        SkipWhitespace();

        if (AtEnd())
          return;

        plStringView sRawValue;

        if (*m_szCur == '"' || *m_szCur == '\'')
        {
          const char cQuote = *m_szCur;
          ++m_szCur;
          const char* szStart = m_szCur;
          while (!AtEnd() && *m_szCur != cQuote)
            ++m_szCur;
          sRawValue = plStringView(szStart, m_szCur);
          if (!AtEnd())
            ++m_szCur; // closing quote
        }
        else if (m_bLenient)
        {
          const char* szStart = m_szCur;
          while (!AtEnd() && !IsWhitespace(*m_szCur) && *m_szCur != '>' && *m_szCur != '/')
            ++m_szCur;
          sRawValue = plStringView(szStart, m_szCur);
        }
        else
        {
          m_pReport->Warning(m_sSourceName, "Unquoted attribute value in XML input.");
          continue;
        }

        plStringBuilder sDecoded;
        plUiImportUtils::DecodeEntities(sRawValue, sDecoded);
        attr.m_sValue = sDecoded;
      }
    }

    void AppendText(plUiMarkupNode& ref_node, plStringView sRaw)
    {
      plStringBuilder sDecoded;
      plUiImportUtils::DecodeEntities(sRaw, sDecoded);

      if (m_bLenient)
      {
        // collapse whitespace runs, drop pure-whitespace segments
        plStringBuilder sCollapsed;
        bool bLastWasSpace = true;
        for (auto it = sDecoded.GetIteratorFront(); it.IsValid(); ++it)
        {
          const plUInt32 c = it.GetCharacter();
          if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
          {
            if (!bLastWasSpace)
              sCollapsed.Append(" ");
            bLastWasSpace = true;
          }
          else
          {
            sCollapsed.Append(c);
            bLastWasSpace = false;
          }
        }
        sCollapsed.Trim(" ");

        if (sCollapsed.IsEmpty())
          return;

        if (!ref_node.m_sText.IsEmpty() && !ref_node.m_sText.EndsWith("\n"))
          ref_node.m_sText.Append(" ");
        ref_node.m_sText.Append(sCollapsed.GetView());
      }
      else
      {
        ref_node.m_sText.Append(sDecoded.GetView());
      }
    }

    void Run(plUiMarkupNode& ref_root)
    {
      plHybridArray<plUiMarkupNode*, 16> stack;
      stack.PushBack(&ref_root);

      while (!AtEnd())
      {
        if (*m_szCur != '<')
        {
          const char* szStart = m_szCur;
          while (!AtEnd() && *m_szCur != '<')
            ++m_szCur;
          AppendText(*stack.PeekBack(), plStringView(szStart, m_szCur));
          continue;
        }

        // '<' ...
        if (m_szCur + 4 <= m_szEnd && plStringUtils::CompareN(m_szCur, "<!--", 4) == 0)
        {
          m_szCur += 4;
          SkipUntil("-->");
          continue;
        }

        if (m_szCur + 9 <= m_szEnd && plStringUtils::CompareN(m_szCur, "<![CDATA[", 9) == 0)
        {
          m_szCur += 9;
          const char* szStart = m_szCur;
          const char* szTextEnd = m_szCur;
          if (SkipUntil("]]>"))
            szTextEnd = m_szCur - 3;
          else
            szTextEnd = m_szEnd;
          stack.PeekBack()->m_sText.Append(plStringView(szStart, szTextEnd));
          continue;
        }

        if (m_szCur + 2 <= m_szEnd && (m_szCur[1] == '!' || m_szCur[1] == '?'))
        {
          // doctype / processing instruction
          ++m_szCur;
          SkipUntil(">");
          continue;
        }

        if (m_szCur + 2 <= m_szEnd && m_szCur[1] == '/')
        {
          // end tag
          m_szCur += 2;
          plStringBuilder sTag = ReadName();
          if (m_bLenient)
            sTag.ToLower();
          SkipUntil(">");

          // find the matching element on the stack (never pop the synthetic root)
          plInt32 iMatch = -1;
          for (plInt32 i = static_cast<plInt32>(stack.GetCount()) - 1; i >= 1; --i)
          {
            if (stack[i]->m_sTag == sTag.GetView())
            {
              iMatch = i;
              break;
            }
          }

          if (iMatch < 0)
          {
            if (!m_bLenient)
            {
              plStringBuilder sMsg;
              sMsg.SetFormat("Mismatched end tag </{}> ignored.", sTag);
              m_pReport->Warning(m_sSourceName, sMsg);
            }
            continue;
          }

          if (!m_bLenient && iMatch != static_cast<plInt32>(stack.GetCount()) - 1)
          {
            plStringBuilder sMsg;
            sMsg.SetFormat("End tag </{}> closes {} unclosed inner element(s).", sTag, stack.GetCount() - 1 - iMatch);
            m_pReport->Warning(m_sSourceName, sMsg);
          }

          stack.SetCount(static_cast<plUInt32>(iMatch));
          continue;
        }

        // start tag
        ++m_szCur; // '<'
        plStringBuilder sTag = ReadName();
        if (sTag.IsEmpty())
        {
          // stray '<' in text
          AppendText(*stack.PeekBack(), "<");
          continue;
        }

        if (m_bLenient)
          sTag.ToLower();

        plUiMarkupNode* pNode = PL_DEFAULT_NEW(plUiMarkupNode);
        pNode->m_sTag = sTag;
        pNode->m_pParent = stack.PeekBack();
        stack.PeekBack()->m_Children.PushBack(pNode);

        ReadAttributes(*pNode);

        bool bSelfClosing = false;
        SkipWhitespace();
        if (!AtEnd() && *m_szCur == '/')
        {
          bSelfClosing = true;
          ++m_szCur;
        }
        if (!AtEnd() && *m_szCur == '?') // xml declaration remnant safety
          ++m_szCur;
        if (!AtEnd() && *m_szCur == '>')
          ++m_szCur;

        if (m_bLenient && sTag == "br")
        {
          // Preserve an explicit HTML line break in the parent's gathered direct text.
          // Without this, "NULL<br>SECTOR" became "NULL SECTOR" during import.
          stack.PeekBack()->m_sText.Append("\n");
        }

        if (bSelfClosing || (m_bLenient && IsVoidElement(sTag)))
          continue;

        if (m_bLenient && IsRawTextElement(sTag))
        {
          // raw text until the matching close tag
          plStringBuilder sClose;
          sClose.SetFormat("</{}", sTag);

          const char* szStart = m_szCur;
          const char* szTextEnd = m_szEnd;

          while (m_szCur < m_szEnd)
          {
            if (*m_szCur == '<' && m_szCur + sClose.GetElementCount() <= m_szEnd &&
                plStringUtils::CompareN_NoCase(m_szCur, sClose.GetData(), sClose.GetElementCount()) == 0)
            {
              szTextEnd = m_szCur;
              m_szCur += sClose.GetElementCount();
              SkipUntil(">");
              break;
            }
            ++m_szCur;
          }

          pNode->m_sText.Append(plStringView(szStart, szTextEnd));
          continue;
        }

        stack.PushBack(pNode);
      }

      if (stack.GetCount() > 1 && !m_bLenient)
      {
        plStringBuilder sMsg;
        sMsg.SetFormat("{} element(s) were not closed at end of input.", stack.GetCount() - 1);
        m_pReport->Warning(m_sSourceName, sMsg);
      }
    }
  };
} // namespace

plUniquePtr<plUiMarkupNode> plUiMarkupParser::Parse(plStringView sContent, bool bLenientHtml, plUiImportReport& ref_report, plStringView sSourceName)
{
  plUniquePtr<plUiMarkupNode> pRoot = PL_DEFAULT_NEW(plUiMarkupNode);
  pRoot->m_sTag = "#root";

  // skip a UTF-8 BOM. NOTE: byte-based, NOT plStringView::Shrink - the BOM is ONE character
  // (U+FEFF), so Shrink(3, 0) would eat the BOM plus the first two real characters.
  if (sContent.GetElementCount() >= 3)
  {
    const plUInt8* szBytes = reinterpret_cast<const plUInt8*>(sContent.GetStartPointer());
    if (szBytes[0] == 0xEF && szBytes[1] == 0xBB && szBytes[2] == 0xBF)
      sContent = plStringView(sContent.GetStartPointer() + 3, sContent.GetEndPointer());
  }

  Parser parser;
  parser.m_szCur = sContent.GetStartPointer();
  parser.m_szEnd = sContent.GetEndPointer();
  parser.m_bLenient = bLenientHtml;
  parser.m_pReport = &ref_report;
  parser.m_sSourceName = sSourceName;
  parser.Run(*pRoot);

  return pRoot;
}
