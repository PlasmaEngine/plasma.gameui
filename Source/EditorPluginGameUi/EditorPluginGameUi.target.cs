using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginGameUiTarget : TargetRules
{
    public EditorPluginGameUiTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plEditorPluginGameUi";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        TargetDependencies.Add("GameUiPlugin");
        TargetDependencies.Add("EnginePluginGameUi");
        ExtraModules.Add("EditorPluginGameUiModule");
    }
}
