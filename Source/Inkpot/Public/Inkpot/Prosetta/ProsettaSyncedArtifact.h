#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sound/SoundWave.h"
#include "ProsettaSyncedArtifact.generated.h"

/** Stable cooked identity for a Prosetta supporting artifact or selected audio take. */
UCLASS(BlueprintType)
class INKPOT_API UProsettaSyncedArtifact : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString Key;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString Namespace;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString Checksum;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString Text;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") TObjectPtr<USoundWave> Audio;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") TMap<FString, FString> Translations;
 UFUNCTION(BlueprintPure, Category="Prosetta") bool ResolveLocalizedText(FName LineId, const FString& LanguageCode, FString& OutText) const
 {
  const FString* text = Translations.Find(LanguageCode + TEXT("/") + LineId.ToString());
  if (!text) return false;
  OutText = *text;
  return true;
 }
};

/** Owns only plugin-created identities. Native files remain outside cooked assets. */
UCLASS(BlueprintType)
class INKPOT_API UProsettaGenerationAsset : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString Namespace;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString GenerationId;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") FString Provenance;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") TMap<FString, TObjectPtr<UObject>> Assets;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta") TMap<FString, FString> Checksums;
#if WITH_EDITORONLY_DATA
 // Retain obsolete owned identities when project references make deletion unsafe.
 UPROPERTY(VisibleAnywhere, Category="Prosetta") TMap<FString, TObjectPtr<UObject>> RetiredAssets;
#endif
};
