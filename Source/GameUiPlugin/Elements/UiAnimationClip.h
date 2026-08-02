#pragma once

#include <GameUiPlugin/Elements/UiElements.h>
#include <GameUiPlugin/GameUiPluginDLL.h>

/// \brief Which channel of an element a plUiAnimTrack animates.
///
/// All channels are relative to the authored state: offsets add pixels to the laid-out
/// rect (children follow), opacity/scale multiply the element's own visuals, tint
/// multiplies the visual color.
struct PL_GAMEUIPLUGIN_DLL plUiAnimTarget
{
  using StorageType = plUInt8;

  enum Enum
  {
    OffsetX,  ///< px shift of the element rect (children follow)
    OffsetY,  ///< px shift of the element rect (children follow)
    Opacity,  ///< multiplier on the element's own opacity
    Scale,    ///< multiplier on the element's own visuals (around the rect center)
    Tint,     ///< color multiplied into the element's own visuals
    Width,    ///< px added to the element rect's width, centered (children follow)
    Height,   ///< px added to the element rect's height, centered (children follow)
    Rotation, ///< degrees; rotates the element's own visuals around the rect center
    Fill,     ///< multiplier on a progress bar's fill amount

    Default = OffsetX
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiAnimTarget);

/// \brief How a playing plUiAnimationClip behaves at the end of its duration.
struct PL_GAMEUIPLUGIN_DLL plUiClipLoopMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    Once,     ///< holds the last-key pose
    Loop,     ///< wraps around to 0
    PingPong, ///< bounces between 0 and the duration

    Default = Once
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_GAMEUIPLUGIN_DLL, plUiClipLoopMode);

/// \brief A keyframe on a plUiAnimTrack.
///
/// The easing shapes the segment that STARTS at this key. Float targets read Value,
/// the Tint target reads Color.
class PL_GAMEUIPLUGIN_DLL plUiAnimKey : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiAnimKey, plReflectedClass);

public:
  plUiAnimKey();
  ~plUiAnimKey();

  float m_fTime = 0.0f;  ///< seconds from the clip start
  float m_fValue = 0.0f; ///< for float targets (OffsetX/Y in px, Opacity/Scale as factor)
  plColor m_Color = plColor::White;
  plEnum<plUiAnimEasing> m_Easing;
};

/// \brief One property track of a plUiAnimationClip: a keyed channel on one element.
///
/// The element is referenced by its Name (first match in the document tree). Keys are
/// evaluated piecewise: before the first key the first key's value holds, after the last
/// key the last key's value holds.
class PL_GAMEUIPLUGIN_DLL plUiAnimTrack : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiAnimTrack, plReflectedClass);

public:
  plUiAnimTrack();
  ~plUiAnimTrack();

  plString m_sElement; ///< Name of the animated element
  plEnum<plUiAnimTarget> m_Target;
  plHybridArray<plUiAnimKey*, 4> m_Keys; ///< owned; not required to be sorted (playback sorts)
};

/// \brief A named keyframe animation clip of a UI Document ("lite" clips: intro slides,
/// pulses, attention blinks — cutscene-grade sequencing stays in the Sequencer).
///
/// A clip can be started in three ways:
/// - PlayOnEvent: a plUiButton click that raises this event identifier starts the clip
/// - EntryBinding + Trigger mode: plays forward while the bool entry is true, backward when false
/// - EntryBinding + Value mode: the clamped 0..1 entry value IS the playback position
///
/// A clip that has never been started poses its tracks at time 0, so an intro can park
/// its elements offscreen until the event plays them in.
class PL_GAMEUIPLUGIN_DLL plUiAnimationClip : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plUiAnimationClip, plReflectedClass);

public:
  plUiAnimationClip();
  ~plUiAnimationClip();

  plString m_sName;
  float m_fDuration = 0.8f;  ///< seconds
  plString m_sPlayOnEvent;   ///< click-event identifier that starts the clip (optional)
  plString m_sEntryBinding;  ///< blackboard entry driving the clip (optional)
  plEnum<plUiAnimationMode> m_Mode; ///< how EntryBinding drives playback
  plEnum<plUiClipLoopMode> m_LoopMode;

  plHybridArray<plUiAnimTrack*, 4> m_Tracks; ///< owned
};
