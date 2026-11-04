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
 Near->UnregisterComponent(); Near->RegisterComponent();
 TestEqual(TEXT("re-registration refreshes the renamed label"), Near->Speaker, FString(TEXT("RENAMED")));
 TestNull(TEXT("no voice without a sound"), Near->CreateVoice(nullptr));
 auto* Sound = NewObject<USoundWaveProcedural>();
 if (World->GetAudioDeviceRaw() && World->AllowAudioPlayback())
 {
  UAudioComponent* Voice = Near->CreateVoice(Sound);
  if (TestNotNull(TEXT("voice created"), Voice))
  {
   TestTrue(TEXT("voice attached to the speaker"), Voice->GetAttachParent() == Near && Voice->GetOwner() == Near->GetOwner());
   TestTrue(TEXT("voice waits for the caller to play"), !Voice->bAutoActivate && !Voice->IsPlaying() && Voice->IsRegistered());
   TestTrue(TEXT("native attenuation applied"), Voice->bOverrideAttenuation && Voice->AttenuationOverrides.FalloffDistance == Near->AttenuationOverrides.FalloffDistance);
   Voice->DestroyComponent();
  }
 }
 else { TestNull(TEXT("no voice without an audio device"), Near->CreateVoice(Sound)); }
 auto* Ownerless = NewObject<UProsettaSpeakerComponent>(World);
 Ownerless->RegisterComponentWithWorld(World);
 TestNull(TEXT("ownerless speaker has no emitter outer"), Ownerless->CreateVoice(Sound));
 Ownerless->UnregisterComponent();
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaSpeakerRegistryValidationTest, "Inkpot.Prosetta.Speakers.RegistryValidation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaSpeakerRegistryValidationTest::RunTest(const FString& Parameters)
{
 auto* Registry = NewObject<UProsettaSpeakerRegistry>();
 FString Error;
 TestTrue(TEXT("trimmed catalog loads"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":7,"shortname":" SPHERE "}]})"), Error));
 const FProsettaSpeakerIdentity* Sphere = Registry->FindById(7);
 TestTrue(TEXT("shortname stored trimmed"), Sphere && Sphere->Shortname == TEXT("SPHERE") && Sphere->DisplayName == TEXT("SPHERE"));
 TestNotNull(TEXT("lookup trims"), Registry->FindByShortname(TEXT(" sphere")));
 TestFalse(TEXT("string ID rejected"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":"8","shortname":"A"}]})"), Error));
 TestFalse(TEXT("fractional ID rejected"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":7.5,"shortname":"A"}]})"), Error));
 TestFalse(TEXT("string project rejected"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":"42","characters":[]})"), Error));
 TestFalse(TEXT("out-of-range ID rejected"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":4294967296,"shortname":"A"}]})"), Error));
 TestFalse(TEXT("whitespace duplicates rejected"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":7,"shortname":"A"},{"id":8,"shortname":" a "}]})"), Error));
 TestFalse(TEXT("blank shortname rejected"), Registry->LoadCharactersJSON(TEXT(R"({"projectId":42,"characters":[{"id":7,"shortname":"  "}]})"), Error));
 TestTrue(TEXT("failed loads keep the catalog"), Registry->ProjectId == 42 && Registry->Characters.Num() == 1);
 return true;
}
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

 // A weave gather is named but is not a knot or stitch: its leading tag belongs to its own line only.
 auto* GatherAsset = NewObject<UInkpotStoryAsset>();
 GatherAsset->SetCompiledJSON(TEXT(R"({"inkVersion":21,"root":[{"->":"Talk"},"done",{"Talk":[["#","^queue","/#","^Intro","\n",{"->":".^.g-0"},{"g-0":["#","^overlap","/#","^Gathered","\n","^Later","\n","end",null]}],null]}],"listDefs":{}})"));
 auto* Gather = InkpotSubsystem->BeginStory(GatherAsset);
 if (!TestNotNull(TEXT("gather fixture story"), Gather)) { return false; }
 Gather->Continue(); Gather->Continue();
 TestTrue(TEXT("gather tag stays on its line"), Gather->GetCurrentLine()->GetTags().Contains(TEXT("overlap")));
 Gather->Continue();
 UInkpotLine* Later = Gather->GetCurrentLine();
 TestEqual(TEXT("later gather line"), Later->GetString().TrimStartAndEnd(), FString(TEXT("Later")));
 TestFalse(TEXT("gather is not a policy scope"), Later->GetInheritedTags().Contains(TEXT("overlap")));
 TestTrue(TEXT("knot scope still inherited"), Later->GetInheritedTags().Contains(TEXT("queue")));
 InkpotSubsystem->EndStory(Gather);

 // A dynamic tag at the top of a knot is skipped without a story error; static tags beside it still count.
 auto* DynamicAsset = NewObject<UInkpotStoryAsset>();
 DynamicAsset->SetCompiledJSON(TEXT(R"({"inkVersion":21,"root":[{"->":"Dyn"},"done",{"Dyn":[["#","^mood_","ev",{"VAR?":"x"},"out","/ev","/#","#","^queue","/#","^Hi","\n","^Again","\n","end",null],null],"global decl":["ev",1,{"VAR=":"x"},"/ev","end",null]}],"listDefs":{}})"));
 auto* Dynamic = InkpotSubsystem->BeginStory(DynamicAsset);
 if (!TestNotNull(TEXT("dynamic tag fixture story"), Dynamic)) { return false; }
 Dynamic->Continue(); Dynamic->Continue();
 UInkpotLine* Again = Dynamic->GetCurrentLine();
 TestEqual(TEXT("second dynamic-knot line"), Again->GetString().TrimStartAndEnd(), FString(TEXT("Again")));
 TestTrue(TEXT("dynamic knot tag skipped, static kept"), Again->GetInheritedTags() == TArray<FString>{TEXT("queue")});
 InkpotSubsystem->EndStory(Dynamic);
 return true;
}
#endif
