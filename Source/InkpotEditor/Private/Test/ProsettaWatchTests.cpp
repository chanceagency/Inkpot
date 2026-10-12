#if !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Prosetta/ProsettaWatchSubsystem.h"
#include "Prosetta/ProsettaChecksum.h"
#include "Inkpot/Prosetta/ProsettaSyncedArtifact.h"
#include "Asset/InkpotStoryAsset.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace
{
 FString SHA(const void* Data, uint32 Size)
 {
  return ProsettaChecksum::SHA256(Data, Size);
 }
 FString SHAString(const FString& Value)
 {
  FTCHARToUTF8 utf8(*Value);
  return SHA(utf8.Get(), utf8.Length());
 }
 FString Json(const TSharedPtr<FJsonObject>& Object)
 {
  FString result;
  FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&result));
  return result;
 }
 FString PackageFile(UObject* Asset)
 {
  return FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
 }
 bool SaveUserFixture(UObject* Asset)
 {
  FAssetRegistryModule::AssetCreated(Asset);
  FSavePackageArgs args;
  args.TopLevelFlags = RF_Public | RF_Standalone;
  if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *PackageFile(Asset), args)) return false;
  FAssetRegistryModule::GetRegistry().ScanModifiedAssetFiles({PackageFile(Asset)});
  return true;
 }
 UProsettaGenerationAsset* UserFixture(const FString& Name, UObject* Reference = nullptr)
 {
  UPackage* package = CreatePackage(*(TEXT("/Game/ProsettaUserFixtures/") + Name));
  UProsettaGenerationAsset* user = NewObject<UProsettaGenerationAsset>(package, *Name, RF_Public | RF_Standalone);
  user->Namespace = TEXT("UserContent");
  if (Reference) user->Assets.Add(TEXT("UserReference"), Reference);
  return user;
 }
 struct FFixture
 {
  FString Root;
  FString Event;
  FString Namespace;
 };
 FFixture Generation(const FString& Scope, const FString& Revision, const FString& Channel, bool Audio = true, bool Corrupt = false)
 {
  FFixture result;
  const FString id = SHAString(Scope + Revision + Channel);
  result.Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("WatchFixtures") / id);
  result.Namespace = FString(TEXT("Project199999/")) + (Channel == TEXT("live") ? TEXT("Live_") : TEXT("Release_")) + SHAString(Scope);
  TArray<TSharedPtr<FJsonValue>> artifacts;
  auto add = [&result, &artifacts](const FString& Key, const FString& Relative, const TArray<uint8>& Bytes, const FString& Kind)
  {
   const FString path = result.Root / Relative;
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(path), true);
   FFileHelper::SaveArrayToFile(Bytes, *path);
   auto descriptor = MakeShared<FJsonObject>();
   descriptor->SetStringField(TEXT("key"), Key);
   descriptor->SetStringField(TEXT("kind"), Kind);
   descriptor->SetStringField(TEXT("relativePath"), Relative);
   descriptor->SetStringField(TEXT("checksum"), SHA(Bytes.GetData(), Bytes.Num()));
   descriptor->SetStringField(TEXT("checksumAlgorithm"), TEXT("sha256"));
   descriptor->SetNumberField(TEXT("byteSize"), Bytes.Num());
   artifacts.Add(MakeShared<FJsonValueObject>(descriptor));
  };
  auto text = [&add](const FString& Key, const FString& Relative, const FString& Text, const FString& Kind = TEXT("json"))
  {
   FTCHARToUTF8 utf8(*Text);
   TArray<uint8> bytes;
   bytes.Append(reinterpret_cast<const uint8*>(utf8.Get()), utf8.Length());
   add(Key, Relative, bytes, Kind);
  };
  text(TEXT("compiled-story"), TEXT("compiled/story.json"), TEXT("{\"inkVersion\":21,\"root\":[\"^Generation ") + Revision + TEXT("\",\"\\n\",\"done\",null],\"listDefs\":{}}"));
  text(TEXT("lines"), TEXT("lines/lines.json"), TEXT("{\"lines\":{\"023A\":{\"string\":\"Authored\"}}}"));
  text(TEXT("localization"), TEXT("localization/localization.json"), TEXT("{\"containers\":[{\"lineId\":\"023A\",\"localizedContainers\":[{\"languageCode\":\"fr\",\"localizedString\":\"Bonjour ") + Revision + TEXT("\"}]}]}"));
  text(TEXT("native-binding:cpp/Test"), TEXT("native-bindings/cpp/Test.h"), TEXT("// staged C++\n"), TEXT("binary"));
  if (Audio)
  {
   // 16-bit mono 8 kHz silence WAV; native SoundFactory is exercised.
   TArray<uint8> wav;
   auto append = [&wav](uint32 Value, int32 Count) { for (int32 i = 0; i < Count; ++i) wav.Add((Value >> (8 * i)) & 255); };
   const auto tag = [&wav](const char* Value) { for (int32 i = 0; i < 4; ++i) wav.Add(Value[i]); };
   tag("RIFF"); append(36 + 1600, 4); tag("WAVE"); tag("fmt "); append(16, 4); append(1, 2); append(1, 2);
   append(8000, 4); append(16000, 4); append(2, 2); append(16, 2); tag("data"); append(1600, 4);
   wav.AddZeroed(1600);
   wav[44] = static_cast<uint8>(FCString::Atoi(*Revision));
   add(TEXT("audio-file:take1"), TEXT("audio/take1.wav"), wav, TEXT("binary"));
  }
  auto provenance = MakeShared<FJsonObject>();
  provenance->SetStringField(TEXT("id"), id);
  auto revisions = MakeShared<FJsonObject>();
  revisions->SetStringField(TEXT("42"), Revision);
  provenance->SetObjectField(TEXT("revision"), revisions);
  auto manifest = MakeShared<FJsonObject>();
  manifest->SetNumberField(TEXT("schemaVersion"), 2);
  manifest->SetNumberField(TEXT("projectId"), 199999);
  manifest->SetObjectField(TEXT("generation"), provenance);
  manifest->SetArrayField(TEXT("artifacts"), artifacts);
  FFileHelper::SaveStringToFile(Json(manifest), *(result.Root / TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  if (Corrupt) FFileHelper::SaveStringToFile(TEXT("corrupt"), *(result.Root / TEXT("compiled/story.json")));
  auto event = MakeShared<FJsonObject>();
  event->SetNumberField(TEXT("schemaVersion"), 1);
  event->SetStringField(TEXT("type"), TEXT("generation-completed"));
  event->SetStringField(TEXT("generationId"), id);
  event->SetStringField(TEXT("generationPath"), result.Root);
  event->SetStringField(TEXT("manifestPath"), result.Root / TEXT("manifest.json"));
  event->SetNumberField(TEXT("projectId"), 199999);
  event->SetStringField(TEXT("scope"), Scope);
  event->SetStringField(TEXT("channel"), Channel);
  event->SetObjectField(TEXT("revision"), revisions);
  event->SetObjectField(TEXT("provenance"), provenance);
  result.Event = Json(event);
  return result;
 }
 UProsettaGenerationAsset* State(const FFixture& Fixture)
 {
  return LoadObject<UProsettaGenerationAsset>(nullptr, *(TEXT("/Game/Prosetta/") + Fixture.Namespace + TEXT("/Generation.Generation")));
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaWatchTest, "Inkpot.Prosetta.Watch",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaWatchTest::RunTest(const FString& InParameters)
{
 TestEqual(TEXT("SHA256 known empty vector"), SHA(nullptr, 0), FString(TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")));
 const FString scope = TEXT("test-") + FGuid::NewGuid().ToString();
 UProsettaWatchSubsystem* watch = NewObject<UProsettaWatchSubsystem>();
 const FFixture first = Generation(scope, TEXT("1"), TEXT("live"));
 const FFixture second = Generation(scope, TEXT("2"), TEXT("live"));
 TestTrue(TEXT("first complete event queued"), watch->ConsumeEvent(first.Event));
 UWorld* oldWorld = GEditor->PlayWorld;
 GEditor->PlayWorld = NewObject<UWorld>();
 TestFalse(TEXT("Play defers imports"), watch->TryApplyPending());
 TestNull(TEXT("no published assets created during Play"), FindObject<UProsettaGenerationAsset>(nullptr, *(TEXT("/Game/Prosetta/") + first.Namespace + TEXT("/Generation.Generation"))));
 TestEqual(TEXT("queued during Play"), watch->GetPendingCount(), 1);
 TestTrue(TEXT("new generation supersedes during Play"), watch->ConsumeEvent(second.Event));
 TestTrue(TEXT("old event accepted without rollback"), watch->ConsumeEvent(first.Event));
 GEditor->PlayWorld = oldWorld;
 TestTrue(TEXT("one latest generation applied after Play"), watch->TryApplyPending());
 UProsettaGenerationAsset* state = State(second);
 if (!TestNotNull(TEXT("stable ownership registry created"), state)) return false;
 UInkpotStoryAsset* story = Cast<UInkpotStoryAsset>(state->Assets.FindRef(TEXT("compiled-story")));
 UProsettaSyncedArtifact* localized = Cast<UProsettaSyncedArtifact>(state->Assets.FindRef(TEXT("localization")));
 UProsettaSyncedArtifact* audio = Cast<UProsettaSyncedArtifact>(state->Assets.FindRef(TEXT("audio-file:take1")));
 if (!TestNotNull(TEXT("compiled story exists"), story) || !TestNotNull(TEXT("localization asset exists"), localized) || !TestNotNull(TEXT("selected audio asset exists"), audio)) return false;
 TestTrue(TEXT("latest queued story used"), story->GetCompiledJSON().Contains(TEXT("Generation 2")));
 TestTrue(TEXT("latest localization used"), localized->Text.Contains(TEXT("Bonjour 2")));
 TestNotNull(TEXT("native SoundWave held for cooking"), audio->Audio.Get());
 TestFalse(TEXT("baked audio is not transient"), audio->Audio->HasAnyFlags(RF_Transient));
 TSoftObjectPtr<UInkpotStoryAsset> storyReference(story);
 TSoftObjectPtr<UProsettaSyncedArtifact> localizationReference(localized), audioReference(audio);
 const FFixture third = Generation(scope, TEXT("3"), TEXT("live"));
 watch->ConsumeEvent(third.Event);
 TestTrue(TEXT("changed generation applied"), watch->TryApplyPending());
 TestEqual(TEXT("story identity stable"), storyReference.Get(), story);
 TestEqual(TEXT("localization identity stable"), localizationReference.Get(), localized);
 TestEqual(TEXT("audio identity stable"), audioReference.Get(), audio);
 TestTrue(TEXT("localization changes"), localized->Text.Contains(TEXT("Bonjour 3")));
 FString translated;
 TestTrue(TEXT("runtime localization lookup"), localized->ResolveLocalizedText(FName(TEXT("023A")), TEXT("fr"), translated));
 TestEqual(TEXT("runtime lookup sees updated translation"), translated, FString(TEXT("Bonjour 3")));
 const FFixture corrupt = Generation(scope, TEXT("4"), TEXT("live"), true, true);
 watch->ConsumeEvent(corrupt.Event);
 TestFalse(TEXT("corrupt complete generation fails"), watch->TryApplyPending());
 TestTrue(TEXT("previous story retained"), story->GetCompiledJSON().Contains(TEXT("Generation 3")));
 TestTrue(TEXT("previous localization retained"), localized->Text.Contains(TEXT("Bonjour 3")));
 TestTrue(TEXT("error visible"), watch->GetStatus().Contains(TEXT("checksum")));
 const FString nativeDestination = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NativeFixture") / scope);
 TestFalse(TEXT("native source never automatically copied"), FPaths::FileExists(nativeDestination / TEXT("Test.h")));
 TestTrue(TEXT("explicit native import"), watch->ImportNativeSources(third.Namespace, nativeDestination));
 TestTrue(TEXT("explicit source file exists"), FPaths::FileExists(nativeDestination / TEXT("Test.h")));
 const FFixture frozen = Generation(scope, TEXT("3"), TEXT("latest-release"));
 watch->ConsumeEvent(frozen.Event);
 TestTrue(TEXT("frozen applies separately"), watch->TryApplyPending());
 UProsettaGenerationAsset* release = State(frozen);
 TestTrue(TEXT("frozen identity separate"), release && release != state && release->Assets.FindRef(TEXT("compiled-story")).Get() != story);
 TStrongObjectPtr<UProsettaGenerationAsset> userReference(UserFixture(TEXT("Reference_") + SHAString(scope), audio));
 TestTrue(TEXT("external user audio reference saved"), SaveUserFixture(userReference.Get()));
 const FFixture removal = Generation(scope, TEXT("5"), TEXT("live"), false);
 watch->ConsumeEvent(removal.Event);
 TestTrue(TEXT("owned audio removal applies"), watch->TryApplyPending());
 TestFalse(TEXT("obsolete owned audio removed from registry"), state->Assets.Contains(TEXT("audio-file:take1")));
 TestEqual(TEXT("referenced retired identity retained"), state->RetiredAssets.FindRef(TEXT("audio-file:take1")).Get(), static_cast<UObject*>(audio));
 TestNotNull(TEXT("retired audio references remain usable"), audio->Audio.Get());
 const FFixture reappeared = Generation(scope, TEXT("6"), TEXT("live"));
 watch->ConsumeEvent(reappeared.Event);
 TestTrue(TEXT("reappeared selected audio applies"), watch->TryApplyPending());
 TestEqual(TEXT("reappeared audio restores original identity"), state->Assets.FindRef(TEXT("audio-file:take1")).Get(), static_cast<UObject*>(audio));
 watch->ConsumeEvent(TEXT("{\"schemaVersion\":1,\"type\":\"blocked\"}"));
 TestEqual(TEXT("blocked event does not queue import"), watch->GetPendingCount(), 0);
 TestTrue(TEXT("blocked retains story"), story->GetCompiledJSON().Contains(TEXT("Generation 6")));
 // Keep ignored fixture packages for the cold-load persistence verification.
 FFileHelper::SaveStringToFile(TEXT("/Game/Prosetta/") + frozen.Namespace + TEXT("/Generation.Generation"), *(FPaths::ProjectSavedDir() / TEXT("WatchPersistence.txt")));
 IFileManager::Get().DeleteDirectory(*nativeDestination, false, true);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaWatchOwnedRemovalTest, "Inkpot.Prosetta.WatchOwnedRemoval",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaWatchOwnedRemovalTest::RunTest(const FString& InParameters)
{
 TStrongObjectPtr<UProsettaWatchSubsystem> watch(NewObject<UProsettaWatchSubsystem>());
 const FString scope = TEXT("removal-") + FGuid::NewGuid().ToString();
 const FFixture unused = Generation(scope, TEXT("1"), TEXT("live"));
 watch->ConsumeEvent(unused.Event);
 if (!TestTrue(TEXT("unreferenced fixture imports"), watch->TryApplyPending())) return false;
 TStrongObjectPtr<UProsettaGenerationAsset> unusedState(State(unused));
 UObject* unusedAudio = unusedState->Assets.FindRef(TEXT("audio-file:take1"));
 if (!TestNotNull(TEXT("unreferenced audio exists before removal"), unusedAudio)) return false;
 const FString removedPackage = PackageFile(unusedAudio);
 const TWeakObjectPtr<UObject> deletedAudio(unusedAudio);
 unusedAudio = nullptr; // Do not keep a GC-visible reference to a candidate expected to delete.
 TStrongObjectPtr<UProsettaGenerationAsset> unrelated(UserFixture(TEXT("Unrelated_") + SHAString(scope)));
 TestTrue(TEXT("unrelated user package saved"), SaveUserFixture(unrelated.Get()));
 const FString unrelatedPackage = PackageFile(unrelated.Get());
 const FFixture removal = Generation(scope, TEXT("2"), TEXT("live"), false);
 watch->ConsumeEvent(removal.Event);
 TestTrue(TEXT("obsolete unreferenced generation applies"), watch->TryApplyPending());
 TestFalse(TEXT("unreferenced owned package physically removed"), FPaths::FileExists(removedPackage));
 TestFalse(TEXT("unreferenced obsolete identity destroyed"), deletedAudio.IsValid());
 TestFalse(TEXT("deleted key not retained"), unusedState->RetiredAssets.Contains(TEXT("audio-file:take1")));
 TestTrue(TEXT("unrelated user package preserved"), FPaths::FileExists(unrelatedPackage));
 TestTrue(TEXT("unrelated user object preserved"), unrelated.IsValid());
 AddInfo(watch->GetStatus());

 const FFixture referenced = Generation(scope + TEXT("-referenced"), TEXT("1"), TEXT("live"));
 watch->ConsumeEvent(referenced.Event);
 if (!TestTrue(TEXT("referenced fixture imports"), watch->TryApplyPending())) return false;
 TStrongObjectPtr<UProsettaGenerationAsset> referencedState(State(referenced));
 UObject* referencedAudio = referencedState->Assets.FindRef(TEXT("audio-file:take1"));
 if (!TestNotNull(TEXT("referenced audio exists before removal"), referencedAudio)) return false;
 const FString retainedPackage = PackageFile(referencedAudio);
 TStrongObjectPtr<UProsettaGenerationAsset> user(UserFixture(TEXT("External_") + SHAString(scope), referencedAudio));
 TestTrue(TEXT("external user reference saved"), SaveUserFixture(user.Get()));
 const TWeakObjectPtr<UObject> retainedTarget(referencedAudio);
 user->Assets.Empty(); // Keep the saved disk edge but remove the memory edge for this scan.
 const FFixture referencedRemoval = Generation(scope + TEXT("-referenced"), TEXT("2"), TEXT("live"), false);
 watch->ConsumeEvent(referencedRemoval.Event);
 TestTrue(TEXT("referenced obsolete generation applies"), watch->TryApplyPending());
 TestFalse(TEXT("referenced obsolete key leaves active registry"), referencedState->Assets.Contains(TEXT("audio-file:take1")));
 TestEqual(TEXT("external referenced identity retired"), referencedState->RetiredAssets.FindRef(TEXT("audio-file:take1")).Get(), referencedAudio);
 TestTrue(TEXT("external referenced package preserved"), FPaths::FileExists(retainedPackage));
 TestTrue(TEXT("disk-only external reference prevents deletion"), retainedTarget.IsValid());
 if (retainedTarget.IsValid()) user->Assets.Add(TEXT("UserReference"), retainedTarget.Get());
 TestEqual(TEXT("saved user identity preserved"), user->Assets.FindRef(TEXT("UserReference")).Get(), referencedAudio);
 TestEqual(TEXT("rechecking referenced asset preserves it"), watch->CleanupRetiredAssets(referenced.Namespace), 0);
 AddInfo(watch->GetStatus());
 auto persistence = MakeShared<FJsonObject>();
 persistence->SetStringField(TEXT("removedPackage"), removedPackage);
 persistence->SetStringField(TEXT("retainedPackage"), retainedPackage);
 persistence->SetStringField(TEXT("unrelatedPackage"), unrelatedPackage);
 persistence->SetStringField(TEXT("userPath"), user->GetPathName());
 persistence->SetStringField(TEXT("registryPath"), referencedState->GetPathName());
 FFileHelper::SaveStringToFile(Json(persistence), *(FPaths::ProjectSavedDir() / TEXT("WatchRemovalPersistence.json")));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaWatchPersistenceTest, "Inkpot.Prosetta.WatchPersistence",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaWatchPersistenceTest::RunTest(const FString& InParameters)
{
 FString path;
 if (!TestTrue(TEXT("watch persistence fixture recorded"), FFileHelper::LoadFileToString(path, *(FPaths::ProjectSavedDir() / TEXT("WatchPersistence.txt"))))) return false;
 UProsettaGenerationAsset* state = LoadObject<UProsettaGenerationAsset>(nullptr, *path);
 if (!TestNotNull(TEXT("saved registry reloads"), state)) return false;
 UProsettaSyncedArtifact* audio = Cast<UProsettaSyncedArtifact>(state->Assets.FindRef(TEXT("audio-file:take1")));
 if (!TestNotNull(TEXT("saved audio wrapper reloads"), audio) || !TestNotNull(TEXT("saved nested SoundWave reloads"), audio->Audio.Get())) return false;
 TestFalse(TEXT("reloaded SoundWave is persistent"), audio->Audio->HasAnyFlags(RF_Transient));
 TestTrue(TEXT("reloaded SoundWave retains duration"), audio->Audio->Duration > 0);
 UProsettaSyncedArtifact* localization = Cast<UProsettaSyncedArtifact>(state->Assets.FindRef(TEXT("localization")));
 FString localized;
 if (TestNotNull(TEXT("saved localization reloads"), localization))
 {
  TestTrue(TEXT("saved localization resolves"), localization->ResolveLocalizedText(FName(TEXT("023A")), TEXT("fr"), localized));
  TestEqual(TEXT("saved localized string"), localized, FString(TEXT("Bonjour 3")));
 }
 FString removalJSON;
 TSharedPtr<FJsonObject> removal;
 if (!TestTrue(TEXT("removal persistence record exists"), FFileHelper::LoadFileToString(removalJSON, *(FPaths::ProjectSavedDir() / TEXT("WatchRemovalPersistence.json"))))) return false;
 if (!TestTrue(TEXT("removal persistence record parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(removalJSON), removal))) return false;
 TestFalse(TEXT("deleted owned package absent after restart"), FPaths::FileExists(removal->GetStringField(TEXT("removedPackage"))));
 TestTrue(TEXT("referenced owned package retained after restart"), FPaths::FileExists(removal->GetStringField(TEXT("retainedPackage"))));
 TestTrue(TEXT("unrelated package retained after restart"), FPaths::FileExists(removal->GetStringField(TEXT("unrelatedPackage"))));
 UProsettaGenerationAsset* user = LoadObject<UProsettaGenerationAsset>(nullptr, *removal->GetStringField(TEXT("userPath")));
 UProsettaGenerationAsset* retiredRegistry = LoadObject<UProsettaGenerationAsset>(nullptr, *removal->GetStringField(TEXT("registryPath")));
 if (TestNotNull(TEXT("saved external user reference loads"), user) && TestNotNull(TEXT("saved retirement registry loads"), retiredRegistry))
 {
  UObject* retired = retiredRegistry->RetiredAssets.FindRef(TEXT("audio-file:take1"));
  TestNotNull(TEXT("saved referenced retired asset loads"), retired);
  TestEqual(TEXT("saved user reference matches retained identity"), user->Assets.FindRef(TEXT("UserReference")).Get(), retired);
 }

 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaWatchCLITest, "Inkpot.Prosetta.WatchCLI",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaWatchCLITest::RunTest(const FString& InParameters)
{
 // Version gate: watch shipped in Prosetta Sync CLI 0.10.0.
 TestTrue(TEXT("0.10.0 supported"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.10.0\n")));
 TestTrue(TEXT("build metadata ignored"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.10.0+93b8cf038")));
 TestTrue(TEXT("v prefix tolerated"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("v0.10.1")));
 TestTrue(TEXT("newer minor supported"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.11.0")));
 TestTrue(TEXT("newer major supported"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("1.0.0")));
 TestTrue(TEXT("numeric not lexical compare"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.100.0")));
 TestFalse(TEXT("0.9.0 rejected"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.9.0")));
 TestFalse(TEXT("0.9.12 rejected"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.9.12")));
 TestFalse(TEXT("0.10.0 prerelease rejected"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.10.0-rc.1")));
 TestTrue(TEXT("later prerelease supported"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.11.0-rc.1")));
 TestFalse(TEXT("empty rejected"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("")));
 TestFalse(TEXT("garbage rejected"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("Unknown command 'version'.")));
 TestFalse(TEXT("two-part rejected"), UProsettaWatchSubsystem::IsSupportedCLIVersion(TEXT("0.10")));

 // Arguments mirror the CLI's usage checks ( which otherwise exit with code 2 ).
 FString arguments, error;
 TestTrue(TEXT("default live watch"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("live"), TEXT(""), true, arguments, error));
 TestEqual(TEXT("default arguments"), arguments, FString(TEXT("watch --json --config \"/cfg/config.json\" --channel live")));
 TestTrue(TEXT("scoped release watch"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("latest-release"), TEXT("core"), false, arguments, error));
 TestEqual(TEXT("scoped arguments"), arguments, FString(TEXT("watch --json --config \"/cfg/config.json\" --channel latest-release --volumes core --no-mirror")));
 TestTrue(TEXT("multi-volume live watch"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("live"), TEXT("core,side@latest"), true, arguments, error));
 TestTrue(TEXT("multi-volume arguments"), arguments.EndsWith(TEXT("--volumes core,side@latest")));
 TestFalse(TEXT("unknown channel"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("nightly"), TEXT(""), true, arguments, error));
 TestTrue(TEXT("unknown channel explained"), error.Contains(TEXT("live or latest-release")));
 TestFalse(TEXT("channel is case sensitive"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("LIVE"), TEXT(""), true, arguments, error));
 TestFalse(TEXT("fixed tag rejected"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("live"), TEXT("core@1.0"), true, arguments, error));
 TestTrue(TEXT("fixed tag explained"), error.Contains(TEXT("sync")));
 TestFalse(TEXT("multiple release volumes rejected"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("latest-release"), TEXT("core,side"), true, arguments, error));
 TestFalse(TEXT("empty volume rejected"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("live"), TEXT("core,,side"), true, arguments, error));
 TestFalse(TEXT("argument injection rejected"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/config.json"), TEXT("live"), TEXT("core --token x"), true, arguments, error));
 TestFalse(TEXT("quoted config rejected"), UProsettaWatchSubsystem::BuildWatchArguments(TEXT("/cfg/\"x\".json"), TEXT("live"), TEXT(""), true, arguments, error));

 // Non-import events surface their code and never queue.
 UProsettaWatchSubsystem* watch = GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>();
 if (!TestNotNull(TEXT("watch subsystem"), watch)) return false;
 const int32 pending = watch->GetPendingCount();
 TestTrue(TEXT("reconnect accepted"), watch->ConsumeEvent(TEXT("{\"schemaVersion\":1,\"type\":\"error\",\"projectId\":7,\"channel\":\"live\",\"code\":\"STREAM_RECONNECTING\"}")));
 TestTrue(TEXT("reconnect status"), watch->GetStatus().Contains(TEXT("reconnecting")));
 TestTrue(TEXT("REST error accepted"), watch->ConsumeEvent(TEXT("{\"schemaVersion\":1,\"type\":\"error\",\"projectId\":7,\"channel\":\"live\",\"scope\":\"project\",\"code\":\"REST_401\"}")));
 TestTrue(TEXT("REST code surfaced"), watch->GetStatus().Contains(TEXT("REST_401")) && watch->GetStatus().Contains(TEXT("project")));
 TestTrue(TEXT("retry accepted"), watch->ConsumeEvent(TEXT("{\"schemaVersion\":1,\"type\":\"error\",\"projectId\":7,\"channel\":\"live\",\"code\":\"GENERATION_RETRY\"}")));
 TestTrue(TEXT("retry status"), watch->GetStatus().Contains(TEXT("retry")));
 TestTrue(TEXT("blocked accepted"), watch->ConsumeEvent(TEXT("{\"schemaVersion\":1,\"type\":\"blocked\",\"projectId\":7,\"channel\":\"latest-release\",\"scope\":\"core\",\"code\":\"REST_409\"}")));
 TestTrue(TEXT("blocked code surfaced"), watch->GetStatus().StartsWith(TEXT("Watch blocked REST_409 for core")));
 TestTrue(TEXT("status accepted"), watch->ConsumeEvent(TEXT("{\"schemaVersion\":1,\"type\":\"status\",\"code\":\"CONNECTED\"}")));
 TestTrue(TEXT("status surfaced"), watch->GetStatus().Contains(TEXT("CONNECTED")));
 TestTrue(TEXT("fatal JSON result accepted"), watch->ConsumeEvent(TEXT("{\"status\":\"error\",\"error\":\"Project token missing.\"}")));
 TestEqual(TEXT("fatal JSON result surfaced"), watch->GetStatus(), FString(TEXT("CLI error: Project token missing.")));
 TestFalse(TEXT("future schema rejected"), watch->ConsumeEvent(TEXT("{\"schemaVersion\":2,\"type\":\"generation-completed\"}")));
 TestFalse(TEXT("non-JSON rejected"), watch->ConsumeEvent(TEXT("not json")));
 TestEqual(TEXT("nothing queued"), watch->GetPendingCount(), pending);
 return true;
}
#if PLATFORM_MAC || PLATFORM_LINUX
class FProsettaSidecarCommand : public IAutomationLatentCommand
{
public:
 FProsettaSidecarCommand(FAutomationTestBase* InTest, UProsettaWatchSubsystem* InWatch, const FFixture& InFixture)
  : Test(InTest), Watch(InWatch), Fixture(InFixture), Started(FPlatformTime::Seconds()) {}
 virtual bool Update() override
 {
  const FString path = TEXT("/Game/Prosetta/") + Fixture.Namespace + TEXT("/Generation.Generation");
  UProsettaGenerationAsset* state = FindObject<UProsettaGenerationAsset>(nullptr, *path);
  if (!state && FPlatformTime::Seconds() - Started < 15) return false;
  Test->AddInfo(FString::Printf(TEXT("Sidecar status: %s; pending=%d; safe=%d"), *Watch->GetStatus(), Watch->GetPendingCount(), UProsettaWatchSubsystem::IsSafeToApply()));
  Test->TestNotNull(TEXT("CLI stdout event applied by real editor ticker"), state);
  if (state)
  {
   UInkpotStoryAsset* story = Cast<UInkpotStoryAsset>(state->Assets.FindRef(TEXT("compiled-story")));
   Test->TestTrue(TEXT("sidecar generation imported exact compiled story"), story && story->GetCompiledJSON().Contains(TEXT("Generation 9")));
  }
  FString arguments;
  FFileHelper::LoadFileToString(arguments, *(Fixture.Root / TEXT("args.txt")));
  Test->TestTrue(TEXT("CLI launched with native watch arguments"), arguments.StartsWith(TEXT("watch --json --config ")) && arguments.Contains(TEXT("--channel live")));
  Test->TestFalse(TEXT("default watch mirrors"), arguments.Contains(TEXT("--no-mirror")));
  Watch->StopWatch();
  return true;
 }
private:
 FAutomationTestBase* Test;
 UProsettaWatchSubsystem* Watch;
 FFixture Fixture;
 double Started;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaSidecarTest, "Inkpot.Prosetta.WatchSidecar",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaSidecarTest::RunTest(const FString& InParameters)
{
 const FFixture fixture = Generation(TEXT("sidecar-") + FGuid::NewGuid().ToString(), TEXT("9"), TEXT("live"));
 const FString eventPath = fixture.Root / TEXT("event.ndjson");
 const FString executable = fixture.Root / TEXT("fixture-sidecar.sh");
 const FString config = fixture.Root / TEXT("config.json");
 FFileHelper::SaveStringToFile(fixture.Event + TEXT("\n"), *eventPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 FFileHelper::SaveStringToFile(TEXT("{}"), *config);
 const FString argsPath = fixture.Root / TEXT("args.txt");
 const FString script = TEXT("#!/bin/sh\nif [ \"$1\" = \"--version\" ]; then echo 0.10.0; exit 0; fi\nprintf '%s\\n' \"$*\" > \"") + argsPath +
  TEXT("\"\ncat \"") + eventPath + TEXT("\"\nsleep 30\n");
 FFileHelper::SaveStringToFile(script, *executable, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 int32 chmodExit = -1;
 FPlatformProcess::ExecProcess(TEXT("/bin/chmod"), *(TEXT("700 \"") + executable + TEXT("\"")), &chmodExit, nullptr, nullptr);
 UProsettaWatchSubsystem* watch = GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>();
 if (!TestNotNull(TEXT("shipping editor subsystem registered"), watch)) return false;
 if (!TestTrue(TEXT("real sidecar process launches"), watch->StartWatch(executable, config, TEXT("")))) return false;
 AddCommand(new FProsettaSidecarCommand(this, watch, fixture));
 return true;
}

class FProsettaSidecarExitCommand : public IAutomationLatentCommand
{
public:
 FProsettaSidecarExitCommand(FAutomationTestBase* InTest, UProsettaWatchSubsystem* InWatch, const FString& InExpected)
  : Test(InTest), Watch(InWatch), Expected(InExpected), Started(FPlatformTime::Seconds()) {}
 virtual bool Update() override
 {
  if (!Watch->GetStatus().StartsWith(TEXT("CLI ")) && FPlatformTime::Seconds() - Started < 10) return false;
  Test->AddInfo(TEXT("Sidecar status: ") + Watch->GetStatus());
  Test->TestTrue(TEXT("CLI exit surfaced with its diagnostic"), Watch->GetStatus().Contains(Expected));
  Watch->StopWatch();
  return true;
 }
private:
 FAutomationTestBase* Test;
 UProsettaWatchSubsystem* Watch;
 FString Expected;
 double Started;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaSidecarExitTest, "Inkpot.Prosetta.WatchSidecarExit",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaSidecarExitTest::RunTest(const FString& InParameters)
{
 const FString root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ProsettaWatchTests") / (TEXT("exit-") + FGuid::NewGuid().ToString()));
 IFileManager::Get().MakeDirectory(*root, true);
 const FString config = root / TEXT("config.json");
 FFileHelper::SaveStringToFile(TEXT("{}"), *config);
 const auto WriteScript = [&root](const FString& Name, const FString& Body)
 {
  const FString path = root / Name;
  FFileHelper::SaveStringToFile(TEXT("#!/bin/sh\n") + Body, *path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  int32 chmodExit = -1;
  FPlatformProcess::ExecProcess(TEXT("/bin/chmod"), *(TEXT("700 \"") + path + TEXT("\"")), &chmodExit, nullptr, nullptr);
  return path;
 };
 UProsettaWatchSubsystem* watch = GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>();
 if (!TestNotNull(TEXT("shipping editor subsystem registered"), watch)) return false;

 // A pre-watch CLI is refused before it is ever launched with `watch`.
 const FString old = WriteScript(TEXT("old-cli.sh"), TEXT("if [ \"$1\" = \"--version\" ]; then echo 0.9.0; exit 0; fi\ntouch \"") + root + TEXT("/launched\"\n"));
 TestFalse(TEXT("0.9.0 CLI refused"), watch->StartWatch(old, config, TEXT("")));
 TestTrue(TEXT("version requirement explained"), watch->GetStatus().Contains(TEXT("0.10.0")) && watch->GetStatus().Contains(TEXT("0.9.0")));
 TestFalse(TEXT("old CLI never ran watch"), FPaths::FileExists(root / TEXT("launched")));

 // Plugin-side usage checks fail fast without launching.
 TestFalse(TEXT("fixed tag refused"), watch->StartWatch(old, config, TEXT(""), TEXT("live"), TEXT("core@1.0")));
 TestTrue(TEXT("fixed tag explained"), watch->GetStatus().Contains(TEXT("one-shot sync")));

 // A CLI usage error ( exit 2, stderr ) is reported once the process exits; NDJSON before it still counts.
 const FString rejecting = WriteScript(TEXT("rejecting-cli.sh"), TEXT("if [ \"$1\" = \"--version\" ]; then echo 0.10.0; exit 0; fi\n")
  TEXT("echo '{\"schemaVersion\":1,\"type\":\"error\",\"projectId\":1,\"channel\":\"live\",\"code\":\"STREAM_RECONNECTING\"}'\n")
  TEXT("echo 'Watch each latest-release volume with its own cache.' >&2\necho 'Run prosetta-sync --help for usage.' >&2\nexit 2\n"));
 if (!TestTrue(TEXT("0.10.0 CLI launches"), watch->StartWatch(rejecting, config, TEXT(""), TEXT("latest-release"), TEXT("core"), false))) return false;
 TestTrue(TEXT("launch status names channel and version"), watch->GetStatus().Contains(TEXT("latest-release")) && watch->GetStatus().Contains(TEXT("0.10.0")));
 AddCommand(new FProsettaSidecarExitCommand(this, watch, TEXT("CLI rejected the watch arguments: Watch each latest-release volume with its own cache.")));
 return true;
}
#endif
#endif
