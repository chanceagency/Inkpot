using UnrealBuildTool;

public class Inkpot : ModuleRules
{
	public Inkpot(ReadOnlyTargetRules Target) : base(Target)
	{
		// Minimum supported engine: Unreal 5.7.4 (latest stable 5.7 patch).
		if (Target.Version.MajorVersion < 5 || (Target.Version.MajorVersion == 5 && (Target.Version.MinorVersion < 7 || (Target.Version.MinorVersion == 7 && Target.Version.PatchVersion < 4))))
		{
			throw new BuildException("Inkpot requires Unreal Engine 5.7.4 or later (found {0}.{1}.{2}).", Target.Version.MajorVersion, Target.Version.MinorVersion, Target.Version.PatchVersion);
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] 
            {
			}
		);
		
		PrivateIncludePaths.AddRange(
			new string[] 
            {
			}
		);
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
                "CoreUObject",
                "Engine",

                "InkPlusPlus",

                "DeveloperSettings",
                "GameplayTags",

                "Json"
            }
        );
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UMG"
			}
		);
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
		
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
				}
			);
		}
	}
}
