using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginGameUiModule : ModuleRules
{
    public EnginePluginGameUiModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EnginePluginGameUi", "EnginePluginGameUiPCH.h",
            "BUILDSYSTEM_BUILDING_ENGINEPLUGINGAMEUI_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        // The GameUi runtime ships with the engine.
        PlasmaPackageSdk.AddImportLibrary(this, context, "plGameUiPlugin");
    }
}
