using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class GameUiPluginTarget : TargetRules
{
    public GameUiPluginTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plGameUiPlugin";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        ExtraModules.Add("GameUiPluginModule");
    }
}
