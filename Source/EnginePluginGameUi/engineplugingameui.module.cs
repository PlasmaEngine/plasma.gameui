using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginGameUiModule : ModuleRules
{
    public EnginePluginGameUiModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EnginePluginGameUi", "EnginePluginGameUiPCH.h",
            "BUILDSYSTEM_BUILDING_ENGINEPLUGINGAMEUI_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/GameUiPlugin", "plGameUiPlugin");
    }
}
