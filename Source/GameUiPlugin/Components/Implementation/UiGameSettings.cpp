#include <GameUiPlugin/GameUiPluginPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Core/Utils/Blackboard.h>
#include <Foundation/System/Screen.h>
#include <GameEngine/GameApplication/GameApplication.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>

namespace
{
  plStringBuilder Key(plStringView id, const char* prefix = "Settings_")
  {
    plStringBuilder key(prefix, id);
    key.ReplaceAll(".", "_");
    return key;
  }
  plString FriendlyValue(plStringView id, const plVariant& value)
  {
    const char* name = nullptr;
    if (id == "Display.Mode")
      name = "Window mode";
    else if (id == "Display.Monitor")
      name = "Monitor";
    else if (id == "Display.Width")
      name = "Resolution width";
    else if (id == "Display.Height")
      name = "Resolution height";
    else if (id == "Display.RefreshRate")
      name = "Refresh rate";
    else if (id == "Display.VSync")
      name = "VSync";
    else if (id == "Display.FpsCap")
      name = "Frame rate limit";
    else if (id == "Audio.Master")
      name = "Master volume";
    else if (id == "Audio.Mute")
      name = "Mute audio";
    else if (id == "Audio.Music")
      name = "Music volume";
    else if (id == "Audio.Effects")
      name = "Effects volume";
    else if (id == "Audio.Dialogue")
      name = "Dialogue volume";
    else if (id == "Accessibility.Subtitles")
      name = "Subtitles";
    else if (id == "Accessibility.TextScale")
      name = "Text size";
    else if (id == "Accessibility.UiScale")
      name = "Interface size";
    else if (id == "Accessibility.ReducedMotion")
      name = "Reduce motion";
    else if (id == "Accessibility.ScreenShake")
      name = "Camera shake";
    if (!name)
      return {};
    plString display = value.ConvertTo<plString>();
    if (value.IsA<bool>())
      display = value.Get<bool>() ? "On" : "Off";
    else if (id == "Display.Mode")
    {
      const char* modes[] = {"Fixed window", "Resizable window", "Borderless", "Fullscreen"};
      display = modes[plMath::Clamp(value.ConvertTo<plInt32>(), 0, 3)];
    }
    else if (id == "Display.Monitor" && value.ConvertTo<plInt32>() == -1)
      display = "Primary";
    else if (id == "Display.RefreshRate" && value.ConvertTo<plInt32>() == 0)
      display = "Desktop";
    else if (id == "Display.FpsCap" && value.ConvertTo<plInt32>() == 0)
      display = "Unlimited";
    else if (id.StartsWith("Audio.") || id.StartsWith("Accessibility."))
    {
      plStringBuilder percent;
      percent.SetFormat("{}%", plInt32(plMath::Round(value.ConvertTo<double>() * 100)));
      display = percent;
    }
    return plStringBuilder(name, ": ", display);
  }
  void Place(plUiElement* element, float x, float y, float width, float height)
  {
    element->m_vAnchorMin = plVec2::MakeZero();
    element->m_vAnchorMax = plVec2::MakeZero();
    element->m_vOffsets = plVec4(x, y, width, height);
  }
  plUiText* Label(plUiElement* parent, plStringView name, plStringView text, float x, float y, float width)
  {
    plUiText* label = PL_DEFAULT_NEW(plUiText);
    label->m_sName = name;
    label->m_sText = text;
    label->m_uiFontSize = 20;
    Place(label, x, y, width, 28);
    parent->m_Children.PushBack(label);
    return label;
  }
  plUiButton* Button(plUiElement* parent, plStringView name, plStringView text, float x, float y, float width)
  {
    plUiButton* button = PL_DEFAULT_NEW(plUiButton);
    button->m_sName = name;
    button->m_Color = plColorGammaUB(34, 64, 92);
    button->m_HoverColor = plColorGammaUB(46, 103, 143);
    button->m_PressedColor = plColorGammaUB(24, 142, 126);
    button->m_sClickEntryBinding = name;
    button->m_ClickEntryAction = plUiClickEntryAction::SetTrue;
    Place(button, x, y, width, 36);
    parent->m_Children.PushBack(button);
    Label(button, plStringBuilder(name, "_Label"), text, 10, 5, width - 20);
    return button;
  }
} // namespace

void plUiCanvasComponent::CancelGameSettingsPreview()
{
  if (m_uiSettingsTransaction)
    if (auto* app = plGameApplication::GetGameApplicationInstance())
      app->GetGameSettings().Revert(m_uiSettingsTransaction);
  m_uiSettingsTransaction = 0;
  m_SettingsDraft.Clear();
}
void plUiCanvasComponent::OnDeactivated()
{
  CancelGameSettingsPreview();
  SUPER::OnDeactivated();
}
void plUiCanvasComponent::CreateGameSettingsMenu()
{
  auto* app = plGameApplication::GetGameApplicationInstance();
  if (!app)
    return;
  const plString resourceId = "GameSettings/BuiltinMenu";
  m_hDocument = plResourceManager::GetExistingResource<plUiDocumentResource>(resourceId);
  if (m_hDocument.IsValid())
    return;
  plUiDocumentResourceDescriptor document;
  plUiPanel* root = PL_DEFAULT_NEW(plUiPanel);
  document.m_pRoot = root;
  root->m_sName = "GameSettings";
  root->m_Color = plColorGammaUB(12, 20, 30, 245);
  root->m_vAnchorMin = plVec2::MakeZero();
  root->m_vAnchorMax = plVec2(1);
  root->m_vOffsets = plVec4::MakeZero();
  Label(root, "Title", "Game settings", 30, 20, 500)->m_uiFontSize = 28;
  Label(root, "Preset", "", 30, 65, 550)->m_sTextBinding = "Settings_PresetLabel";
  Button(root, "Settings_NextPreset", "Next quality preset", 600, 55, 350)->m_sDisabledBinding = "Settings_QualityDisabled";
  const char* keys[] = {"Display.Mode", "Display.Monitor", "Display.Width", "Display.Height", "Display.RefreshRate", "Display.VSync", "Display.FpsCap",
    "Audio.Master", "Audio.Mute", "Audio.Music", "Audio.Effects", "Audio.Dialogue",
    "Accessibility.Subtitles", "Accessibility.TextScale", "Accessibility.UiScale", "Accessibility.ReducedMotion", "Accessibility.ScreenShake"};
  auto defaults = app->GetGameSettings().GetDefaults();
  for (plUInt32 i = 0; i < PL_ARRAY_SIZE(keys); ++i)
  {
    const float x = 30.0f + (i / 9) * 570.0f, y = 120.0f + (i % 9) * 72.0f;
    Label(root, Key(keys[i], "Label_"), keys[i], x, y, 530)->m_sTextBinding = Key(keys[i], "Settings_Label_");
    const auto* value = defaults.GetValue(keys[i]);
    if (value->IsA<bool>())
    {
      auto* button = Button(root, Key(keys[i]), "Toggle", x, y + 29, 220);
      button->m_ClickEntryAction = plUiClickEntryAction::Toggle;
      button->m_sDisabledBinding = Key(keys[i], "Settings_Disabled_");
    }
    else
    {
      auto* minus = Button(root, Key(keys[i], "Settings_Minus_"), "-", x, y + 29, 90);
      auto* plus = Button(root, Key(keys[i], "Settings_Plus_"), "+", x + 100, y + 29, 90);
      minus->m_sDisabledBinding = Key(keys[i], "Settings_Disabled_");
      plus->m_sDisabledBinding = Key(keys[i], "Settings_Disabled_");
    }
  }
  Button(root, "Settings_Apply", "Apply", 30, 810, 180)->m_sDisabledBinding = "Settings_Busy";
  Button(root, "Settings_Confirm", "Keep", 225, 810, 180)->m_sVisibleBinding = "Settings_ConfirmVisible";
  Button(root, "Settings_Revert", "Revert", 420, 810, 180)->m_sVisibleBinding = "Settings_ConfirmVisible";
  Button(root, "Settings_Reset", "Reset defaults", 615, 810, 240)->m_sDisabledBinding = "Settings_Busy";
  Label(root, "Status", "", 30, 865, 1080)->m_sTextBinding = "Settings_Status";
  auto* subtitle = Label(root, "SubtitleExample", "Subtitle size preview", 30, 905, 1080);
  subtitle->m_sVisibleBinding = "Settings_Accessibility_Subtitles";
  m_hDocument = plResourceManager::CreateResource<plUiDocumentResource>(resourceId, std::move(document), "Built-in game settings menu");
}

void plUiCanvasComponent::UpdateGameSettings()
{
  auto* app = plGameApplication::GetGameApplicationInstance();
  if (!app)
    return;
  auto& settings = app->GetGameSettings();
  // Canvas consumes UI scale/text/reduced-motion/subtitles; game consumers read the same snapshot.
  settings.SetAccessibilitySupported(true);
  auto state = settings.GetApplyState();
  m_pLocalBlackboard->SetEntryValue("Settings_QualityDescriptors", settings.GetQualityDescriptors());
  bool refresh = m_SettingsDraft.IsEmpty();
  if (m_iSettingsStatus != plInt32(state.m_Status))
  {
    if (state.m_uiTransaction == m_uiSettingsTransaction && (state.m_Status == plGameSettingsStatus::Applied || state.m_Status == plGameSettingsStatus::Reverted || state.m_Status == plGameSettingsStatus::SaveFailure))
      refresh = true;
    m_iSettingsStatus = plInt32(state.m_Status);
  }
  auto publish = [this]()
  {
    for (auto it = m_SettingsDraft.GetIterator(); it.IsValid(); ++it)
      m_pLocalBlackboard->SetEntryValue(Key(it.Key()), it.Value());
  };
  if (refresh)
  {
    m_SettingsDraft = settings.GetConfirmedSettings();
    publish();
  }
  auto take = [this](plStringView key)
  {
    auto v = m_pLocalBlackboard->GetEntryValue(plTempHashedString(key), false);
    bool pressed = v.ConvertTo<bool>();
    if (pressed)
      m_pLocalBlackboard->SetEntryValue(key, false);
    return pressed;
  };
  const bool busy = state.m_Status == plGameSettingsStatus::Queued || state.m_Status == plGameSettingsStatus::AwaitingConfirmation;
  if (!busy)
  {
    for (auto it = m_SettingsDraft.GetIterator(); it.IsValid(); ++it)
    {
      if (it.Key() == "Quality.Overrides")
      {
        const auto overrides = m_pLocalBlackboard->GetEntryValue(plTempHashedString("Settings_Quality_Overrides"), it.Value());
        if (overrides.IsA<plVariantDictionary>()) it.Value() = overrides;
        continue;
      }
      auto value = m_pLocalBlackboard->GetEntryValue(plTempHashedString(Key(it.Key())), it.Value());
      it.Value() = value;
      if (value.IsNumber())
      {
        int direction = take(Key(it.Key(), "Settings_Plus_")) ? 1 : take(Key(it.Key(), "Settings_Minus_")) ? -1
                                                                                                           : 0;
        if (direction)
        {
          if (it.Key() == "Display.Width" || it.Key() == "Display.Height" || it.Key() == "Display.RefreshRate")
          {
            const auto modes = settings.GetDisplayModes();
            plInt32 selected = 0;
            for (plUInt32 i = 0; i < modes.GetCount(); ++i)
            {
              auto mode = modes[i].Get<plVariantDictionary>();
              if (mode["Width"] == m_SettingsDraft["Display.Width"] && mode["Height"] == m_SettingsDraft["Display.Height"])
                selected = plInt32(i);
            }
            if (!modes.IsEmpty())
            {
              auto mode = modes[plUInt32((selected + direction + modes.GetCount()) % modes.GetCount())].Get<plVariantDictionary>();
              m_SettingsDraft.Insert("Display.Width", mode["Width"]);
              m_SettingsDraft.Insert("Display.Height", mode["Height"]);
              m_SettingsDraft.Insert("Display.RefreshRate", m_SettingsDraft["Display.Mode"].ConvertTo<plInt32>() == 3 ? mode["RefreshRate"] : plVariant(plInt32(0)));
              publish();
            }
            continue;
          }
          double step = 0.1;
          if (it.Key().StartsWith("Display."))
            step = (it.Key() == "Display.Width" || it.Key() == "Display.Height") ? 80 : it.Key() == "Display.FpsCap"      ? 30
                                                                                      : it.Key() == "Display.RefreshRate" ? 1
                                                                                                                          : 1;
          double n = value.ConvertTo<double>() + direction * step;
          if (it.Key() == "Display.Mode")
          {
            n = plMath::Clamp(n, 0.0, 3.0);
            m_SettingsDraft.Insert("Display.RefreshRate", plInt32(0));
            m_pLocalBlackboard->SetEntryValue("Settings_Display_RefreshRate", plInt32(0));
          }
          else if (it.Key() == "Display.Monitor")
          {
            plHybridArray<plScreenInfo, 2> screens;
            plScreen::EnumerateScreens(screens).IgnoreResult();
            n = plMath::Clamp(n, -1.0, double(plInt32(screens.GetCount()) - 1));
          }
          else if (it.Key() == "Display.Width")
            n = plMath::Clamp(n, 640.0, 7680.0);
          else if (it.Key() == "Display.Height")
            n = plMath::Clamp(n, 480.0, 4320.0);
          else if (it.Key().StartsWith("Display."))
            n = plMath::Clamp(n, 0.0, 1000.0);
          else if (it.Key().EndsWith("Scale"))
            n = plMath::Clamp(n, 0.5, 3.0);
          else
            n = plMath::Clamp(n, 0.0, 1.0);
          it.Value() = value.IsA<plInt32>() ? plVariant(plInt32(n)) : plVariant(float(n));
          m_pLocalBlackboard->SetEntryValue(Key(it.Key()), it.Value());
        }
      }
      m_pLocalBlackboard->SetEntryValue(Key(it.Key(), "Settings_Label_"), FriendlyValue(it.Key(), it.Value()));
    }
    if (take("Settings_NextPreset"))
    {
      const auto presets = settings.GetPresets();
      const auto* selected = m_SettingsDraft.GetValue("Quality.Preset");
      plUInt32 next = 0;
      for (plUInt32 i = 0; i < presets.GetCount(); ++i)
        if (selected && *selected == plVariant(presets[i].m_sId))
          next = (i + 1) % presets.GetCount();
      if (!presets.IsEmpty())
        m_SettingsDraft.Insert("Quality.Preset", presets[next].m_sId);
      m_SettingsDraft.Insert("Quality.Overrides", plVariantDictionary());
      publish();
    }
    if (take("Settings_Reset"))
    {
      m_SettingsDraft = settings.ResetToDefaults();
      publish();
    }
    if (take("Settings_Apply"))
      m_uiSettingsTransaction = settings.Apply(m_SettingsDraft);
  }
  if (take("Settings_Confirm"))
    settings.Confirm(m_uiSettingsTransaction);
  if (take("Settings_Revert"))
    settings.Revert(m_uiSettingsTransaction);
  state = settings.GetApplyState();
  m_pLocalBlackboard->SetEntryValue("Settings_Dirty", m_SettingsDraft != settings.GetConfirmedSettings());
  m_pLocalBlackboard->SetEntryValue("Settings_ConfirmVisible", state.m_Status == plGameSettingsStatus::AwaitingConfirmation);
  m_pLocalBlackboard->SetEntryValue("Settings_Busy", busy);
  plStringBuilder status;
  if (state.m_Status == plGameSettingsStatus::AwaitingConfirmation)
    status.SetFormat("Keep these settings? Reverting in {} seconds.", plInt32(plMath::Ceil(state.m_fSecondsRemaining)));
  else if (state.m_bRestartRequired && state.m_Status == plGameSettingsStatus::Applied)
    status = "Settings saved. Restart to finish applying quality changes.";
  else if (!state.m_sMessage.IsEmpty())
    status = state.m_sMessage;
  else
    status = state.m_Status == plGameSettingsStatus::Applied ? "Settings saved." : state.m_Status == plGameSettingsStatus::Reverted ? "Settings reverted."
                                                                                                                                    : "Choose your settings, then Apply.";
  m_pLocalBlackboard->SetEntryValue("Settings_Status", plString(status));
  plStringBuilder presetLabel("Quality: ", m_SettingsDraft["Quality.Preset"].ConvertTo<plString>());
  m_pLocalBlackboard->SetEntryValue("Settings_PresetLabel", plString(presetLabel));
  for (const auto& item : settings.GetDescriptors())
  {
    const auto& d = item.Get<plVariantDictionary>();
    const auto *id = d.GetValue("Id"), *supported = d.GetValue("Supported");
    if (id && supported)
    {
      const auto& name = id->Get<plString>();
      const bool disabled = busy || !supported->ConvertTo<bool>();
      m_pLocalBlackboard->SetEntryValue(Key(name, "Settings_Disabled_"), disabled);
      if (name == "Quality.Preset")
        m_pLocalBlackboard->SetEntryValue("Settings_QualityDisabled", disabled);
      m_pLocalBlackboard->SetEntryValue(Key(name, "Settings_Reason_"), *d.GetValue("Reason"));
    }
  }
}
