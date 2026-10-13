using UnrealBuildTool;

public class GridWater : ModuleRules
{
	public GridWater(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore", "ProceduralMeshComponent", "RenderCore", "RHI"
		});
	}
}
