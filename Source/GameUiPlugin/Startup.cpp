#include <GameUiPlugin/GameUiPluginPCH.h>

#include <Core/Input/InputManager.h>
#include <Foundation/Configuration/Plugin.h>
#include <Foundation/Configuration/Startup.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>
#include <GameUiPlugin/Resources/UiFontResource.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>

namespace
{
  void RegisterUiNavAction(const char* szAction, const char* szSlot1, const char* szSlot2, const char* szSlot3 = nullptr)
  {
    plInputActionConfig cfg;
    cfg.m_bApplyTimeScaling = false;
    cfg.m_sInputSlotTrigger[0] = szSlot1;
    cfg.m_sInputSlotTrigger[1] = szSlot2;
    cfg.m_sInputSlotTrigger[2] = szSlot3;

    plInputManager::SetInputActionConfig("UiNav", szAction, cfg, true);
  }

  /// default bindings for the UI navigation action set (mockup: "normal plInputManager
  /// actions, rebindable like any input" — games overwrite them with their own config)
  void RegisterUiNavActionSet()
  {
    RegisterUiNavAction("Up", plInputSlot_KeyUp, plInputSlot_Controller0_PadUp, plInputSlot_Controller0_LeftStick_PosY);
    RegisterUiNavAction("Down", plInputSlot_KeyDown, plInputSlot_Controller0_PadDown, plInputSlot_Controller0_LeftStick_NegY);
    RegisterUiNavAction("Left", plInputSlot_KeyLeft, plInputSlot_Controller0_PadLeft, plInputSlot_Controller0_LeftStick_NegX);
    RegisterUiNavAction("Right", plInputSlot_KeyRight, plInputSlot_Controller0_PadRight, plInputSlot_Controller0_LeftStick_PosX);
    RegisterUiNavAction("Accept", plInputSlot_KeyReturn, plInputSlot_KeyNumpadEnter, plInputSlot_Controller0_ButtonA);
    RegisterUiNavAction("Cancel", plInputSlot_KeyEscape, plInputSlot_Controller0_ButtonB);
  }
} // namespace

// clang-format off
PL_BEGIN_SUBSYSTEM_DECLARATION(GameUi, GameUiPlugin)

  BEGIN_SUBSYSTEM_DEPENDENCIES
    "Foundation",
    "Core"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
  }

  ON_HIGHLEVELSYSTEMS_STARTUP
  {
    RegisterUiNavActionSet();

    plResourceManager::RegisterResourceForAssetType("UI Document", plGetStaticRTTI<plUiDocumentResource>());
    plResourceManager::RegisterResourceForAssetType("UI Font", plGetStaticRTTI<plUiFontResource>());
    plResourceManager::RegisterResourceForAssetType("UI Style", plGetStaticRTTI<plUiStyleResource>());

    {
      plUiDocumentResourceDescriptor desc;
      plUiDocumentResourceHandle hResource = plResourceManager::CreateResource<plUiDocumentResource>("UiDocumentMissing", std::move(desc), "Fallback for missing UI document resource");
      plResourceManager::SetResourceTypeMissingFallback<plUiDocumentResource>(hResource);
    }

    {
      plUiFontResourceDescriptor desc;
      plUiFontResourceHandle hResource = plResourceManager::CreateResource<plUiFontResource>("UiFontMissing", std::move(desc), "Fallback for missing UI font resource");
      plResourceManager::SetResourceTypeMissingFallback<plUiFontResource>(hResource);
    }

    {
      // empty style: every GetClass returns nullptr -> elements degrade to local visuals
      plUiStyleResourceDescriptor desc;
      plUiStyleResourceHandle hResource = plResourceManager::CreateResource<plUiStyleResource>("UiStyleMissing", std::move(desc), "Fallback for missing UI style resource");
      plResourceManager::SetResourceTypeMissingFallback<plUiStyleResource>(hResource);
    }
  }

  ON_HIGHLEVELSYSTEMS_SHUTDOWN
  {
    plUiDocumentResource::CleanupDynamicPluginReferences();
    plUiFontResource::CleanupDynamicPluginReferences();
    plUiStyleResource::CleanupDynamicPluginReferences();
  }

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Startup);
