#include "Inkpot/Prosetta/ProsettaSpeakerComponent.h"
#include "Inkpot/Prosetta/ProsettaSpeakerRegistry.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sound/SoundBase.h"

UProsettaSpeakerComponent::UProsettaSpeakerComponent()
{
 PrimaryComponentTick.bCanEverTick = false;
 AttenuationOverrides.bAttenuate = true;
 AttenuationOverrides.bSpatialize = true;
 AttenuationOverrides.SpatializationAlgorithm = SPATIALIZATION_Default;
 AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
 AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
 AttenuationOverrides.AttenuationShapeExtents = FVector(100.f, 0.f, 0.f);
 AttenuationOverrides.FalloffDistance = 2400.f;
 AttenuationOverrides.dBAttenuationAtMax = -48.f;
 AttenuationOverrides.bEnableOcclusion = true;
 AttenuationOverrides.OcclusionTraceChannel = ECC_Visibility;
 AttenuationOverrides.OcclusionLowPassFilterFrequency = 2500.f;
 AttenuationOverrides.OcclusionVolumeAttenuation = 0.65f;
 AttenuationOverrides.OcclusionInterpolationTime = 0.15f;
 AttenuationOverrides.StereoSpread = 0.f;
 AttenuationOverrides.bApplyNormalizationToStereoSounds = true;
}
TArray<FString> UProsettaSpeakerComponent::GetSpeakerOptions() const
{
 TArray<FString> Names{FString()};
 if (SpeakerRegistry) { for (const auto& C : SpeakerRegistry->Characters) { Names.Add(C.Shortname); } }
 Names.Sort(); return Names;
}
bool UProsettaSpeakerComponent::SelectSpeaker(const FString& Shortname)
{
 if (Shortname.IsEmpty()) { Speaker.Reset(); CharacterId = ProjectId = 0; return true; }
 const auto* C = SpeakerRegistry ? SpeakerRegistry->FindByShortname(Shortname) : nullptr;
 if (!C) { return false; }
 Speaker = C->Shortname; CharacterId = C->CharacterId; ProjectId = SpeakerRegistry->ProjectId; return true;
}
bool UProsettaSpeakerComponent::Matches(int32 InProjectId, int32 InCharacterId) const
{
 return bSpatialVoiceEnabled && IsRegistered() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
  SpeakerRegistry && ProjectId == SpeakerRegistry->ProjectId && ProjectId == InProjectId &&
  CharacterId > 0 && CharacterId == InCharacterId && SpeakerRegistry->FindById(CharacterId);
}
UAudioComponent* UProsettaSpeakerComponent::CreateVoice(USoundBase* Sound)
{
 UWorld* World = GetWorld();
 if (!Sound || !World || !World->GetAudioDeviceRaw() || !World->AllowAudioPlayback() || !IsRegistered()) { return nullptr; }
 UAudioComponent* Voice = NewObject<UAudioComponent>(GetOwner(), NAME_None, RF_Transient);
 Voice->bAutoActivate = false;
 Voice->bAutoDestroy = true;
 Voice->bStopWhenOwnerDestroyed = true;
 Voice->bAllowSpatialization = true;
 Voice->bIsUISound = false;
 Voice->AttenuationSettings = Attenuation;
 Voice->bOverrideAttenuation = Attenuation == nullptr;
 Voice->AttenuationOverrides = AttenuationOverrides;
 Voice->SetupAttachment(this);
 Voice->SetSound(Sound);
 Voice->RegisterComponentWithWorld(World);
 return Voice;
}
UProsettaSpeakerComponent* UProsettaSpeakerComponent::Resolve(UWorld* World, int32 InProjectId, int32 InCharacterId, AActor* ConversationActor, const FVector& ListenerLocation)
{
 UProsettaSpeakerComponent* Best = nullptr;
 double BestDistance = TNumericLimits<double>::Max();
 if (!World || InProjectId <= 0 || InCharacterId <= 0) { return nullptr; }
 for (TActorIterator<AActor> It(World); It; ++It)
 {
  TInlineComponentArray<UProsettaSpeakerComponent*> Speakers(*It);
  for (auto* Candidate : Speakers)
  {
   if (!Candidate->Matches(InProjectId, InCharacterId)) { continue; }
   const bool Preferred = Candidate->GetOwner() == ConversationActor;
   const bool BestPreferred = Best && Best->GetOwner() == ConversationActor;
   const double Distance = FVector::DistSquared(Candidate->GetComponentLocation(), ListenerLocation);
   if (!Best || (Preferred && !BestPreferred) || (Preferred == BestPreferred &&
       (Distance < BestDistance || (Distance == BestDistance && Candidate->GetPathName() < Best->GetPathName()))))
   { Best = Candidate; BestDistance = Distance; }
  }
 }
 return Best;
}
#if WITH_EDITOR
void UProsettaSpeakerComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
 if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UProsettaSpeakerComponent, Speaker))
 { if (!SelectSpeaker(Speaker)) { Speaker.Reset(); CharacterId = ProjectId = 0; } }
 else if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UProsettaSpeakerComponent, SpeakerRegistry))
 { Speaker.Reset(); CharacterId = ProjectId = 0; }
 Super::PostEditChangeProperty(Event);
}
#endif

EProsettaVoicePolicy FProsettaVoicePolicyTags::Resolve(const TArray<FString>& Tags, EProsettaVoicePolicy Inherited)
{
 for (const FString& Tag : Tags)
 {
  const FString Value = Tag.TrimStartAndEnd().ToLower();
  if (Value == TEXT("interrupt")) { Inherited = EProsettaVoicePolicy::Interrupt; }
  else if (Value == TEXT("overlap")) { Inherited = EProsettaVoicePolicy::Overlap; }
  else if (Value == TEXT("queue")) { Inherited = EProsettaVoicePolicy::Queue; }
 }
 return Inherited;
}
