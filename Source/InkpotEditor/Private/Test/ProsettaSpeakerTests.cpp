#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Inkpot/Prosetta/ProsettaSpeakerRegistry.h"
#include "Inkpot/Prosetta/ProsettaSpeakerComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWaveProcedural.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaSpeakerTest, "Inkpot.Prosetta.Speakers.IdentityAndRouting",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaSpeakerTest::RunTest(const FString& Parameters)
{
 auto* Registry = NewObject<UProsettaSpeakerRegistry>();
 TestTrue(TEXT("line boolean overrides inherited policy"), FProsettaVoicePolicyTags::Resolve({TEXT("queue"),TEXT("interrupt")}, EProsettaVoicePolicy::Overlap) == EProsettaVoicePolicy::Interrupt);
 TestTrue(TEXT("unrelated tags preserve policy"), FProsettaVoicePolicyTags::Resolve({TEXT("pause:1")}, EProsettaVoicePolicy::Queue) == EProsettaVoicePolicy::Queue);
 FString Error;
 TestTrue(TEXT("API catalog loads"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":7,"shortname":"SPHERE","longName":"The Sphere"}]})"), Error));
 TestFalse(TEXT("unsupported schema rejected"), Registry->LoadCharactersJSON(TEXT(R"({"schemaVersion":99,"projectId":42,"characters":[]})"), Error));
 TestNotNull(TEXT("failed load retains previous registry"), Registry->FindById(7));
 TestFalse(TEXT("duplicates rejected"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":7,"shortname":"A"},{"id":7,"shortname":"B"}]})"), Error));
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 auto MakeSource = [&](FVector Position)
 {
  auto* Actor = World->SpawnActor<AActor>();
  auto* Source = NewObject<UProsettaSpeakerComponent>(Actor);
  Actor->SetRootComponent(Source); Actor->AddInstanceComponent(Source); Source->RegisterComponent();
  Source->SetWorldLocation(Position); Source->SpeakerRegistry = Registry;
  TestTrue(TEXT("speaker picker binds ID"), Source->SelectSpeaker(TEXT("sphere")));
  return Source;
 };
 auto* Near = MakeSource(FVector(100,0,0));
 auto* Far = MakeSource(FVector(1000,0,0));
 TestTrue(TEXT("picker uses API shortnames"), Near->GetSpeakerOptions().Contains(TEXT("SPHERE")));
 TestTrue(TEXT("nearest source"), UProsettaSpeakerComponent::Resolve(World,42,7,nullptr,FVector::ZeroVector) == Near);
 TestTrue(TEXT("conversation object overrides nearest"), UProsettaSpeakerComponent::Resolve(World,42,7,Far->GetOwner(),FVector::ZeroVector) == Far);
 TestNull(TEXT("cross-project IDs never match"), UProsettaSpeakerComponent::Resolve(World,43,7,nullptr,FVector::ZeroVector));
 TestNull(TEXT("unmapped narrator remains unmapped"), UProsettaSpeakerComponent::Resolve(World,42,8,nullptr,FVector::ZeroVector));
 Registry->Characters[0].Shortname = TEXT("RENAMED");
 TestTrue(TEXT("rename keeps mapping"), Near->Matches(42,7));
 Near->bSpatialVoiceEnabled = false;
 TestTrue(TEXT("disabled emitter ignored"), UProsettaSpeakerComponent::Resolve(World,42,7,nullptr,FVector::ZeroVector) == Far);
 TestTrue(TEXT("native panning without plugins"), Far->AttenuationOverrides.SpatializationAlgorithm == SPATIALIZATION_Default);
 TestTrue(TEXT("distance attenuates"), Far->AttenuationOverrides.bAttenuate && Far->AttenuationOverrides.bSpatialize);
 TestTrue(TEXT("falloff quieter at distance"), Far->AttenuationOverrides.Evaluate(FTransform::Identity, FVector(2000,0,0), 1.f) < Far->AttenuationOverrides.Evaluate(FTransform::Identity, FVector(100,0,0), 1.f));
 Far->GetOwner()->Destroy();
 TestNull(TEXT("destroyed emitter ignored"), UProsettaSpeakerComponent::Resolve(World,42,7,nullptr,FVector::ZeroVector));
 World->DestroyWorld(false);
 return true;
}
#endif
#if WITH_DEV_AUTOMATION_TESTS
#include "Asset/InkpotStoryAsset.h"
#include "Inkpot/Inkpot.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/InkpotLine.h"
#include "Engine/Engine.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaVoiceScopeTest, "Inkpot.Prosetta.Speakers.PolicyScopes",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaVoiceScopeTest::RunTest(const FString& Parameters)
{
 auto* InkpotSubsystem = GEngine->GetEngineSubsystem<UInkpot>();
 auto* Asset = NewObject<UInkpotStoryAsset>();
 Asset->SetCompiledJSON(TEXT(R"({"inkVersion":21,"root":[{"->":"Talk.First"},"done",{"Talk":["#","^queue","/#",{"First":["#","^overlap","/#","^Hello","#","^interrupt","/#","\n","^Second","\n","end",{"#f":5}],"#f":1}]}],"listDefs":{}})"));
 auto* Story = InkpotSubsystem->BeginStory(Asset);
 if (!TestNotNull(TEXT("scope fixture story"), Story)) { return false; }
 Story->Continue();
 auto* First = Story->GetCurrentLine();
 TestTrue(TEXT("knot policy captured"), First->GetInheritedTags().Contains(TEXT("queue")));
 TestTrue(TEXT("stitch overrides knot"), FProsettaVoicePolicyTags::Resolve(First->GetInheritedTags(), EProsettaVoicePolicy::Interrupt) == EProsettaVoicePolicy::Overlap);
 TestTrue(TEXT("line overrides stitch"), FProsettaVoicePolicyTags::Resolve(First->GetTags(), EProsettaVoicePolicy::Overlap) == EProsettaVoicePolicy::Interrupt);
 Story->Continue();
 auto* Second = Story->GetCurrentLine();
 TestTrue(TEXT("scope persists beyond first line"), FProsettaVoicePolicyTags::Resolve(Second->GetInheritedTags(), EProsettaVoicePolicy::Interrupt) == EProsettaVoicePolicy::Overlap);
 TestTrue(TEXT("old line keeps own policy"), First->GetTags().Contains(TEXT("interrupt")));
 InkpotSubsystem->EndStory(Story);
 return true;
}
#endif
