using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class GameUiTestTarget : TargetRules
{
    public GameUiTestTarget(BuildContext context) : base(context)
    {
        Type = TargetType.Executable;
        OutputName = "GameUiTest";
        // Kept out of the package binary directory, which is what installs and publishes draw from. The
        // test finds the engine DLLs (plGameUiPlugin among them) through PATH.
        OutputDirectory = Path.Combine(context.ProjectRoot, "Bin", "Tests", $"{context.Platform}_x64_{context.Configuration}");
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        GenerateDebugInfo = true;

        ExtraModules.Add("GameUiTestModule");
    }
}
