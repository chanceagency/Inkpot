using UnrealBuildTool;

public class InkPlusPlus : ModuleRules
{
	public InkPlusPlus(ReadOnlyTargetRules Target) : base(Target)
	{
		// Minimum supported engine: Unreal 5.7.4 (latest stable 5.7 patch).
		if (Target.Version.MajorVersion < 5 || (Target.Version.MajorVersion == 5 && (Target.Version.MinorVersion < 7 || (Target.Version.MinorVersion == 7 && Target.Version.PatchVersion < 4))))
		{
			throw new BuildException("InkPlusPlus requires Unreal Engine 5.7.4 or later (found {0}.{1}.{2}).", Target.Version.MajorVersion, Target.Version.MinorVersion, Target.Version.PatchVersion);
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
		);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Json",
				"JsonUtilities"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
