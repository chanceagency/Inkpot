#pragma once

#include "CoreMinimal.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "ProsettaMetadata.generated.h"

/**
 * UProsettaMetadata
 * Holds the authored line metadata for a story, parsed from the sidecar Prosetta JSON
 * that is baked onto the story asset at import. Resolves a line id ( the 'id' attribute
 * of a <prosetta> tag ) to its rich FProsettaLineMetadata.
 *
 * Expected JSON shape ( the "lines" map may also be supplied as an array of objects
 * each carrying an "id"/"lineId" field ):
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

	/** Resolve the authoritative display string for a line id. Returns true when a
	 *  non-empty string is available. */
	bool ResolveText(FName InLineId, FString& OutText) const;

	/** Number of lines loaded. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	int32 Num() const;

	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool IsEmpty() const;

private:
	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	TMap<FName, FProsettaLineMetadata> Lines;
};
