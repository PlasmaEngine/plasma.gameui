#include <GameUiPlugin/GameUiPluginPCH.h>

#include <GameUiPlugin/Elements/UiAnimationClip.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plUiAnimTarget, 1)
  PL_ENUM_CONSTANTS(plUiAnimTarget::OffsetX, plUiAnimTarget::OffsetY, plUiAnimTarget::Opacity, plUiAnimTarget::Scale, plUiAnimTarget::Tint)
  PL_ENUM_CONSTANTS(plUiAnimTarget::Width, plUiAnimTarget::Height, plUiAnimTarget::Rotation, plUiAnimTarget::Fill)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plUiClipLoopMode, 1)
  PL_ENUM_CONSTANTS(plUiClipLoopMode::Once, plUiClipLoopMode::Loop, plUiClipLoopMode::PingPong)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiAnimKey, 1, plRTTIDefaultAllocator<plUiAnimKey>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Time", m_fTime)->AddAttributes(new plClampValueAttribute(0.0f, 3600.0f), new plSuffixAttribute(" sec")),
    PL_MEMBER_PROPERTY("Value", m_fValue),
    PL_MEMBER_PROPERTY("Color", m_Color)->AddAttributes(new plExposeColorAlphaAttribute(), new plDefaultValueAttribute(plColor::White)),
    PL_ENUM_MEMBER_PROPERTY("Easing", plUiAnimEasing, m_Easing),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Key @ {Time}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiAnimTrack, 1, plRTTIDefaultAllocator<plUiAnimTrack>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Element", m_sElement),
    PL_ENUM_MEMBER_PROPERTY("Target", plUiAnimTarget, m_Target),
    PL_ARRAY_MEMBER_PROPERTY("Keys", m_Keys)->AddFlags(plPropertyFlags::PointerOwner),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Track: {Element} . {Target}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiAnimationClip, 1, plRTTIDefaultAllocator<plUiAnimationClip>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Duration", m_fDuration)->AddAttributes(new plDefaultValueAttribute(0.8f), new plClampValueAttribute(0.01f, 3600.0f), new plSuffixAttribute(" sec")),
    PL_MEMBER_PROPERTY("PlayOnEvent", m_sPlayOnEvent),
    PL_MEMBER_PROPERTY("EntryBinding", m_sEntryBinding),
    PL_ENUM_MEMBER_PROPERTY("Mode", plUiAnimationMode, m_Mode),
    PL_ENUM_MEMBER_PROPERTY("LoopMode", plUiClipLoopMode, m_LoopMode),
    PL_ARRAY_MEMBER_PROPERTY("Tracks", m_Tracks)->AddFlags(plPropertyFlags::PointerOwner),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Clip: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiAnimKey::plUiAnimKey() = default;
plUiAnimKey::~plUiAnimKey() = default;

plUiAnimTrack::plUiAnimTrack() = default;

plUiAnimTrack::~plUiAnimTrack()
{
  for (plUiAnimKey* pKey : m_Keys)
  {
    PL_DEFAULT_DELETE(pKey);
  }

  m_Keys.Clear();
}

plUiAnimationClip::plUiAnimationClip() = default;

plUiAnimationClip::~plUiAnimationClip()
{
  for (plUiAnimTrack* pTrack : m_Tracks)
  {
    PL_DEFAULT_DELETE(pTrack);
  }

  m_Tracks.Clear();
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Elements_Implementation_UiAnimationClip);
