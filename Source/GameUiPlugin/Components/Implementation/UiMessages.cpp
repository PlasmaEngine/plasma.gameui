#include <GameUiPlugin/GameUiPluginPCH.h>

#include <GameUiPlugin/Components/UiMessages.h>

// clang-format off
PL_IMPLEMENT_MESSAGE_TYPE(plMsgUiEvent);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgUiEvent, 1, plRTTIDefaultAllocator<plMsgUiEvent>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Identifier", m_sIdentifier),
    PL_MEMBER_PROPERTY("ItemIndex", m_iItemIndex)->AddAttributes(new plDefaultValueAttribute(-1)),
    PL_MEMBER_PROPERTY("UiInstanceId", m_uiUiInstanceId),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Components_Implementation_UiMessages);
