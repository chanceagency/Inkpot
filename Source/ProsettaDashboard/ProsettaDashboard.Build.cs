using System.IO;
using UnrealBuildTool;
using UnrealBuildTool.Rules;

public class ProsettaDashboard : ModuleRules
{
	public ProsettaDashboard(ReadOnlyTargetRules Target) : base(Target)
	{
		InkpotBuildRules.RequireMinimumEngine(Target, "ProsettaDashboard");

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Dashboard sources include their siblings by name.
		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private"),
			Path.Combine(ModuleDirectory, "Private", "Dashboard")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"EditorSubsystem",
			"Inkpot",
			"InkpotEditor"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"AssetRegistry",
			"AssetTools",
			"Blutility",
			"DirectoryWatcher",
			"EditorFramework",
			"InputCore",
			"Json",
			"JsonUtilities",
			"LevelEditor",
			"OutputLog",
			"Projects",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"SourceControl",
			"ToolMenus",
			"ToolWidgets",
			"UMG",
			"UMGEditor",
			"UnrealEd",
			"WorkspaceMenuStructure"
		});
	}
}
