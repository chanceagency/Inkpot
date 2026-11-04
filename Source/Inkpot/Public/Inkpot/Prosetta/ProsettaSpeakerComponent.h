#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Sound/SoundAttenuation.h"
#include "ProsettaSpeakerComponent.generated.h"
class UAudioComponent;
class USoundBase;
class UProsettaSpeakerRegistry;

/** Incoming line behavior. Kept independent of the native audio backend. */
UENUM(BlueprintType)
enum class EProsettaVoicePolicy : uint8 { Interrupt, Overlap, Queue };

/** Boolean Ink tags. Last recognized tag in a scope wins; absent tags preserve the inherited value. */
struct INKPOT_API FProsettaVoicePolicyTags
{
 static EProsettaVoicePolicy Resolve(const TArray<FString>& Tags, EProsettaVoicePolicy Inherited);
};

/** Assign a synced Prosetta character to an actor and place its voice origin like a mouth/socket.
 * The caller owns each returned audio component and its interruption/pacing policy. */
UCLASS(ClassGroup=(Prosetta), meta=(BlueprintSpawnableComponent, DisplayName="Prosetta Speaker"))
class INKPOT_API UProsettaSpeakerComponent : public USceneComponent
{
 GENERATED_BODY()
public:
 UProsettaSpeakerComponent();
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prosetta|Speaker") TObjectPtr<UProsettaSpeakerRegistry> SpeakerRegistry;
 /** Select from the imported API catalog. Identity is retained if the character is renamed. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prosetta|Speaker", meta=(GetOptions="GetSpeakerOptions")) FString Speaker;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta|Speaker") int32 CharacterId = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Prosetta|Speaker") int32 ProjectId = 0;
 /** Used when a line/stitch/knot has no voice policy tag. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Speaker") EProsettaVoicePolicy VoicePolicy = EProsettaVoicePolicy::Interrupt;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Speaker") bool bSpatialVoiceEnabled = true;
 /** Optional shared Unreal attenuation asset; otherwise use the editable native defaults below. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Audio") TObjectPtr<USoundAttenuation> Attenuation;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Audio", meta=(EditCondition="Attenuation == nullptr")) FSoundAttenuationSettings AttenuationOverrides;
 UFUNCTION(BlueprintPure, Category="Prosetta|Speaker") TArray<FString> GetSpeakerOptions() const;
 UFUNCTION(BlueprintCallable, Category="Prosetta|Speaker") bool SelectSpeaker(const FString& Shortname);
 bool Matches(int32 InProjectId, int32 InCharacterId) const;
 /** Creates a registered, attached, stopped emitter, ready for the caller to bind OnAudioFinished then Play. */
 UFUNCTION(BlueprintCallable, Category="Prosetta|Audio") UAudioComponent* CreateVoice(USoundBase* Sound);
 /** Prefer the conversation actor, otherwise nearest to listener; stable path breaks ties. */
 static UProsettaSpeakerComponent* Resolve(UWorld* World, int32 InProjectId, int32 InCharacterId, AActor* ConversationActor, const FVector& ListenerLocation);
 virtual void OnRegister() override;
#if WITH_EDITOR
 virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
