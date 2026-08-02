// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaGameUiWorkspace : WorkspaceRules
{
    public PlasmaGameUiWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("GameUiPlugin");
        TargetNames.Add("EnginePluginGameUi");
        TargetNames.Add("EditorPluginGameUi");
    }
}
