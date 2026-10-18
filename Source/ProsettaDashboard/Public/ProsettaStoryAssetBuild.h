#pragma once
#include "CoreMinimal.h"

struct FProsettaStoryAssetBuildOptions;

/**
 * Imports a project's dialog presentation (voice takes, portraits) for a story asset that is being
 * rebuilt. Inkpot has no presentation format of its own, so projects that have one install an importer
 * on FProsettaDashboardModule. Packages it writes as <AssetPath>_Presentation or under
 * <AssetPath's folder>/ProsettaMedia/ are saved with the story; nothing else is. False with OutError fails the build.
 */
using FProsettaPresentationImporter = TFunction<bool(const FProsettaStoryAssetBuildOptions& Options, FString& OutError)>;

/**
 * Builds (imports or reimports) an Inkpot story asset from a synced Prosetta mirror. Used by the
 * dashboard's "Rebuild asset" action and available to project commandlets. Working files are staged
 * under StageDir (Saved/ProsettaImport by default), never inside the Saved/Prosetta mirror.
 */
struct FProsettaStoryAssetBuildOptions
{
	/** Verified mirror the compiled story, lines and media come from. Default: Saved/Prosetta. */
	FString MirrorRoot;
	/** Volume the asset is built for; empty builds the canonical story. Resolves paths through manifest.json. */
	FString VolumeSlug;
	/** Target asset, e.g. /Game/ProsettaSyncedStory or /Game/Prosetta/Volumes/<slug>/Story. */
	FString AssetPath;
	FString StageDir;
	/** Explicit inputs; empty resolves them from the mirror (manifest-driven for a volume). */
	FString SourceInkPath;
	FString CompiledStoryPath;
	FString LinesPath;
	/** FFmpeg override handed to the presentation importer. */
	FString AudioDecoder;
	/** Run ImportPresentation too (needs the mirror's media). Off for story-only rebuilds and tests. */
	bool bImportPresentation = true;
	/** The project's presentation importer; unset builds the story only. */
	FProsettaPresentationImporter ImportPresentation;
	/** Save the packages this build wrote when done. */
	bool bSave = true;
};

struct FProsettaStoryAssetBuildResult
{
	bool bSucceeded = false;
	FString Error;
	/** /Game/Path/Name.Name of the asset that was written. */
	FString ObjectPath;
	bool bReimported = false;
	/** The staged file the asset was imported from. */
	FString StagedPath;
	/** The compiled story file that was used, when one was. */
	FString CompiledStoryPath;
};

/** Importer working directory: <Project>/Saved/ProsettaImport (outside the Saved/Prosetta mirror). */
PROSETTADASHBOARD_API FString ProsettaDefaultImportStageDir();
/** The canonical story asset path from Project Settings > Prosetta Sync (default /Game/ProsettaSyncedStory). */
PROSETTADASHBOARD_API FString ProsettaDefaultStoryAssetPath();
/** /Game/Prosetta/Volumes/<Slug>/Story, the conventional per-volume asset. */
PROSETTADASHBOARD_API FString ProsettaVolumeAssetPath(const FString& Slug);
/** Splits /Game/Path/Name[.Name] into its package path and asset name; false for an invalid long package name. */
PROSETTADASHBOARD_API bool SplitProsettaAssetPath(const FString& AssetPath, FString& OutPackagePath, FString& OutAssetName);
/** Reads a compiled story (raw Ink JSON or a Prosetta `{story: …}` wrapper) and re-serializes the Ink JSON. */
PROSETTADASHBOARD_API bool ReadProsettaCompiledStory(const FString& Path, FString& OutInkJson, FString& OutError);
/** Imports or reimports the asset. With a source ink file the .ink flow runs and the compiled story is kept; otherwise the compiled .ink.json flow runs. */
PROSETTADASHBOARD_API FProsettaStoryAssetBuildResult RebuildProsettaStoryAsset(const FProsettaStoryAssetBuildOptions& Options);
