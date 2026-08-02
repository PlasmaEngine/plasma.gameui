#pragma once

#include <GameUiPlugin/GameUiPluginDLL.h>

#include <Foundation/Math/Rect.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Types/Variant.h>
#include <GameUiPlugin/Resources/UiFontResource.h>
#include <RendererCore/Textures/Texture2DResource.h>

using plUiStyleResourceHandle = plTypedResourceHandle<class plUiStyleResource>;
using plUiDocumentResourceHandle = plTypedResourceHandle<class plUiDocumentResource>;

/// \brief How a plUiVisualElement fills its rect.
struct PL_GAMEUIPLUGIN_DLL plUiBrushMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    Solid,     ///< Fill with the color only.
    Image,     ///< Stretch the texture over the rect, tinted by the color.
    NineSlice, ///< Split the texture by the slice margins; corners stay fixed, edges/center stretch.

    Default = Solid
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiBrushMode);

/// \brief Two-color linear gradient of a brush.
struct PL_GAMEUIPLUGIN_DLL plUiGradientMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    None,       ///< flat Color
    Horizontal, ///< Color (left) -> GradientColor (right)
    Vertical,   ///< Color (top) -> GradientColor (bottom)

    Default = None
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiGradientMode);

struct PL_GAMEUIPLUGIN_DLL plUiTextHAlign
{
  using StorageType = plUInt8;

  enum Enum
  {
    Left,
    Center,
    Right,

    Default = Left
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiTextHAlign);

struct PL_GAMEUIPLUGIN_DLL plUiTextVAlign
{
  using StorageType = plUInt8;

  enum Enum
  {
    Top,
    Center,
    Bottom,

    Default = Top
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiTextVAlign);

/// \brief What happens to text that doesn't fit its element rect.
struct PL_GAMEUIPLUGIN_DLL plUiTextOverflow
{
  using StorageType = plUInt8;

  enum Enum
  {
    Overflow, ///< draw past the rect (previous behavior)
    Clip,     ///< scissor glyphs to the rect
    Ellipsis, ///< truncate the last fitting line with "..."

    Default = Overflow
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiTextOverflow);

/// \brief How a plUiAnimationClip's blackboard entry drives its playback.
struct PL_GAMEUIPLUGIN_DLL plUiAnimationMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    Trigger, ///< bool entry: plays forward while true, backward while false
    Value,   ///< numeric entry: the clamped 0..1 value IS the playback position (0.5 = halfway)

    Default = Trigger
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiAnimationMode);

struct PL_GAMEUIPLUGIN_DLL plUiAnimEasing
{
  using StorageType = plUInt8;

  enum Enum
  {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,

    Default = EaseInOut
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiAnimEasing);

/// \brief How a plUiElementState compares its blackboard entry.
struct PL_GAMEUIPLUGIN_DLL plUiStateCondition
{
  using StorageType = plUInt8;

  enum Enum
  {
    IsTrue,  ///< entry converts to true
    IsFalse, ///< entry converts to false
    Equal,   ///< entry == Value
    NotEqual,
    Less, ///< entry < Value
    LessEqual,
    Greater, ///< entry > Value
    GreaterEqual,

    Default = IsTrue
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiStateCondition);

/// \brief What an active plUiElementState does to its element's visibility.
struct PL_GAMEUIPLUGIN_DLL plUiStateVisibility
{
  using StorageType = plUInt8;

  enum Enum
  {
    NoChange, ///< visibility stays authored / binding-driven
    Show,     ///< element is visible while the state is active (even when authored invisible)
    Hide,     ///< element is hidden while the state is active

    Default = NoChange
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiStateVisibility);

/// \brief An action executed by a parent document when an event is raised inside one of
/// its plUiSubDocument placements.
struct PL_GAMEUIPLUGIN_DLL plUiEventActionType
{
  using StorageType = plUInt8;

  enum Enum
  {
    SetState,         ///< Set Entry to Value on the canvas blackboard stack.
    Show,             ///< Force the named parent-document element visible.
    Hide,             ///< Force the named parent-document element hidden.
    ToggleVisibility, ///< Toggle the named parent-document element's runtime visibility.
    PlayClip,         ///< Play the named clip in the parent document instance.

    Default = SetState
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiEventActionType);

/// \brief One action in a plUiEventReaction.
class PL_GAMEUIPLUGIN_DLL plUiEventAction : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiEventAction, plReflectedClass);

public:
  plEnum<plUiEventActionType> m_Type;
  plString m_sEntry;     ///< SetState target entry.
  plVariant m_Value = 0; ///< SetState value.
  plString m_sTarget;    ///< Show/Hide/Toggle parent element name.
  plString m_sClip;      ///< PlayClip parent clip name.
};

/// \brief A child event and the parent-scoped actions it invokes.
class PL_GAMEUIPLUGIN_DLL plUiEventReaction : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiEventReaction, plReflectedClass);

public:
  plUiEventReaction();
  ~plUiEventReaction();

  plString m_sEvent;
  plHybridArray<plUiEventAction*, 2> m_Actions;
};

/// \brief One state of an element's state machine: a condition over a blackboard entry
/// (e.g. "Health < 20") plus what happens while it is active.
///
/// States are evaluated in array order every frame; the FIRST state whose condition holds
/// is active. An active state can do two things:
/// - Style: the referenced UI Style cross-fades over the element's look during
///   EnterDuration (Pulse ping-pongs the blend for attention effects)
/// - PlayClip: the named animation clip of the document plays forward while the state is
///   active and backward once it ends (same rules as an entry-Trigger clip)
class PL_GAMEUIPLUGIN_DLL plUiElementState : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiElementState, plReflectedClass);

public:
  plUiElementState();
  ~plUiElementState();

  plString m_sName;
  plString m_sEntry; ///< blackboard entry evaluated by the condition
  plEnum<plUiStateCondition> m_Condition;
  double m_fValue = 0.0; ///< comparison operand for the numeric conditions

  /// String operand for Equal/NotEqual: when non-empty, the entry compares as a STRING
  /// against this text instead of numerically. Enables flow states like
  /// "Ui_Active == 'Start'" or "Platform == 'XBOX'".
  plString m_sValueText;

  /// While the state is active, Show/Hide overrides the element's visibility (beats both
  /// the authored Visible flag and VisibleBinding; the layout slot is kept). The switch is
  /// instant - EnterDuration only affects the style blend. UI FLOW: combine an authored
  /// Visible=false element with a Show state to reveal it on a data condition (focused
  /// tooltip, platform-specific prompts, wizard pages).
  plEnum<plUiStateVisibility> m_Visibility;

  plUiStyleResourceHandle m_hStyle; ///< the look while active; empty = no visual change
  plString m_sPlayClip;             ///< animation clip driven by this state; empty = none
  plString m_sExitClip;             ///< optional clip played forward when the state stops matching

  /// For a Hide state, keep rendering for this many seconds after the state activates.
  /// Interaction is disabled immediately. This allows an outgoing page to finish its
  /// exit animation before its parent sub-document placement disappears.
  float m_fHideDelay = 0.0f;

  float m_fEnterDuration = 0.2f; ///< seconds to blend the style in/out
  plEnum<plUiAnimEasing> m_Easing;
  bool m_bPulse = false;       ///< ping-pong the style blend while active
  float m_fPulsePeriod = 1.1f; ///< seconds for one full pulse cycle
};

/// \brief Base class of all UI widgets in a UI Document.
///
/// Placement uses an anchor/offset rect transform:
/// - m_vAnchorMin / m_vAnchorMax are fractions (0..1) of the parent rect.
/// - m_vOffsets (x=left, y=top, z=right, w=bottom) are pixel offsets applied to the anchor points.
/// - If an anchor is a point on an axis (min == max), the z/w components are interpreted as
///   width/height instead of a right/bottom offset.
/// - m_vLayoutMargin is consumed by container layouts (left/top/right/bottom); it does not
///   alter free anchor placement.
/// - authored transforms affect this element and its entire subtree after layout.
class PL_GAMEUIPLUGIN_DLL plUiElement : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiElement, plReflectedClass);

public:
  plUiElement();
  ~plUiElement();

  /// \brief Deletes all child elements.
  void ClearChildren();

  /// \brief Computes this element's rect in canvas space, given the parent rect.
  plRectFloat ComputeRect(const plRectFloat& parentRect) const;

  plString m_sName;
  bool m_bVisible = true;

  /// Opacity of THIS element's own visuals only — it does not propagate to children,
  /// so a fully transparent container can still show its contents. (Visible, in
  /// contrast, hides the whole subtree.)
  float m_fOpacity = 1.0f;

  /// Name of a blackboard entry (bool-convertible) that drives visibility at runtime.
  /// Unlike the authored Visible flag, binding-driven hiding keeps the element's layout
  /// slot inside containers (visibility:hidden semantics).
  plString m_sVisibleBinding;

  /// The element's UI Style. When it resolves, ALL visual values come from the style and
  /// the element's own visual properties are ignored ("style replaces visuals").
  /// Layout/hit-testing are never styled. Empty = the element's own properties.
  plUiStyleResourceHandle m_hStyle;

  /// Editor-only: locked elements (and their subtree) are not clickable/draggable in the
  /// canvas editor — the hierarchy panel's lock column. Ignored at runtime.
  bool m_bEditorLocked = false;

  // ————— gamepad/keyboard navigation (interactive elements) —————
  // explicit nav targets by element NAME override the geometric best-candidate search;
  // DefaultFocus marks where focus starts when nav input arrives with nothing focused
  bool m_bDefaultFocus = false;
  plString m_sNavUp;
  plString m_sNavDown;
  plString m_sNavLeft;
  plString m_sNavRight;

  /// The element's state machine: evaluated in order, first matching condition wins
  /// (see plUiElementState). Owned.
  plHybridArray<plUiElementState*, 1> m_States;

  // new elements spawn as a centered 200x50 box (point anchor), NOT fullscreen
  plVec2 m_vAnchorMin = plVec2(0.5f, 0.5f);
  plVec2 m_vAnchorMax = plVec2(0.5f, 0.5f);
  plVec4 m_vOffsets = plVec4(-100.0f, -25.0f, 200.0f, 50.0f);
  plVec4 m_vLayoutMargin = plVec4::MakeZero();

  /// Static post-layout transform applied to this element's complete subtree. Origin is
  /// normalized within the element rect; center and translation are canvas pixels,
  /// rotation is degrees, and perspective is the focal distance in canvas pixels.
  plVec2 m_vTransformOrigin = plVec2(0.5f);
  plVec3 m_vTransformCenter = plVec3::MakeZero();
  plVec3 m_vTransformTranslation = plVec3::MakeZero();
  plVec3 m_vTransformRotation = plVec3::MakeZero();
  plVec3 m_vTransformScale = plVec3(1.0f);
  float m_fTransformPerspective = 1000.0f;

  plHybridArray<plUiElement*, 4> m_Children;
};

/// \brief Base class for widgets that paint a brush (color / texture / nine-slice) into their rect.
class PL_GAMEUIPLUGIN_DLL plUiVisualElement : public plUiElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiVisualElement, plUiElement);

public:
  plUiVisualElement();
  ~plUiVisualElement();

  plColorGammaUB m_Color = plColorGammaUB(255, 255, 255, 255);
  plEnum<plUiBrushMode> m_BrushMode;
  plTexture2DResourceHandle m_hTexture;
  plVec4 m_vSliceMargins = plVec4(16.0f); ///< Nine-slice margins in texture pixels (left/top/right/bottom).

  /// Clips the CHILDREN to this element's rect (the own brush stays unclipped, so
  /// nine-slice borders remain intact). Also limits where children are clickable.
  bool m_bClipChildren = false;

  /// Corner radii in px (top-left, top-right, bottom-right, bottom-left). Applies to
  /// Solid/Image brushes and to progress/slider fills; NineSlice keeps its texture shape.
  plVec4 m_vCornerRadius = plVec4::MakeZero();

  /// Outline hugging the (rounded) shape; 0 = off.
  float m_fBorderWidth = 0.0f;
  plColorGammaUB m_BorderColor = plColorGammaUB(255, 255, 255, 255);

  /// Two-color linear gradient: the brush Color blends toward GradientColor.
  plEnum<plUiGradientMode> m_GradientMode;
  plColorGammaUB m_GradientColor = plColorGammaUB(128, 128, 128, 255);

  /// Soft drop shadow behind the brush; 0 = off. Size is the blur/extend distance in px.
  float m_fShadowSize = 0.0f;
  plVec2 m_vShadowOffset = plVec2(0.0f, 4.0f);
  plColorGammaUB m_ShadowColor = plColorGammaUB(0, 0, 0, 150);
};

/// \brief A container widget with an optional background brush. The root of every UI Document is a panel.
class PL_GAMEUIPLUGIN_DLL plUiPanel : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiPanel, plUiVisualElement);

public:
  plUiPanel();
  ~plUiPanel();
};

struct PL_GAMEUIPLUGIN_DLL plUiStackDirection
{
  using StorageType = plUInt8;

  enum Enum
  {
    Vertical,
    Horizontal,

    Default = Vertical
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiStackDirection);

struct PL_GAMEUIPLUGIN_DLL plUiStackJustification
{
  using StorageType = plUInt8;

  enum Enum
  {
    Start,
    Center,
    End,
    SpaceBetween,

    Default = Start
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiStackJustification);

struct PL_GAMEUIPLUGIN_DLL plUiStackAlignment
{
  using StorageType = plUInt8;

  enum Enum
  {
    Stretch,
    Start,
    Center,
    End,

    Default = Stretch
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiStackAlignment);

/// \brief Lays its children out sequentially along one axis.
///
/// Layout contract (see plUiLayout): inside this container a child's anchors and
/// Offsets.x/y are ignored; Offsets.z/w act as the child's preferred width/height on
/// the main axis, the cross axis stretches to the padded content rect.
class PL_GAMEUIPLUGIN_DLL plUiStackPanel : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiStackPanel, plUiVisualElement);

public:
  plUiStackPanel();
  ~plUiStackPanel();

  plEnum<plUiStackDirection> m_Direction;
  plEnum<plUiStackJustification> m_Justification;
  plEnum<plUiStackAlignment> m_Alignment;
  float m_fSpacing = 4.0f;
  plVec4 m_vPadding = plVec4::MakeZero(); ///< left/top/right/bottom px
};

/// \brief A stack panel whose content scrolls along its Direction (mouse wheel).
///
/// Always clips its children to its rect. The scroll offset is per-canvas-instance
/// runtime state (like hover/pressed), NOT part of the document — the shared tree stays
/// immutable. A minimal scrollbar (track + thumb) renders while the content overflows.
class PL_GAMEUIPLUGIN_DLL plUiScrollView : public plUiStackPanel
{
  PL_ADD_DYNAMIC_REFLECTION(plUiScrollView, plUiStackPanel);

public:
  plUiScrollView();
  ~plUiScrollView();

  float m_fScrollSpeed = 40.0f; ///< px per mouse-wheel tick
  bool m_bShowScrollbar = true;
};

/// \brief Lays its children out row-major into uniform cells.
///
/// Layout contract (see plUiLayout): cell width derives from the column count, cell
/// height is fixed; children stretch to fill their cell (their own rect transform is
/// ignored). Rows grow downward and may overflow the panel.
class PL_GAMEUIPLUGIN_DLL plUiGridPanel : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiGridPanel, plUiVisualElement);

public:
  plUiGridPanel();
  ~plUiGridPanel();

  plUInt32 m_uiColumns = 2;
  float m_fRowHeight = 50.0f;
  plVec2 m_vSpacing = plVec2(4.0f);       ///< x = column gap, y = row gap
  plVec4 m_vPadding = plVec4::MakeZero(); ///< left/top/right/bottom px
};

/// \brief A widget that displays a texture (stretched or nine-sliced).
class PL_GAMEUIPLUGIN_DLL plUiImage : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiImage, plUiVisualElement);

public:
  plUiImage();
  ~plUiImage();
};

/// \brief What a plUiButton click writes into its bound blackboard entry.
struct PL_GAMEUIPLUGIN_DLL plUiClickEntryAction
{
  using StorageType = plUInt8;

  enum Enum
  {
    Toggle,
    SetTrue,
    SetFalse,

    Default = Toggle
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiClickEntryAction);

/// \brief A clickable button.
///
/// Reacts to pointer input while the world is simulated: the brush color switches to
/// HoverColor / PressedColor. A click can do both of:
/// - raise a plMsgUiEvent with ClickEvent as identifier on the owning canvas component
/// - write to a blackboard entry (ClickEntryBinding + ClickEntryAction), so other
///   bound elements react without any game code
class PL_GAMEUIPLUGIN_DLL plUiButton : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiButton, plUiVisualElement);

public:
  plUiButton();
  ~plUiButton();

  plColorGammaUB m_HoverColor = plColorGammaUB(230, 230, 230, 255);
  plColorGammaUB m_PressedColor = plColorGammaUB(170, 170, 170, 255);
  plTexture2DResourceHandle m_hHoverTexture;   ///< optional; falls back to the base Texture
  plTexture2DResourceHandle m_hPressedTexture; ///< optional; falls back to the base Texture
  plString m_sClickEvent;
  plString m_sClickEntryBinding; ///< blackboard entry written on click
  plEnum<plUiClickEntryAction> m_ClickEntryAction;

  /// Name of a bool blackboard entry: while true the button is non-interactive and
  /// renders in the style class's Disabled visuals (grey fallback without a class).
  plString m_sDisabledBinding;
};

/// \brief A button with an on/off state.
///
/// The checked state is the bool value of ClickEntryBinding's entry (a click already
/// toggles it; radio groups fall out of SetTrue/SetFalse actions on a shared entry).
/// Without a binding the state is per-canvas-instance runtime state, flipped on click.
/// Visual precedence: Pressed > Hover > Checked > Normal.
class PL_GAMEUIPLUGIN_DLL plUiToggle : public plUiButton
{
  PL_ADD_DYNAMIC_REFLECTION(plUiToggle, plUiButton);

public:
  plUiToggle();
  ~plUiToggle();

  plColorGammaUB m_CheckedColor = plColorGammaUB(120, 190, 120, 255);
  plTexture2DResourceHandle m_hCheckedTexture; ///< optional; falls back to the base Texture
};

/// \brief A draggable value slider.
///
/// The track uses the inherited brush, the fill covers up to the value (progress-bar
/// style) and a square handle sits at the value position. Dragging writes ValueBinding's
/// entry (two-way); without a binding the value is per-canvas-instance runtime state.
class PL_GAMEUIPLUGIN_DLL plUiSlider : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiSlider, plUiVisualElement);

public:
  plUiSlider();
  ~plUiSlider();

  float m_fMin = 0.0f;
  float m_fMax = 1.0f;
  float m_fValue = 0.5f; ///< authored value in [Min, Max]; used while unbound

  /// Numeric blackboard entry the slider reads AND writes (drag). Read remaps
  /// [Min, Max] -> fill fraction like a progress bar binding.
  plString m_sValueBinding;

  plColorGammaUB m_FillColor = plColorGammaUB(80, 140, 220, 255);
  plColorGammaUB m_HandleColor = plColorGammaUB(240, 240, 240, 255);
  plTexture2DResourceHandle m_hHandleTexture; ///< optional; solid quad otherwise
};

/// \brief A read-only progress bar: the background uses the inherited brush, the fill is a
/// solid quad covering FillAmount of the rect from the left.
class PL_GAMEUIPLUGIN_DLL plUiProgressBar : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiProgressBar, plUiVisualElement);

public:
  plUiProgressBar();
  ~plUiProgressBar();

  float m_fFillAmount = 0.5f;
  plColorGammaUB m_FillColor = plColorGammaUB(220, 80, 70, 255);
  plTexture2DResourceHandle m_hFillTexture; ///< optional; the fill crops the texture horizontally (no squashing)

  /// Name of a numeric blackboard entry that drives the fill; the value is remapped
  /// from [BindingMin, BindingMax] to [0, 1]. Falls back to FillAmount when unbound.
  plString m_sFillAmountBinding;
  float m_fBindingMin = 0.0f;
  float m_fBindingMax = 1.0f;
};

/// \brief Instances another UI Document inside this element's rect — the building block
/// for USER-CREATED widgets: author a reusable widget as a normal UI Document, then place
/// it anywhere via a Sub-Document element.
///
/// The referenced document's root is laid out against this element's rect (a stretching
/// root fills it). Rendering, hit-testing and binding discovery all descend (recursion
/// cap 8); widget runtime state (hover, toggles, sliders, scrolling) is keyed per
/// INSTANCE via plUiElementInstance salts, so every placement acts independently.
class PL_GAMEUIPLUGIN_DLL plUiSubDocument : public plUiElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiSubDocument, plUiElement);

public:
  plUiSubDocument();
  ~plUiSubDocument();

  plUiDocumentResourceHandle m_hDocument;

  /// Events raised anywhere inside this placed document can execute actions in the
  /// containing parent document. The nearest matching placement consumes parent routing;
  /// the normal plMsgUiEvent is still emitted to gameplay.
  plHybridArray<plUiEventReaction*, 2> m_Reactions;
};

/// \brief A data-driven, virtualized list: instantiates its Item Template (a UI Document /
/// user widget) once per item of a blackboard entry.
///
/// Data contract — the ItemsBinding entry holds one of:
///  - a plVariantArray: one item per element; plVariantDictionary items expose their keys
///    as the template's binding entries (template binds "Name" -> item shows dict["Name"]),
///    non-dictionary items are exposed under the entry name "Item";
///  - a plain NUMBER: that many placeholder items (authored template defaults show) —
///    also the editor mock-preview path.
///
/// Game code just writes the data: build the array, pBoard->SetEntryValue("Inventory", items).
/// Items flow along the inherited stack Direction with ItemExtent px each (cross axis
/// stretches); scrolling/clipping inherited from plUiScrollView; only visible items are
/// drawn and hit-tested. Clicks inside item i raise plMsgUiEvent with m_iItemIndex == i
/// and (when set) write the index to the SelectedBinding entry.
/// Limitation: item templates must not contain further Item Lists.
class PL_GAMEUIPLUGIN_DLL plUiItemList : public plUiScrollView
{
  PL_ADD_DYNAMIC_REFLECTION(plUiItemList, plUiScrollView);

public:
  plUiItemList();
  ~plUiItemList();

  plUiDocumentResourceHandle m_hItemTemplate;
  plString m_sItemsBinding;    ///< blackboard entry holding the item data (see class docs)
  float m_fItemExtent = 60.0f; ///< main-axis size of one item in px
  plString m_sSelectedBinding; ///< optional: the clicked item's index is written here
};

/// \brief How a plUiPath maps its path-space geometry into the element rect.
struct PL_GAMEUIPLUGIN_DLL plUiPathStretch
{
  using StorageType = plUInt8;

  enum Enum
  {
    Uniform, ///< fit inside the rect preserving aspect ratio, centered
    Fill,    ///< stretch to the rect (aspect may distort)
    None,    ///< path units are canvas px, anchored at the rect's top-left

    Default = Uniform
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiPathStretch);

/// \brief A filled VECTOR SHAPE defined by SVG/XAML path data ("M 0,0 L 10,0 ... Z").
///
/// Renders resolution-independent triangulated geometry tinted with the inherited brush
/// Color (hover/pressed/state style colors apply like any visual element). Supported
/// commands: M/L/H/V/C/S/Q/T/A/Z (absolute and relative); curves and arcs are flattened.
/// Each closed subpath fills independently — even-odd holes are NOT supported (a ring
/// renders as a disc), and strokes/gradients on the path are not supported.
/// Tessellation happens once per element and is shared by all instances.
class PL_GAMEUIPLUGIN_DLL plUiPath : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiPath, plUiVisualElement);

public:
  plUiPath();
  ~plUiPath();

  void SetPathData(const char* szData); // [ property ] (invalidates the tessellation cache)
  const char* GetPathData() const { return m_sPathData; }

  plEnum<plUiPathStretch> m_Stretch;

  /// \brief The tessellated triangle list (path space, consecutive triples) and the
  /// path-space bounds. Built lazily and cached; thread-safe.
  const plDynamicArray<plVec2>& GetTriangles(plVec2& out_vMin, plVec2& out_vMax) const;

private:
  plString m_sPathData;

  mutable plDynamicArray<plVec2> m_Triangles;
  mutable plVec2 m_vPathMin = plVec2::MakeZero();
  mutable plVec2 m_vPathMax = plVec2::MakeZero();
  mutable bool m_bTessellated = false;
};

/// \brief An invisible layout element that occupies a fixed amount of space.
///
/// Width/Height set its size directly (the main-axis value is what a Stack Panel
/// consumes); anchors/offsets still position it under free placement.
class PL_GAMEUIPLUGIN_DLL plUiSpacer : public plUiElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiSpacer, plUiElement);

public:
  plUiSpacer();
  ~plUiSpacer();

  float m_fWidth = 100.0f;
  float m_fHeight = 20.0f;
  bool m_bExpand = false; ///< in a Stack Panel, consume an equal share of unused main-axis space
};

/// \brief A single-line text entry field.
///
/// Focus it (click or gamepad navigation), type to edit; the caret blinks at the edit
/// position. Every edit live-writes the TextBinding entry (like a slider drag); Enter
/// commits and raises SubmitEvent, Escape reverts to the bound value and drops focus.
/// Editing state is per canvas INSTANCE — the widget works inside sub-documents.
class PL_GAMEUIPLUGIN_DLL plUiTextInput : public plUiVisualElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiTextInput, plUiVisualElement);

public:
  plUiTextInput();
  ~plUiTextInput();

  plString m_sText;        ///< authored default content
  plString m_sTextBinding; ///< two-way string blackboard entry (display + live write)
  plString m_sPlaceholder; ///< dim hint while the field is empty
  plUiFontResourceHandle m_hFont;
  plColorGammaUB m_TextColor = plColorGammaUB(255, 255, 255, 255);
  plUInt32 m_uiFontSize = 16;
  plUInt32 m_uiMaxLength = 0; ///< maximum character count; 0 = unlimited
  plColorGammaUB m_CaretColor = plColorGammaUB(6, 250, 201, 255);
  plString m_sSubmitEvent; ///< raised as plMsgUiEvent (and clip event) when Enter commits
};

/// \brief A text widget.
///
/// Renders with the assigned SDF font (crisp at any scale). If no font is set (or it fails
/// to load), the text falls back to the debug bitmap font.
class PL_GAMEUIPLUGIN_DLL plUiText : public plUiElement
{
  PL_ADD_DYNAMIC_REFLECTION(plUiText, plUiElement);

public:
  plUiText();
  ~plUiText();

  /// Displayed string precedence: TextBinding (blackboard) > Localized (plTranslate(Text)) > Text.
  plString m_sText;
  plString m_sTextBinding; ///< name of a blackboard entry whose value replaces Text
  bool m_bLocalized = false;
  plUiFontResourceHandle m_hFont;
  plColorGammaUB m_Color = plColorGammaUB(255, 255, 255, 255);
  plUInt32 m_uiFontSize = 16;
  plEnum<plUiTextHAlign> m_HAlign;
  plEnum<plUiTextVAlign> m_VAlign;

  bool m_bWrap = false; ///< break lines at word boundaries when they exceed the rect width
  plEnum<plUiTextOverflow> m_Overflow;
};
