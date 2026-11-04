#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ProsettaSpeakerRegistry.generated.h"

/** Portable identity imported from the verified Engine Sync character registry. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaSpeakerIdentity
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") int32 CharacterId = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString Shortname;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString DisplayName;
};

/** Cookable speaker catalog. Importers populate this from characters/characters.json;
 * runtime scene components never contact the API or read credentials. */
UCLASS(BlueprintType)
class INKPOT_API UProsettaSpeakerRegistry : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") int32 ProjectId = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") TArray<FProsettaSpeakerIdentity> Characters;
 const FProsettaSpeakerIdentity* FindById(int32 Id) const;
 const FProsettaSpeakerIdentity* FindByShortname(const FString& Shortname) const;
 /** Replaces the catalog only for a valid schema with unique identities. */
 bool LoadCharactersJSON(const FString& JSON, FString& OutError);
};
