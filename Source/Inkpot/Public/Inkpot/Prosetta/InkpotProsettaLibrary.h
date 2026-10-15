#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "InkpotProsettaLibrary.generated.h"

class UInkpotLine;

/**
 * UInkpotProsettaLibrary
 * Scans fragments of story text for <prosetta>...</prosetta> markup, recovering the
 * line id and data attributes from each tag and producing clean, player-facing text
 * with the markup removed.
 *
 * The runtime ( see UInkpotLine ) drives this so the UI layer receives filtered text
 * alongside the hydrated segment data.
 */
UCLASS()
class INKPOT_API UInkpotProsettaLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** A resolver decides the display text for a given line id, returning true if it
	 *  supplied an authoritative replacement for the tag's inner text. On entry OutText
	 *  holds the tag's runtime inner text ( Ink expressions already evaluated ). */
	using FTextResolver = TFunctionRef<bool(FName /*LineId*/, FString& /*OutText*/)>;

	/**
	 * ParseFragmentResolved
	 * Scans the fragment, and for each <prosetta> tag asks the resolver for the
	 * authoritative display text for the tag's id. When the resolver declines, the
	 * tag's inner text is used instead.
	 */
	static FProsettaParseResult ParseFragmentResolved(const FString& InFragment, FTextResolver InResolver);

	/**
	 * ParseFragment
	 * Scans the fragment using each tag's inner text as the display text.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	static FProsettaParseResult ParseFragment(const FString& InFragment);

	/**
	 * StripTags
	 * Convenience helper returning just the cleaned, player-facing text.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	static FString StripTags(const FString& InFragment);

	/**
	 * LoadSyncManifestProvenance
	 * Reads story provenance ( project, scope, volume, generation, revision, build id ) from
	 * a Prosetta sync mirror or generation manifest.json, e.g. <Project>/Saved/Prosetta/manifest.json.
	 * Pass a volume slug to pick that volume's compiled story in a multi-volume manifest.
	 * Set the result on a UInkpotStoryAsset built from the mirror so its stories log it.
	 */
	UFUNCTION(BlueprintCallable, Category="Inkpot|Prosetta")
	static bool LoadSyncManifestProvenance(const FString& ManifestFilename, const FString& VolumeSlug, FProsettaStoryProvenance& OutProvenance);

	/**
	 * ParsePauseTag
	 * Parses one Ink line tag of the form "pause:<seconds>" or "wait:<seconds>" ( synonyms ), the
	 * time a player waits after the line finishes ( voice and text reveal ) before the next line.
	 * The key matches without regard to case; whitespace around the key, colon and value is
	 * ignored ( "Pause : 0.2" ), and so is a '#' that is the tag's very first character ( " #pause:1"
	 * is rejected ). The value must be a plain non-negative decimal: digits with an optional
	 * fractional part, or a leading '.' ( "0", ".5", "1.2" ).
	 * Signs, exponents, a trailing '.', units and anything too large for a double are rejected.
	 * Matches the Prosetta web player's rules.
	 * @return true and the seconds when the tag is a valid pause tag.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	static bool ParsePauseTag(const FString& Tag, double& OutSeconds);

	/**
	 * FindPauseSeconds
	 * The pause override among a line's tags. Invalid pause tags are ignored; when several valid
	 * ones are present the last wins.
	 * @return true when any tag is a valid pause tag.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	static bool FindPauseSeconds(const TArray<FString>& Tags, double& OutSeconds);

	/**
	 * GetLinePauseSeconds
	 * FindPauseSeconds over a line's tags. Line tags are read from the story's current tags, so
	 * call this while the line is the story's current line.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	static bool GetLinePauseSeconds(const UInkpotLine* Line, double& OutSeconds);

	/** The default sync mirror manifest: <Project>/Saved/Prosetta/manifest.json. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	static FString GetDefaultSyncManifestPath();

	/** Compact one-line form of a provenance record, as used in Inkpot's log prefixes. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	static FString ProvenanceToLogString(const FProsettaStoryProvenance& Provenance);
};
