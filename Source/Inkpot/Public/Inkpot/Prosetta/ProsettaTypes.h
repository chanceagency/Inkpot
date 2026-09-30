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
