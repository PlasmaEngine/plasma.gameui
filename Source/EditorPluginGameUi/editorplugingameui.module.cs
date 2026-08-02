using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginGameUiModule : ModuleRules
{
    public EditorPluginGameUiModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EditorPluginGameUi", "EditorPluginGameUiPCH.h",
            "BUILDSYSTEM_BUILDING_EDITORPLUGINGAMEUI_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PlasmaPackageSdk.ConfigureAssetsBuiltIn(this, context);

        // UiFontAsset rasterises glyphs with FreeType, vendored by RmlUi and read out of the SDK.
        var freetype = Path.Combine(PlasmaPackageSdk.Root, "Code", "ThirdParty", "RmlUi", "Dependencies", "freetype");
        PrivateIncludePaths.Add(Path.Combine(freetype, "include"));
        PrivateLibraries.Add(Path.Combine(freetype, "win", "x64", "freetype.lib"));
        PlasmaPackageFreetype.StageRuntime(context);
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/GameUiPlugin", "plGameUiPlugin");
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/EnginePluginGameUi", "plEnginePluginGameUi");

        PlasmaPackageQt.ConfigureQtModule(this, context, "EditorPluginGameUiModule",
            "Source/EditorPluginGameUi", "Core", "Gui", "Widgets", "Network");
    }
}
