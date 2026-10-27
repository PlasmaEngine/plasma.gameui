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
        PlasmaPackageSdk.AddImportLibrary(this, context, "plGameUiPlugin");

        // The animation panel's clip list, strips and track view are engine-drawn: EditorUi.
        PlasmaPackageSdk.AddImportLibrary(this, context, "plEditorUi");
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/EnginePluginGameUi", "plEnginePluginGameUi");
    }
}
