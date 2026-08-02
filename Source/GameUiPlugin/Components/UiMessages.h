#pragma once

#include <GameUiPlugin/GameUiPluginDLL.h>

#include <Core/Messages/EventMessage.h>

/// \brief Raised by plUiCanvasComponent when an interactive UI element is triggered (e.g. a button click).
struct PL_GAMEUIPLUGIN_DLL plMsgUiEvent : plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgUiEvent, plEventMessage);

  /// The ClickEvent identifier of the triggering element.
  plHashedString m_sIdentifier;

  /// When the click happened inside a plUiItemList row: the row's index; -1 otherwise.
  plInt32 m_iItemIndex = -1;

  /// When the click happened inside a dynamically spawned UI instance
  /// (plUiCanvasComponent::CreateUiInstance): that instance's id; 0 otherwise.
  plUInt32 m_uiUiInstanceId = 0;
};
