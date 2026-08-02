#pragma once

#include <EditorPluginGameUi/EditorPluginGameUiDLL.h>

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Color8UNorm.h>
#include <Foundation/Math/Rect.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Math/Vec4.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/Variant.h>

// ═══════════════════════════════════════════════════════════════════════════════════════
// Shared intermediate representation of the UI importers (Figma / HTML / NoesisGUI XAML).
//
// Each frontend converts its source into a plUiImportNode tree; plUiImportWriter turns
// that tree into a new .plUiDocumentAsset. Anything a frontend cannot express must leave
// a plUiImportReport entry — the importers NEVER silently drop content.
// See Documentation/GameUI/Plan_ImportExport.html.
// ═══════════════════════════════════════════════════════════════════════════════════════

struct plUiImportSeverity
{
  enum Enum
  {
    Info,    ///< an approximation the user should know about
    Warning, ///< content was dropped or substituted
    Error,   ///< a whole subtree / document could not be imported
  };
};

struct plUiImportReportEntry
{
  plUiImportSeverity::Enum m_Severity = plUiImportSeverity::Info;
  plString m_sSourcePath; ///< path of the source node (e.g. "body/div#hud/img")
  plString m_sMessage;
};

/// \brief Collected warnings + statistics of one import run. Shown in the report dialog
/// after the import and mirrored to plLog.
class plUiImportReport
{
public:
  void Add(plUiImportSeverity::Enum severity, plStringView sSourcePath, plStringView sMessage)
  {
    plUiImportReportEntry& e = m_Entries.ExpandAndGetRef();
    e.m_Severity = severity;
    e.m_sSourcePath = sSourcePath;
    e.m_sMessage = sMessage;
  }

  void Info(plStringView sSourcePath, plStringView sMessage) { Add(plUiImportSeverity::Info, sSourcePath, sMessage); }
  void Warning(plStringView sSourcePath, plStringView sMessage) { Add(plUiImportSeverity::Warning, sSourcePath, sMessage); }
  void Error(plStringView sSourcePath, plStringView sMessage) { Add(plUiImportSeverity::Error, sSourcePath, sMessage); }

  bool HasProblems() const
  {
    for (const plUiImportReportEntry& e : m_Entries)
    {
      if (e.m_Severity != plUiImportSeverity::Info)
        return true;
    }
    return false;
  }

  plDynamicArray<plUiImportReportEntry> m_Entries;

  // statistics for the summary line
  plUInt32 m_uiElements = 0;
  plUInt32 m_uiImages = 0;
  plUInt32 m_uiDocuments = 0;
};

/// \brief One property to set on the generated element ("Color", "Text", ...).
/// Enum values are passed as plInt32, resource references as strings (asset guid or path).
struct plUiImportProperty
{
  plString m_sName;
  plVariant m_Value;
};

/// \brief One element state to generate (plUiElementState); enum operands as plain ints.
/// Used for imported flow states, e.g. "visible while Ui_Active == 'Start'".
struct plUiImportState
{
  plString m_sName;
  plString m_sEntry;
  plInt32 m_iCondition = 0; ///< plUiStateCondition value
  double m_fValue = 0.0;
  plString m_sValueText;
  plInt32 m_iVisibility = 0; ///< plUiStateVisibility value
  plString m_sPlayClip;
  plString m_sExitClip;
  float m_fHideDelay = 0.0f;
};

struct plUiImportEventAction
{
  plInt32 m_iType = 0; ///< plUiEventActionType value
  plString m_sEntry;
  plVariant m_Value = 0;
  plString m_sTarget;
  plString m_sClip;
};

struct plUiImportEventReaction
{
  plString m_sEvent;
  plHybridArray<plUiImportEventAction, 2> m_Actions;
};

struct plUiImportAnimKey
{
  float m_fTime = 0.0f;
  float m_fValue = 0.0f;
  plColor m_Color = plColor::White;
  plInt32 m_iEasing = 0; ///< plUiAnimEasing value
};

struct plUiImportAnimTrack
{
  plString m_sElement;
  plInt32 m_iTarget = 0; ///< plUiAnimTarget value
  plHybridArray<plUiImportAnimKey, 4> m_Keys;
};

struct plUiImportAnimation
{
  plString m_sName;
  float m_fDuration = 0.8f;
  plString m_sPlayOnEvent;
  plString m_sEntryBinding;
  plInt32 m_iMode = 0;     ///< plUiAnimationMode value
  plInt32 m_iLoopMode = 0; ///< plUiClipLoopMode value
  plHybridArray<plUiImportAnimTrack, 4> m_Tracks;
};

/// \brief One element of the import IR. Owns its children.
///
/// Placement is stored in the engine's final anchor/offset form (see plUiElement):
/// frontends with absolute source rects convert via plUiImportUtils::ComputePlacement,
/// frontends with alignment semantics (XAML) fill the fields directly. Children of
/// stack-like containers set m_bStackChild instead — Offsets.z/w hold the preferred
/// size while an optional LayoutMargin property preserves asymmetric source margins.
class plUiImportNode
{
public:
  plUiImportNode() = default;
  ~plUiImportNode()
  {
    for (plUiImportNode* pChild : m_Children)
    {
      PL_DEFAULT_DELETE(pChild);
    }
    m_Children.Clear();
  }

  plUiImportNode(const plUiImportNode&) = delete;
  void operator=(const plUiImportNode&) = delete;

  plUiImportNode* AddChild(const plRTTI* pType, plStringView sName)
  {
    plUiImportNode* pChild = PL_DEFAULT_NEW(plUiImportNode);
    pChild->m_pType = pType;
    pChild->m_sName = sName;
    m_Children.PushBack(pChild);
    return pChild;
  }

  void Set(plStringView sProperty, const plVariant& value)
  {
    for (plUiImportProperty& prop : m_Properties)
    {
      if (prop.m_sName == sProperty)
      {
        prop.m_Value = value;
        return;
      }
    }

    plUiImportProperty& prop = m_Properties.ExpandAndGetRef();
    prop.m_sName = sProperty;
    prop.m_Value = value;
  }

  const plRTTI* m_pType = nullptr; ///< target plUiElement type; ignored on the root node
  plString m_sName;

  plVec2 m_vAnchorMin = plVec2::MakeZero();
  plVec2 m_vAnchorMax = plVec2::MakeZero();
  plVec4 m_vOffsets = plVec4(0.0f, 0.0f, 100.0f, 50.0f);
  bool m_bStackChild = false; ///< parent lays this out; Offsets.z/w are its preferred size

  plHybridArray<plUiImportProperty, 12> m_Properties;

  /// Absolute path of an image file to import as a Texture asset; the writer creates
  /// (or reuses) the asset and assigns its guid to m_sTextureProperty.
  plString m_sTextureFile;
  plString m_sTextureProperty = "Texture";

  /// Absolute path of a .ttf/.otf to import as a UI Font asset; assigned to "Font".
  plString m_sFontFile;

  /// States to generate on the element (see plUiImportState).
  plHybridArray<plUiImportState, 2> m_States;

  /// Parent-scoped reactions for imported plUiSubDocument placements.
  plHybridArray<plUiImportEventReaction, 2> m_Reactions;

  plDynamicArray<plUiImportNode*> m_Children;
};

/// \brief One output document of an import run: target path + the element tree
/// (m_Root's children become the document's top-level elements; m_Root itself is not emitted).
class plUiImportDocument
{
public:
  plString m_sTargetPathAbs; ///< absolute path of the .plUiDocumentAsset to create
  plVec2 m_vReferenceSize = plVec2(1920.0f, 1080.0f);
  plUiImportNode m_Root;
  plDynamicArray<plUiImportAnimation> m_Animations;
};

namespace plUiImportUtils
{
  /// \brief Converts an absolute child rect into the engine's offset convention for the
  /// given anchors (see plUiElement::ComputeRect):
  /// - point axis (min == max): Offsets.x/y = position relative to the anchor point, z/w = size
  /// - stretch axis: Offsets.x/y = inset from the min anchor, z/w = SIGNED inset from the max anchor
  plVec4 ComputePlacement(const plRectFloat& childRect, const plRectFloat& parentRect, const plVec2& vAnchorMin, const plVec2& vAnchorMax);

  /// \brief Parses a color in common web/XAML notations: #rgb, #rrggbb, #rrggbbaa (CSS),
  /// #aarrggbb (XAML, flagged via bAlphaFirst), rgb()/rgba(), and a table of common names.
  plResult ParseColor(plStringView sValue, bool bAlphaFirst, plColorGammaUB& out_color);

  /// \brief Decodes the standard XML/HTML entities (&amp; &lt; &gt; &quot; &apos; &#nn; &#xhh;) in-place.
  void DecodeEntities(plStringView sText, plStringBuilder& out_sDecoded);
} // namespace plUiImportUtils
