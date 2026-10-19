#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Inkpot/Prosetta/ProsettaStorySource.h"
#include "Inkpot/Prosetta/ProsettaStorySourceActor.h"
#include "Inkpot/Prosetta/ProsettaSyncInspector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A throwaway game world to place story source hosts in. */
	struct FStorySourceWorld
	{
		UWorld* World = nullptr;
		FStorySourceWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ProsettaStorySourceTest"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		~FStorySourceWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		FStorySourceWorld(const FStorySourceWorld&) = delete;
		FStorySourceWorld& operator=(const FStorySourceWorld&) = delete;
		template <typename T> T* Spawn() const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<T>(Params);
		}
	};

	/** A scratch mirror under the automation transient dir with a manifest and the files it names. */
	struct FScratchMirror
	{
		FString Root;
		explicit FScratchMirror(const TCHAR* Name)
		{
			Root = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("ProsettaStorySource"), Name);
			IFileManager::Get().DeleteDirectory(*Root, false, true);
			IFileManager::Get().MakeDirectory(*Root, true);
		}
		~FScratchMirror() { IFileManager::Get().DeleteDirectory(*Root, false, true); }
		void Write(const FString& Relative, const FString& Content) const
		{
			FFileHelper::SaveStringToFile(Content, *FPaths::Combine(Root, Relative));
		}
	};

	const TCHAR* MultiVolumeManifest = TEXT(R"JSON({"schemaVersion":2,"projectId":150,
		"scope":{"canonical":"volumes=core@1.0,extra","volumes":[
			{"slug":"core","tag":"1.0","compiledStoryArtifactKey":"compiled-story:core","linesArtifactKey":"lines:core","localizationArtifactKey":"localization:core"},
			{"slug":"extra","tag":""}]},
		"generation":{"id":"abc","revision":{"150":"r7","*":"r9"}},
		"artifacts":[
			{"key":"compiled-story","relativePath":"compiled/story.json","checksum":"aaaa"},
			{"key":"lines","relativePath":"lines/lines.json","checksum":"bbbb"},
			{"key":"localization","relativePath":"localization/localization.json","checksum":"cccc"},
			{"key":"compiled-story:core","relativePath":"compiled/core/story.json","checksum":"dddd"},
			{"key":"lines:core","relativePath":"lines/core/lines.json","checksum":"eeee"},
			{"key":"localization:core","relativePath":"localization/core/localization.json","checksum":"ffff"}]})JSON");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaStorySourceForWorldDefaultTest, "Inkpot.Prosetta.StorySource.ForWorldDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaStorySourceForWorldDefaultTest::RunTest(const FString& Parameters)
{
	FStorySourceWorld W;
	const FProsettaStorySource Source = ProsettaStorySource::ForWorld(W.World);
	TestTrue(TEXT("no setup resolves to the canonical source"), Source.IsCanonical());
	TestTrue(TEXT("default prefers the mirror"), Source.bPreferSyncedMirror);
	TestTrue(TEXT("no override"), Source.StoryAssetOverride.IsNull());
	TestEqual(TEXT("describe"), Source.Describe(), FString(TEXT("canonical")));
	TestEqual(TEXT("null world is canonical"), ProsettaStorySource::ForWorld(nullptr).Describe(), FString(TEXT("canonical")));

	// The default source resolves to exactly the canonical story so existing levels keep playing the same file.
	const FString Expected = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Prosetta"), TEXT("compiled"), TEXT("story.json"));
	TestEqual(TEXT("compiled story path"), ProsettaStorySource::CompiledStoryPathForWorld(W.World), Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaStorySourceForWorldFromHostTest, "Inkpot.Prosetta.StorySource.ForWorldFromHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaStorySourceForWorldFromHostTest::RunTest(const FString& Parameters)
{
	FStorySourceWorld W;
	AProsettaStorySourceActor* Host = W.Spawn<AProsettaStorySourceActor>();
	Host->StorySource.VolumeSlug = TEXT("core");
	Host->StorySource.VolumeTag = TEXT("1.0");
	TestTrue(TEXT("host found through the interface"), ProsettaStorySource::FindHost(W.World) == Host);
	const FProsettaStorySource Source = ProsettaStorySource::ForWorld(W.World);
	TestEqual(TEXT("slug from host"), Source.VolumeSlug, FString(TEXT("core")));
	TestEqual(TEXT("describe volume"), Source.Describe(), FString(TEXT("volume core@1.0")));
	TestTrue(TEXT("equality"), Source == Host->StorySource);

	IProsettaStorySourceHost* Interface = Cast<IProsettaStorySourceHost>(Host);
	FProsettaStorySource Next = Host->StorySource;
	TestFalse(TEXT("re-point with the same source is a no-op"), Interface->SetProsettaStorySource(Next));
	Next.VolumeSlug = TEXT("extra");
	Next.VolumeTag.Reset();
	TestTrue(TEXT("re-point changes the level"), Interface->SetProsettaStorySource(Next));
	TestEqual(TEXT("re-pointed slug"), ProsettaStorySource::ForWorld(W.World).VolumeSlug, FString(TEXT("extra")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaStorySourcePathsVolumePresentTest, "Inkpot.Prosetta.StorySource.Paths.VolumePresent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaStorySourcePathsVolumePresentTest::RunTest(const FString& Parameters)
{
	FProsettaManifestSummary Manifest;
	if (!TestTrue(TEXT("manifest parses"), UProsettaSyncInspector::ParseManifest(MultiVolumeManifest, Manifest))) { return false; }
	TestTrue(TEXT("scope object"), Manifest.ScopeDetail.bFromObject);
	TestEqual(TEXT("scope canonical"), Manifest.Scope, FString(TEXT("volumes=core@1.0,extra")));
	TestEqual(TEXT("two volumes"), Manifest.ScopeDetail.Volumes.Num(), 2);
	TestEqual(TEXT("revision for project"), Manifest.GenerationRevision, FString(TEXT("r9")));
	TestEqual(TEXT("revision map"), Manifest.GenerationRevisions.FindRef(TEXT("150")), FString(TEXT("r7")));

	const FString Root = TEXT("/mirror");
	FProsettaResolvedStoryPaths Paths;
	TestTrue(TEXT("core resolves"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, Root, TEXT("core"), Paths));
	TestEqual(TEXT("core story"), Paths.CompiledStoryPath, FString(TEXT("/mirror/compiled/core/story.json")));
	TestEqual(TEXT("core lines"), Paths.LinesPath, FString(TEXT("/mirror/lines/core/lines.json")));
	TestEqual(TEXT("core localization folder"), Paths.LocalizationFolder, FString(TEXT("/mirror/localization/core")));
	TestEqual(TEXT("core tag"), Paths.VolumeTag, FString(TEXT("1.0")));
	TestFalse(TEXT("no fallback"), Paths.bFellBackToCanonical);
	TestTrue(TEXT("from manifest"), Paths.bFromManifest);

	// Canonical request keeps the canonical artifacts.
	FProsettaResolvedStoryPaths Canonical;
	TestTrue(TEXT("canonical resolves"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, Root, TEXT(""), Canonical));
	TestEqual(TEXT("canonical story"), Canonical.CompiledStoryPath, FString(TEXT("/mirror/compiled/story.json")));
	TestTrue(TEXT("canonical has no slug in a multi-volume scope"), Canonical.VolumeSlug.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaStorySourcePathsVolumeAbsentTest, "Inkpot.Prosetta.StorySource.Paths.VolumeAbsent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaStorySourcePathsVolumeAbsentTest::RunTest(const FString& Parameters)
{
	FProsettaManifestSummary Manifest;
	if (!TestTrue(TEXT("manifest parses"), UProsettaSyncInspector::ParseManifest(MultiVolumeManifest, Manifest))) { return false; }
	const FString Root = TEXT("/mirror");

	// In scope, but the mirror has no artifact for it (and the scope is not that single volume).
	FProsettaResolvedStoryPaths Extra;
	TestFalse(TEXT("extra falls back"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, Root, TEXT("extra"), Extra));
	TestTrue(TEXT("extra flagged"), Extra.bFellBackToCanonical);
	TestEqual(TEXT("extra plays canonical"), Extra.CompiledStoryPath, FString(TEXT("/mirror/compiled/story.json")));
	TestTrue(TEXT("extra note mentions artifact"), Extra.Note.Contains(TEXT("no compiled story artifact")));

	// Not in scope at all.
	FProsettaResolvedStoryPaths Missing;
	TestFalse(TEXT("unknown falls back"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, Root, TEXT("nope"), Missing));
	TestTrue(TEXT("unknown note names the scope"), Missing.Note.Contains(TEXT("not in the synced scope")));
	TestTrue(TEXT("unknown note mentions the dashboard"), Missing.Note.Contains(TEXT("Prosetta Dashboard")));

	// Through the file path: the fallback is logged as a warning.
	FScratchMirror Mirror(TEXT("VolumeAbsent"));
	Mirror.Write(TEXT("manifest.json"), MultiVolumeManifest);
	AddExpectedMessage(TEXT("Prosetta story source: volume 'nope' is not in the synced scope"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	FProsettaResolvedStoryPaths FromFile;
	TestFalse(TEXT("file resolve falls back"), ProsettaStorySource::ResolveStoryPaths(Mirror.Root, TEXT("nope"), FromFile));
	TestTrue(TEXT("file resolve read the manifest"), FromFile.bFromManifest);

	// No manifest at all: canonical layout, flagged when a volume was asked for.
	FScratchMirror Empty(TEXT("NoManifest"));
	AddExpectedMessage(TEXT("has no readable manifest.json"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	FProsettaResolvedStoryPaths NoManifest;
	TestFalse(TEXT("no manifest with a volume falls back"), ProsettaStorySource::ResolveStoryPaths(Empty.Root, TEXT("core"), NoManifest));
	TestFalse(TEXT("not from manifest"), NoManifest.bFromManifest);
	FProsettaResolvedStoryPaths NoManifestCanonical;
	TestTrue(TEXT("no manifest canonical is fine"), ProsettaStorySource::ResolveStoryPaths(Empty.Root, TEXT(""), NoManifestCanonical));
	TestEqual(TEXT("canonical layout"), NoManifestCanonical.CompiledStoryPath, FPaths::Combine(Empty.Root, TEXT("compiled"), TEXT("story.json")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaStorySourcePathsSingleVolumeTest, "Inkpot.Prosetta.StorySource.Paths.SingleVolumeCanonicalFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaStorySourcePathsSingleVolumeTest::RunTest(const FString& Parameters)
{
	// A sync scoped to one volume writes that volume to the canonical files.
	FProsettaManifestSummary Manifest;
	const TCHAR* JSON = TEXT(R"JSON({"schemaVersion":2,"projectId":150,
		"scope":{"canonical":"volumes=core@1.0","volumes":[{"slug":"core","tag":"1.0"}]},
		"generation":{"id":"abc","revision":"r3"},
		"artifacts":[{"key":"compiled-story","relativePath":"compiled/story.json","checksum":"aaaa"},
		             {"key":"lines","relativePath":"lines/lines.json","checksum":"bbbb"}]})JSON");
	if (!TestTrue(TEXT("manifest parses"), UProsettaSyncInspector::ParseManifest(JSON, Manifest))) { return false; }
	TestEqual(TEXT("string revision"), Manifest.GenerationRevision, FString(TEXT("r3")));

	FProsettaResolvedStoryPaths Paths;
	TestTrue(TEXT("core resolves to canonical files"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, TEXT("/m"), TEXT("core"), Paths));
	TestEqual(TEXT("story"), Paths.CompiledStoryPath, FString(TEXT("/m/compiled/story.json")));
	TestEqual(TEXT("slug kept"), Paths.VolumeSlug, FString(TEXT("core")));
	TestEqual(TEXT("tag kept"), Paths.VolumeTag, FString(TEXT("1.0")));
	TestFalse(TEXT("no fallback"), Paths.bFellBackToCanonical);

	FProsettaResolvedStoryPaths Canonical;
	TestTrue(TEXT("canonical request"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, TEXT("/m"), TEXT(""), Canonical));
	TestEqual(TEXT("canonical names the single volume"), Canonical.VolumeSlug, FString(TEXT("core")));

	FProsettaResolvedStoryPaths Other;
	TestFalse(TEXT("another volume falls back"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, TEXT("/m"), TEXT("extra"), Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaStorySourcePathsLegacyScopeTest, "Inkpot.Prosetta.StorySource.Paths.LegacyStringScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaStorySourcePathsLegacyScopeTest::RunTest(const FString& Parameters)
{
	// Older manifests carry the scope as a string; volumes still resolve through `compiled-story:<slug>` keys.
	FProsettaManifestSummary Manifest;
	const TCHAR* JSON = TEXT(R"JSON({"schemaVersion":2,"projectId":150,"scope":"volumes=core@1.0,extra",
		"generation":{"id":"abc","revision":"r3"},
		"artifacts":[{"key":"compiled-story","relativePath":"compiled/story.json","checksum":"aaaa"},
		             {"key":"compiled-story:extra","relativePath":"compiled/extra/story.json","checksum":"bbbb"}]})JSON");
	if (!TestTrue(TEXT("manifest parses"), UProsettaSyncInspector::ParseManifest(JSON, Manifest))) { return false; }
	TestFalse(TEXT("scope from string"), Manifest.ScopeDetail.bFromObject);
	TestEqual(TEXT("two volumes parsed"), Manifest.ScopeDetail.Volumes.Num(), 2);
	const FProsettaScopeVolumeEntry* Core = Manifest.ScopeDetail.FindVolume(TEXT("core"));
	if (TestNotNull(TEXT("core entry"), Core)) { TestEqual(TEXT("core tag"), Core->Tag, FString(TEXT("1.0"))); }
	TestEqual(TEXT("describe"), Manifest.ScopeDetail.Describe(), FString(TEXT("core@1.0, extra")));
	TestEqual(TEXT("live scope"), FProsettaScopeSummary::FromString(TEXT("")).Describe(), FString(TEXT("live")));

	FProsettaResolvedStoryPaths Extra;
	TestTrue(TEXT("extra resolves by key"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, TEXT("/m"), TEXT("extra"), Extra));
	TestEqual(TEXT("extra story"), Extra.CompiledStoryPath, FString(TEXT("/m/compiled/extra/story.json")));
	TestEqual(TEXT("extra lines fall back to canonical"), Extra.LinesPath, FString(TEXT("/m/lines/lines.json")));

	FProsettaResolvedStoryPaths CoreOut;
	TestFalse(TEXT("core has no artifact"), ProsettaStorySource::ResolveStoryPathsFromManifest(Manifest, TEXT("/m"), TEXT("core"), CoreOut));
	return true;
}

#endif
