using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class GameUiTestModule : ModuleRules
{
    public GameUiTestModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/GameUiTest", "GameUiTestPCH.h",
            "BUILDSYSTEM_BUILDING_GAMEUITEST");

        // test framework headers and import library from the SDK
        var unitTestsRoot = Path.Combine(PlasmaPackageSdk.Root, "Code", "UnitTests");
        PublicIncludePaths.Add(unitTestsRoot);
        PublicIncludePaths.Add(Path.Combine(unitTestsRoot, "TestFramework"));
        PublicIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Intermediate", "PlasmaBuild", "Generated", "Code", "UnitTests"));
        PublicIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Intermediate", "PlasmaBuild", "Generated", "Code", "UnitTests", "TestFramework"));

        if (context.Platform == TargetPlatform.Windows)
        {
            PublicIncludePaths.Add(Path.Combine(unitTestsRoot, "TestFramework", "Platform", "Win"));
        }

        PlasmaPackageSdk.AddImportLibrary(this, context, "plTestFramework");

        // the Game UI runtime ships with the engine (its editor UI is built on it)
        PlasmaPackageSdk.AddImportLibrary(this, context, "plGameUiPlugin");
    }
}
