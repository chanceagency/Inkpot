#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "InkpotProsettaLibrary.generated.h"

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
	 *  supplied an authoritative replacement for the tag's inner text. */
	using FTextResolver = TFunctionRef<bool(const FString& /*LineId*/, FString& /*OutText*/)>;

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
};
