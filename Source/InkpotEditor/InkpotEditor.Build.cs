using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class InkpotEditor : ModuleRules
{
	public InkpotEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		// Minimum supported engine: Unreal 5.7.4 (latest stable 5.7 patch).
		if (Target.Version.MajorVersion < 5 || (Target.Version.MajorVersion == 5 && (Target.Version.MinorVersion < 7 || (Target.Version.MinorVersion == 7 && Target.Version.PatchVersion < 4))))
		{
			throw new BuildException("InkpotEditor requires Unreal Engine 5.7.4 or later (found {0}.{1}.{2}).", Target.Version.MajorVersion, Target.Version.MinorVersion, Target.Version.PatchVersion);
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
				"MessageLog"
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
