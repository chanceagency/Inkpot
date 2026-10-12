using UnrealBuildTool;

public class InkPlusPlus : ModuleRules
{
	public InkPlusPlus(ReadOnlyTargetRules Target) : base(Target)
	{
		InkpotBuildRules.RequireMinimumEngine(Target, "InkPlusPlus");

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

// Shared helpers for the Inkpot plugin's module rules (InkPlusPlus, Inkpot, InkpotEditor).
// UBT compiles every *.Build.cs in the plugin into one rules assembly, but does
// not pick up standalone .cs files, so this lives here rather than its own file.
public static class InkpotBuildRules
{
	// Minimum supported engine: Unreal 5.7.4 (latest stable 5.7 patch).
	public const int MinMajorVersion = 5;
	public const int MinMinorVersion = 7;
	public const int MinPatchVersion = 4;

	public static void RequireMinimumEngine(ReadOnlyTargetRules Target, string ModuleName)
	{
		int Major = Target.Version.MajorVersion;
		int Minor = Target.Version.MinorVersion;
		int Patch = Target.Version.PatchVersion;

		if (Major < MinMajorVersion
			|| (Major == MinMajorVersion && (Minor < MinMinorVersion
				|| (Minor == MinMinorVersion && Patch < MinPatchVersion))))
		{
			throw new BuildException("{0} requires Unreal Engine {1}.{2}.{3} or later (found {4}.{5}.{6}).",
				ModuleName, MinMajorVersion, MinMinorVersion, MinPatchVersion, Major, Minor, Patch);
		}
	}
}
