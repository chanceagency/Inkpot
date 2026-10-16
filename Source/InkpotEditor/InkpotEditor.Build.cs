using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class InkpotEditor : ModuleRules
{
	public InkpotEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		InkpotBuildRules.RequireMinimumEngine(Target, "InkpotEditor");

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

                "Projects",
                "EditorSubsystem"
            }
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",

                "InkPlusPlus",
                "Inkpot",
                "Json",
                "AssetRegistry",
                "AudioEditor",

				"Slate",
				"SlateCore",
				"UMG",
                "UMGEditor",
                "Blutility",
                "ContentBrowser",
                "ToolMenus",
                "GameplayTags",

				"EditorScriptingUtilities",
				"MessageLog",
				"DirectoryWatcher"
            }
        );
		
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
