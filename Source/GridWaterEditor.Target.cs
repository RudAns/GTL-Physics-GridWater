using UnrealBuildTool;

public class GridWaterEditorTarget : TargetRules
{
	public GridWaterEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("GridWater");
	}
}
