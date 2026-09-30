#pragma once

#include "CoreMinimal.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "ProsettaLine.generated.h"

/**
 * UProsettaLine
 * A single hydrated segment parsed out of a fragment of story text. Each segment
 * corresponds to one <prosetta>...</prosetta> tag. It carries:
 *  - the line id and the data attributes declared on the tag,
 *  - the resolved authored metadata for that id (from the sidecar JSON),
 *  - the player-facing display text and where it sits within the cleaned fragment.
 *
 * The UI layer can render the cleaned fragment text and use the segment list to
 * drive per-line behaviour ( audio, animation cues, speaker attribution etc. ).
 */
UCLASS(BlueprintType)
class INKPOT_API UProsettaLine : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Initialise
	 * @param InLineId      the value of the tag's 'id' attribute.
	 * @param InText        the player-facing text for this segment ( already resolved ).
	 * @param InAttributes  the non-id data attributes declared on the tag.
	 * @param InStart       offset of this segment's text within the cleaned fragment.
	 * @param InLength      length of this segment's text within the cleaned fragment.
	 */
	void Initialise(FName InLineId, const FString& InText, const TMap<FName, FString>& InAttributes, int32 InStart, int32 InLength);

	/** Apply the authored metadata resolved for this line's id. */
	void SetMetadata(const FProsettaLineMetadata& InMetadata, bool bInResolved);

	/** The 'id' attribute of the source tag (e.g. "023A"). */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	FName GetLineId() const;

	/** The player-facing text for this segment. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	const FString& GetText() const;

	/** All non-id data attributes declared on the tag ( e.g. animate, sfx ). */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	const TMap<FName, FString>& GetAttributes() const;

	/** Look up a single data attribute by name. Returns true if present. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool GetAttribute(FName InName, FString& OutValue) const;

	/** Whether authored metadata was resolved for this line's id from the sidecar. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool IsResolved() const;

	/** The resolved authored metadata for this line ( valid only when IsResolved() ). */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	const FProsettaLineMetadata& GetMetadata() const;

	/** Offset of this segment's text within the cleaned fragment. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	int32 GetStartOffset() const;

	/** Length of this segment's text within the cleaned fragment. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	int32 GetLength() const;

private:
	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	FName LineId;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	FString Text;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	TMap<FName, FString> Attributes;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	FProsettaLineMetadata Metadata;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	bool bResolved = false;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	int32 StartOffset = 0;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	int32 Length = 0;
};
