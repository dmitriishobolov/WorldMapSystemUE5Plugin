using UnrealBuildTool;

public class WorldMapSystem : ModuleRules
{
	public WorldMapSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "SlateCore", "NetCore" });
		PrivateDependencyModuleNames.AddRange(new[] { "Slate", "InputCore" });
	}
}
