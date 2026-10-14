#pragma once

#include "CoreMinimal.h"
#include "ProsettaTypes.generated.h"

/**
 * FProsettaLineMetadata
 * The rich, authored metadata for a single identified line, as resolved from the
 * sidecar Prosetta JSON via its line id. Mirrors the subset of the Prosetta
 * 'ProseLine' record that is useful to the runtime / UI layer.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaLineMetadata
{
	GENERATED_BODY()

	/** The 4 character base-35 line id this metadata was keyed by (e.g. "023A"). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FName LineId;

	/** The authoritative, player-facing line text (ProseLine.string). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString String;

	/** The kind of line (e.g. dialogue, narration, choice). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Kind;

	/** The speaking character's shortname, if any. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString SpeakerShortname;

	/** Arbitrary authored tags attached to the line (from the ProseLine 'tags' map). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TMap<FString, FString> Tags;

	/** Localised variants of the line text, keyed by language code. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TMap<FString, FString> Localizations;
};

/**
 * FProsettaParsedSegment
 * One <prosetta> tag recovered from a fragment, with its resolved display text and
 * the location of that text within the cleaned fragment.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaParsedSegment
{
	GENERATED_BODY()

	/** The value of the tag's 'id' attribute. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FName LineId;

	/** The display text chosen for this segment ( resolved string, else tag inner text ). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Text;

	/** The tag's inner text as evaluated by the Ink runtime, with nested markup removed.
	 *  Inline expressions ( e.g. {2+2} ) have already been evaluated in this text. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString RuntimeText;

	/** The non-id data attributes declared on the tag. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TMap<FName, FString> Attributes;

	/** Offset of Text within the cleaned fragment. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 StartOffset = 0;

	/** Length of Text within the cleaned fragment. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 Length = 0;
};

/**
 * FProsettaParseResult
 * The outcome of scanning a fragment: the cleaned, player-facing text with all tag
 * markup removed, plus the ordered list of identified segments.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaParseResult
{
	GENERATED_BODY()

	/** The fragment with all <prosetta> markup stripped, ready for display. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString CleanText;

	/** The identified segments, in the order they appear in the fragment. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FProsettaParsedSegment> Segments;

	/** True if at least one <prosetta> tag was found in the fragment. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	bool bHasTags = false;
};

/**
 * FProsettaStoryProvenance
 * Where a compiled story came from in Prosetta: project, sync channel / scope, volume,
 * generation and revision, release tag and the compiled story's content hash. Set on the
 * UInkpotStoryAsset by the Prosetta watch import, by the compiled JSON importer, or by
 * game code that builds assets from a sync mirror; copied to the UInkpotStory when it
 * begins and prefixed to Inkpot's story logs so an error can be traced to an exact build.
 * Every field is optional; ToLogString includes only the ones that are present.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaStoryProvenance
{
	GENERATED_BODY()

	/** Prosetta project id ( 0 when unknown ). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	int32 ProjectId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString ProjectIdentifier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString ProjectName;

	/** Sync channel: live or latest-release. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString Channel;

	/** Canonical scope string of the sync ( e.g. the volume selector ). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString Scope;

	/** Volume slug this story was compiled from, when the sync was volume scoped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString VolumeSlug;

	/** Release tag selected for the volume, when any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString VolumeTag;

	/** Watch namespace the asset was imported under ( Project<id>/<Live|Release>_<scope-hash> ). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString Namespace;

	/** Immutable generation id ( sha256 hex ) of the CLI generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString GenerationId;

	/** Project revision captured by the generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString Revision;

	/** Release tag id, for latest-release syncs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString ReleaseTagId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString ReleasePublishedAt;

	/** sha256 hex of the compiled story artifact bytes ( the volume index's buildId ). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString BuildId;

	/** When the sync manifest or compiled payload was generated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inkpot|Prosetta")
	FString GeneratedAt;

	/** True when any identifying field is present. */
	bool IsSet() const;

	/** Compact one-line form for log prefixes, e.g. "Prosetta project 42 live core@17 gen 9f3c1a2b build 77aa01b2". */
	FString ToLogString() const;

	/**
	 * Reads provenance from an Engine Sync v2 manifest document ( the CLI's manifest.json
	 * in a sync mirror or a completed generation ) and, optionally, the watch event that
	 * delivered it. InVolumeSlug selects the compiled story artifact in a multi-volume
	 * manifest; empty picks the single compiled-story artifact when there is one.
	 */
	static bool FromSyncManifest(const TSharedPtr<class FJsonObject>& InManifest, const TSharedPtr<class FJsonObject>& InWatchEvent, const FString& InVolumeSlug, FProsettaStoryProvenance& OutProvenance);
	static bool FromSyncManifestJSON(const FString& InManifestJSON, const FString& InVolumeSlug, FProsettaStoryProvenance& OutProvenance);
	static bool FromSyncManifestFile(const FString& InManifestFilename, const FString& InVolumeSlug, FProsettaStoryProvenance& OutProvenance);

	/** Reads projectName / compiledAt from a /compiled payload wrapper ( {"projectName","compiledAt","story"} ). */
	static bool FromCompiledWrapper(const FString& InWrapperJSON, FProsettaStoryProvenance& OutProvenance);
};
