#pragma once

#include "CoreMinimal.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "Inkpot/Prosetta/ProsettaInlineTemplate.h"
#include "ProsettaMetadata.generated.h"

/**
 * UProsettaMetadata
 * Holds the authored line metadata for a story, parsed from the sidecar Prosetta JSON
 * that is baked onto the story asset at import. Resolves a line id ( the 'id' attribute
 * of a <prosetta> tag ) to its rich FProsettaLineMetadata.
 *
 * Expected JSON shape ( the "lines" map may also be supplied as an array of objects
 * each carrying a "lineId" field matching the tag id; Engine Sync records also carry a
 * database "id", which is only used when "lineId" is absent ):
 * {
 *   "version": 1,
 *   "projectId": 12,
 *   "lines": {
 *     "023A": {
 *       "string": "Honk honk, mothertrucker!",
 *       "kind": "dialogue",
 *       "speaker": "TRUCKER",
 *       "tags": { "mood": "angry" },
 *       "localizations": { "fr": "Tut tut, enfoiré !" }
 *     }
 *   }
 * }
 */
UCLASS(BlueprintType)
class INKPOT_API UProsettaMetadata : public UObject
{
	GENERATED_BODY()

public:
	/** Parse the sidecar JSON. Returns true if at least one line was loaded. */
	bool LoadFromJSON(const FString& InJSON);

	/** Resolve the full metadata for a line id. Returns true when found. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool Resolve(FName InLineId, FProsettaLineMetadata& OutMetadata) const;

	/** Resolve the authoritative string for a line id, verbatim ( any inline Ink
	 *  {expressions} unevaluated ). Returns true when a non-empty string is available. */
	bool ResolveText(FName InLineId, FString& OutText) const;

	/**
	 * ResolveDisplayText
	 * Resolve the player-facing text for a line id: the authoritative string with each
	 * inline Ink {expression} replaced by the value the runtime emitted for it ( recovered
	 * from InRuntimeText ), else its live value from InResolver.
	 * Returns false when no string is available or a placeholder could not be resolved,
	 * in which case the caller should keep the runtime text ( already evaluated by Ink ).
	 *
	 * @param InRuntimeText  the Ink-evaluated text emitted for this line.
	 * @param InResolver     live variable lookup for placeholders not recovered.
	 * @param OutText        the display text.
	 * @param OutValues      the values recovered from the runtime text.
	 */
	bool ResolveDisplayText(FName InLineId, FStringView InRuntimeText, FProsettaInlineTemplate::FVariableResolver InResolver,
		FString& OutText, FProsettaTemplateValues& OutValues) const;

	/** The parsed authoritative string for a line id, or nullptr when it has no {expressions}. */
	const FProsettaInlineTemplate* FindTemplate(FName InLineId) const;

	/**
	 * CollectVariableNames
	 * Fill InOutMetadata.VariableNames from its String and Localizations. LoadFromJSON does
	 * this for every line it reads; UProsettaLine::CaptureVariables does it for metadata
	 * assembled by hand that has not been collected yet.
	 */
	static void CollectVariableNames(FProsettaLineMetadata& InOutMetadata);

	/** Number of lines loaded. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	int32 Num() const;

	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool IsEmpty() const;

private:
	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	TMap<FName, FProsettaLineMetadata> Lines;

	/** Parsed templates for the authoritative strings containing inline expressions. */
	TMap<FName, FProsettaInlineTemplate> Templates;
};
