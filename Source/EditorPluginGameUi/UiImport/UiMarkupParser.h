#pragma once

#include <EditorPluginGameUi/UiImport/UiImportModel.h>

#include <Foundation/Types/UniquePtr.h>

/// \brief One parsed markup element (XML or lenient HTML). Owns its children.
///
/// Text content is gathered per element: direct character data of an element is
/// concatenated into m_sText (in source order, entity-decoded, whitespace-collapsed
/// in lenient mode). <style> / <script> raw content lands in m_sText verbatim.
class plUiMarkupNode
{
public:
  struct Attribute
  {
    plString m_sName;
    plString m_sValue;
  };

  plUiMarkupNode() = default;
  ~plUiMarkupNode()
  {
    for (plUiMarkupNode* pChild : m_Children)
    {
      PL_DEFAULT_DELETE(pChild);
    }
    m_Children.Clear();
  }

  plUiMarkupNode(const plUiMarkupNode&) = delete;
  void operator=(const plUiMarkupNode&) = delete;

  /// \brief Case-insensitive attribute lookup; returns nullptr when absent.
  const plString* FindAttribute(plStringView sName) const
  {
    for (const Attribute& attr : m_Attributes)
    {
      if (attr.m_sName.IsEqual_NoCase(sName))
        return &attr.m_sValue;
    }
    return nullptr;
  }

  plStringView GetAttribute(plStringView sName, plStringView sFallback = {}) const
  {
    const plString* pValue = FindAttribute(sName);
    return pValue != nullptr ? pValue->GetView() : sFallback;
  }

  /// \brief The tag with any namespace prefix removed ("x:Name" -> "Name").
  plStringView GetLocalTag() const
  {
    const char* szColon = m_sTag.FindSubString(":");
    return szColon != nullptr ? plStringView(szColon + 1, m_sTag.GetView().GetEndPointer()) : m_sTag.GetView();
  }

  plString m_sTag; ///< lowercased in lenient HTML mode, verbatim in XML mode
  plHybridArray<Attribute, 8> m_Attributes;
  plStringBuilder m_sText;
  plUiMarkupNode* m_pParent = nullptr; ///< non-owning; used by CSS descendant selectors
  plDynamicArray<plUiMarkupNode*> m_Children;
};

/// \brief Minimal pull parser shared by the HTML and XAML importers.
///
/// Two dialects (see Plan_ImportExport.html):
/// - strict XML: quoted attributes, case-sensitive tags, self-closing via '/>',
///   mismatched end tags are reported as errors
/// - lenient HTML: void elements (img, br, input, ...), unquoted attribute values,
///   case-insensitive (lowercased) tags, raw-text handling for <script>/<style>,
///   unmatched end tags recovered by popping the open-element stack
///
/// Comments, doctype, CDATA and processing instructions are handled in both modes.
/// Returns a synthetic ROOT node whose children are the top-level elements.
namespace plUiMarkupParser
{
  plUniquePtr<plUiMarkupNode> Parse(plStringView sContent, bool bLenientHtml, plUiImportReport& ref_report, plStringView sSourceName);
} // namespace plUiMarkupParser
