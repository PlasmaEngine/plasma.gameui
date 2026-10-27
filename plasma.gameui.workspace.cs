// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaGameUiWorkspace : WorkspaceRules
{
    public PlasmaGameUiWorkspace(BuildContext context) : base(context)
    {
        // The GameUi runtime (plGameUiPlugin) ships with the engine; this package builds on it.
        TargetNames.Add("EnginePluginGameUi");
        TargetNames.Add("EditorPluginGameUi");
        TargetNames.Add("GameUiTest");
    }
}
