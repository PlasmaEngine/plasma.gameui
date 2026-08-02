using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class GameUiPluginModule : ModuleRules
{
    public GameUiPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/GameUiPlugin", "GameUiPluginPCH.h",
            "BUILDSYSTEM_BUILDING_GAMEUIPLUGIN_LIB");
    }
}
