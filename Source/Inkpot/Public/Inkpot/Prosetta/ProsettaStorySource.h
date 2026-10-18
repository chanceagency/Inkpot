#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/SoftObjectPtr.h"
#include "ProsettaStorySource.generated.h"

class UInkpotStoryAsset;
class AActor;
class UWorld;
struct FProsettaManifestSummary;

/**
 * Which Prosetta story a level plays. Stored on the level's story source host (an actor implementing
 * IProsettaStorySourceHost) so a level can follow one volume of a multi-volume project, pin an
 * imported asset, or stay on the canonical mirror.
 * The default (no slug, no override, mirror preferred) resolves to Saved/Prosetta/compiled/story.json.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaStorySource
{
	GENERATED_BODY()

	/** Volume to play from a scoped sync; empty plays the canonical (whole project / single volume) story. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Story")
	FString VolumeSlug;
	/** Release tag the level was last pointed at ("" or "latest" follows live); informational, the sync profile decides what is pulled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Story")
	FString VolumeTag;
	/** When set, this imported story asset plays instead of the mirror. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Story")
	TSoftObjectPtr<UInkpotStoryAsset> StoryAssetOverride;
	/** Load the synced mirror when it is available; off makes the dialog use its asset fallbacks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Story")
	bool bPreferSyncedMirror = true;

	bool IsCanonical() const { return VolumeSlug.IsEmpty(); }
	/** "canonical", "volume core@1.0" or "asset /Game/..." for logs and the debug menu. */
	FString Describe() const;
	bool operator==(const FProsettaStorySource& Other) const;
	bool operator!=(const FProsettaStorySource& Other) const { return !(*this == Other); }
};

/** Mirror files for one story source, resolved from the sync manifest. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaResolvedStoryPaths
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString MirrorRoot;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString ManifestPath;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString CompiledStoryPath;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString LinesPath;
	/** Folder holding localization.json and the per-language CSVs for this source. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString LocalizationFolder;
	/** Slug the paths belong to; empty for the canonical files. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString VolumeSlug;
	/** Tag the manifest records for that volume, when it lists one. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString VolumeTag;
	/** True when manifest.json was read; false means the canonical layout was assumed. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") bool bFromManifest = false;
	/** A volume was requested but the mirror has no files for it, so the canonical story is used. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") bool bFellBackToCanonical = false;
	/** Why the fallback happened, for logs and the dashboard. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Story") FString Note;

	/** True when the compiled story and the lines file both exist on disk. */
	bool IsComplete() const;
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UProsettaStorySourceHost : public UInterface
{
	GENERATED_BODY()
};

/**
 * The seam between Inkpot and a project's level setup: the actor that decides which Prosetta story a
 * level plays. ProsettaStorySource::ForWorld reads the first host in a world, and the Prosetta Dashboard
 * re-points it when a volume is switched (spawning the class named in Project Settings > Prosetta Sync
 * when the level has none). Implement it on whatever actor already configures your level's dialog.
 */
class INKPOT_API IProsettaStorySourceHost
{
	GENERATED_BODY()
public:
	virtual FProsettaStorySource GetProsettaStorySource() const = 0;
	/**
	 * Points the level at NewSource. Called by the editor inside an undo transaction, so an
	 * implementation should Modify() before changing state and notify its property change.
	 * Returns false when nothing changed.
	 */
	virtual bool SetProsettaStorySource(const FProsettaStorySource& NewSource) = 0;
};

namespace ProsettaStorySource
{
	/** <Project>/Saved/Prosetta, the sync mirror (derived data, read-only). */
	INKPOT_API FString MirrorRoot();
	/** <MirrorRoot>/manifest.json. */
	INKPOT_API FString ManifestPath(const FString& MirrorRoot);
	/** Canonical paths under MirrorRoot: compiled/story.json, lines/lines.json, localization/. */
	INKPOT_API FProsettaResolvedStoryPaths CanonicalPaths(const FString& MirrorRoot);
	/**
	 * Resolves the files for VolumeSlug from an already parsed manifest. Manifest driven only:
	 * a volume maps through the scope's artifact keys, then `compiled-story:<slug>`, then the
	 * canonical files when the manifest's scope is that single volume. Any other volume request
	 * falls back to the canonical files with bFellBackToCanonical set. Paths are never derived
	 * from a `volumes/<slug>/...` pattern. Returns false on a fallback.
	 */
	INKPOT_API bool ResolveStoryPathsFromManifest(const FProsettaManifestSummary& Manifest, const FString& MirrorRoot, const FString& VolumeSlug, FProsettaResolvedStoryPaths& Out);
	/** Loads <MirrorRoot>/manifest.json and resolves; without a manifest the canonical layout is assumed (a fallback when a volume was asked for). Logs a warning on a fallback. */
	INKPOT_API bool ResolveStoryPaths(const FString& MirrorRoot, const FString& VolumeSlug, FProsettaResolvedStoryPaths& Out);
	/** The first valid actor in World that implements IProsettaStorySourceHost, or null. */
	INKPOT_API AActor* FindHost(const UWorld* World);
	/** The story source of World's host (FindHost), or the default source when the level has none. */
	INKPOT_API FProsettaStorySource ForWorld(const UWorld* World);
	/** The compiled story World plays from the mirror (ForWorld + ResolveStoryPaths). */
	INKPOT_API FString CompiledStoryPathForWorld(const UWorld* World);
}
