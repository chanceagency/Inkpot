using UnrealBuildTool;

public class Inkpot : ModuleRules
{
	public Inkpot(ReadOnlyTargetRules Target) : base(Target)
	{
		InkpotBuildRules.RequireMinimumEngine(Target, "Inkpot");

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

                "Json",
                "UMG"
            }
        );
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
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
