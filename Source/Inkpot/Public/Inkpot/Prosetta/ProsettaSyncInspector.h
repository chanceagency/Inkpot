#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProsettaSyncInspector.generated.h"

/** One manifest artifact: enough to show what is cached and its content hash. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaArtifactSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Key;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString RelativePath;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Checksum;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int64 ByteSize = 0;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FDateTime UpdatedAt;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") bool bHasUpdatedAt = false;

	/** First 12 hex characters of the checksum, like a short git SHA. */
	FString ShortSha() const { return Checksum.Left(12); }
};

/** One volume of a scoped sync, from the manifest's scope object (or parsed from a legacy "volumes=slug@tag" string). */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaScopeVolumeEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Slug;
	/** Release tag, empty when the volume follows live. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Tag;
	/** Artifact keys the manifest names for this volume; empty means the conventional `<kind>:<slug>` key or the canonical file. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString CompiledStoryArtifactKey;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString LinesArtifactKey;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString LocalizationArtifactKey;

	/** "slug" or "slug@tag". */
	FString Describe() const;
};

/** The manifest's scope, whether it was written as a string or an object. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaScopeSummary
{
	GENERATED_BODY()
	/** Scope parameter such as "volumes=core@1.0,extra"; empty for the live project. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Canonical;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FProsettaScopeVolumeEntry> Volumes;
	/** True when the manifest wrote the scope as an object (newer CLI), false for the legacy string. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") bool bFromObject = false;

	bool IsLive() const { return Volumes.IsEmpty() && Canonical.IsEmpty(); }
	const FProsettaScopeVolumeEntry* FindVolume(const FString& Slug) const;
	/** Comma separated volume descriptions, or "live". */
	FString Describe() const;
	/** Parses "volumes=a@1.0,b" (or "a@1.0,b") into volume entries. */
	static FProsettaScopeSummary FromString(const FString& Scope);
};

/** Parsed manifest.json (schemaVersion 2) from the mirror or a cache/generation directory. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaManifestSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Path;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int32 SchemaVersion = 0;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int32 ProjectId = 0;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ProjectIdentifier;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ProjectName;
	/** Empty for the live project; otherwise the scope parameter such as "volumes=core@1.0". */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Scope;
	/** The scope with its volumes, from either the string or the object form. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FProsettaScopeSummary ScopeDetail;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FDateTime GeneratedAt;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") bool bHasGeneratedAt = false;
	/** Watch generations stamp their provenance into the manifest. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString GenerationId;
	/** The generation's revision: the string form, or the "*" / project entry of the per-namespace map. */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString GenerationRevision;
	/** Every revision the generation records, keyed by namespace ("*" when it was a plain string). */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TMap<FString, FString> GenerationRevisions;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FProsettaArtifactSummary> Artifacts;

	/** Short combined hash over every artifact checksum, stable for identical content. */
	FString ContentSha() const;
	const FProsettaArtifactSummary* FindArtifact(const FString& Key) const;
	/** Volume slugs and release tags from the scope ("volumes=core@1.0,extra" -> core@1.0, extra). */
	TArray<FString> ScopeEntries() const;
};

/** One entry from `prosetta-sync volumes --json`. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaVolumeSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Slug;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Name;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") bool bIsDefault = false;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Status;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ScopeParameter;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FDateTime LastCompiledAt;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") bool bHasLastCompiledAt = false;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int32 WarningCount = 0;
	/** Release tags on the volume ("1.0", "1.1-rc"). */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FString> Tags;
};

USTRUCT(BlueprintType)
struct INKPOT_API FProsettaVolumesListing
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int32 ProjectId = 0;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ProjectIdentifier;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ProjectName;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FProsettaVolumeSummary> Volumes;
};

/**
 * One NDJSON line from `prosetta-sync watch`. Only `generation-completed` is importable;
 * status/error/blocked lines are surfaced for diagnostics and never imported.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaWatchEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int32 SchemaVersion = 0;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Type;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") int32 ProjectId = 0;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Scope;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Channel;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString GenerationId;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString GenerationPath;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ManifestPath;
	/** scope -> revision from the `revision` object, flattened as "scope@rev". */
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FString> Revisions;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ReleaseTagId;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString ReleaseState;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FString> ChangedKeys;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FString> RemovedKeys;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Code;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Message;

	/** Only completed generations may be imported. */
	bool IsImportable() const { return SchemaVersion == 1 && Type == TEXT("generation-completed") && !GenerationPath.IsEmpty(); }
	bool IsError() const { return Type == TEXT("error") || Type == TEXT("blocked"); }
};

/** A completed watch generation found on disk under `<cache>.generations/`. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaGenerationSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Directory;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString GenerationId;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") bool bIsCurrent = false;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FProsettaManifestSummary Manifest;
};

/** A local cache namespace (one per project identifier / scope) plus its watch generations. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaCacheSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Directory;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FString Name;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") bool bHasManifest = false;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") FProsettaManifestSummary Manifest;
	UPROPERTY(BlueprintReadOnly, Category="Prosetta|Sync") TArray<FProsettaGenerationSummary> Generations;
};

/**
 * Pure helpers for reading the local Prosetta sync state. Nothing here talks to the
 * network, and nothing reads or returns a project token: the CLI config is parsed
 * for cachePath/mirrorPath only.
 */
UCLASS()
class INKPOT_API UProsettaSyncInspector : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Prosetta|Sync")
	static bool ParseManifest(const FString& JSON, FProsettaManifestSummary& Out);

	UFUNCTION(BlueprintCallable, Category="Prosetta|Sync")
	static bool LoadManifestFile(const FString& Path, FProsettaManifestSummary& Out);

	UFUNCTION(BlueprintCallable, Category="Prosetta|Sync")
	static bool ParseVolumesListing(const FString& JSON, FProsettaVolumesListing& Out);

	/** Parse one NDJSON line. False for non-JSON lines and unsupported schema versions. */
	UFUNCTION(BlueprintCallable, Category="Prosetta|Sync")
	static bool ParseWatchEvent(const FString& Line, FProsettaWatchEvent& Out);

	/** Replaces a line that mentions a token, bearer or authorization header with a fixed marker; otherwise trims it to 200 characters. For logs and UI. */
	UFUNCTION(BlueprintPure, Category="Prosetta|Sync")
	static FString RedactCredentialLine(const FString& Line);

	/** Human age such as "just now", "5 min ago", "3 h ago", "2 d ago". */
	UFUNCTION(BlueprintPure, Category="Prosetta|Sync")
	static FString DescribeAge(const FDateTime& Then, const FDateTime& Now);

	/** Parse an ISO-8601 timestamp (with or without fractional seconds / Z). */
	static bool ParseTimestamp(const FString& Text, FDateTime& Out);

	/** cachePath / mirrorPath from the CLI config files. Tokens are never read. */
	static bool ReadCliConfigPaths(const FString& ConfigJSON, FString& OutCachePath, FString& OutMirrorPath);

	/** Config file search order used by the CLI, relative to the project directory. */
	static TArray<FString> CliConfigCandidates(const FString& ProjectDir);

	/** Resolve cachePath/mirrorPath from the first readable config; defaults mirror to Saved/Prosetta. */
	static void ResolveLocalPaths(const FString& ProjectDir, FString& OutCachePath, FString& OutMirrorPath);

	/** Scan every cache namespace under the CLI cache root, including watch generations. */
	UFUNCTION(BlueprintCallable, Category="Prosetta|Sync")
	static TArray<FProsettaCacheSummary> ScanCacheRoot(const FString& CacheRoot);

	/** Absolute path to the prosetta-sync executable, or empty when not installed. */
	UFUNCTION(BlueprintPure, Category="Prosetta|Sync")
	static FString ResolveCliPath();
};
