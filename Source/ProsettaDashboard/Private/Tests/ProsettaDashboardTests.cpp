// Prosetta Dashboard editor tests. Everything here runs offline: the model gets no CLI runner,
// and widgets get a fake model.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProsettaSyncProfile.h"
#include "ProsettaProfileStore.h"
#include "ProsettaDashboardSettings.h"
#include "ProsettaCliRunner.h"
#include "ProsettaPython.h"
#include "ProsettaStoryAssetBuild.h"
#include "../Dashboard/ProsettaDashboardSubsystem.h"
#include "Inkpot/Prosetta/ProsettaStorySource.h"
#include "Inkpot/Prosetta/ProsettaStorySourceActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "../Dashboard/ProsettaDashboardModel.h"
#include "../Dashboard/ProsettaDashboardTab.h"
#include "../Dashboard/Widgets/SProsettaDashboard.h"
#include "../Dashboard/Widgets/SProsettaSettingsPanel.h"
#include "../Dashboard/Widgets/SProsettaVolumesPanel.h"
#include "../Dashboard/Widgets/SProsettaChangesLog.h"
#include "../Dashboard/Widgets/SProsettaCredentialsForm.h"
#include "../Dashboard/Widgets/SProsettaConnectionHeader.h"
#include "Dom/JsonObject.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/StyleColors.h"

#define DASHBOARD_TEST(Class, Name) IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "Inkpot.Prosetta.Dashboard." Name, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString TransientDir(const TCHAR* Leaf)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir() / TEXT("ProsettaDashboard") / Leaf / FGuid::NewGuid().ToString());
	}

	FProsettaSyncProfile WatchProfile(std::initializer_list<FProsettaVolumeScope> Scopes)
	{
		FProsettaSyncProfile Profile;
		Profile.Mode = EProsettaSyncMode::Watch;
		Profile.Volumes = Scopes;
		return Profile;
	}

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}

	/** A model the widgets can drive without a CLI; every command only counts. */
	class FFakeDashboardModel final : public IProsettaDashboardModel
	{
	public:
		FProsettaDashboardState State;
		int32 Connects = 0, StartWatches = 0, StopWatches = 0, Syncs = 0, Verifies = 0, RefreshVolumesCalls = 0;
		int32 Runs = 0, Reimports = 0, Applies = 0, Dismisses = 0, Clears = 0, SetProfiles = 0, SwitchCalls = 0;
		int32 WriteCredentialsCalls = 0;
		FString LastWrittenBaseUrl, LastWrittenProjectId;
		int32 LastWrittenTokenLength = 0;
		FString LastSwitchSlug, LastSwitchTag;
		bool bLastSwitchRepoint = false;
		bool bRefuseSwitch = false;
		FProsettaDashboardStateChanged Changed;

		void Broadcast() { Changed.Broadcast(); }

		virtual const FProsettaDashboardState& GetState() const override { return State; }
		virtual FProsettaDashboardStateChanged& OnStateChanged() override { return Changed; }
		virtual void Connect() override { ++Connects; }
		virtual void StartWatch() override { ++StartWatches; }
		virtual void StopWatch() override { ++StopWatches; }
		virtual void SyncOnce() override { ++Syncs; }
		virtual void Verify() override { ++Verifies; }
		virtual void RefreshVolumes() override { ++RefreshVolumesCalls; }
		virtual void RefreshWhoAmI() override {}
		virtual void WriteCredentials(const FString& BaseUrl, const FString& ProjectId, FString&& Token) override
		{
			++WriteCredentialsCalls;
			LastWrittenBaseUrl = BaseUrl;
			LastWrittenProjectId = ProjectId;
			LastWrittenTokenLength = Token.Len();
			Token.Empty();
		}
		virtual void UseRecentConnection(int32) override {}
		virtual bool SwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, FString& OutError) override
		{
			++SwitchCalls;
			LastSwitchSlug = Slug;
			LastSwitchTag = Tag;
			bLastSwitchRepoint = bRepoint;
			if (bRefuseSwitch) { OutError = TEXT("refused by test"); return false; }
			return true;
		}
		virtual void SetProfile(const FProsettaSyncProfile& Edited) override { ++SetProfiles; State.Profile = Edited; State.ProfileProblems.Reset(); Edited.Validate(State.ProfileProblems); Broadcast(); }
		virtual bool LoadProfile(const FString&, FString&) override { return true; }
		virtual bool SaveProfile(FString&) override { State.bProfileDirty = false; return true; }
		virtual bool SaveProfileAs(const FString& NewName, FString&) override { State.Profile.Name = NewName; State.ProfileNames.AddUnique(NewName); return true; }
		virtual void RevertProfile() override {}
		virtual bool DeleteProfile(const FString&, FString&) override { return true; }
		virtual void RunProfile() override { ++Runs; }
		virtual void ReimportLastGeneration() override { ++Reimports; }
		virtual void ApplyPending() override { ++Applies; State.bExternalChangePending = false; Broadcast(); }
		virtual void DismissExternalChange() override { ++Dismisses; State.bExternalChangePending = false; Broadcast(); }
		virtual void OpenBlotter() override {}
		virtual void OpenOutliner() override {}
		virtual void ClearChanges() override { ++Clears; State.Changes.Reset(); Broadcast(); }
	};

	TSharedRef<FFakeDashboardModel> ConnectedFakeModel()
	{
		TSharedRef<FFakeDashboardModel> Model = MakeShared<FFakeDashboardModel>();
		Model->State.Connection = EProsettaDashboardConnection::Connected;
		Model->State.Credentials.Source = TEXT("local");
		Model->State.Credentials.bCliAvailable = true;
		Model->State.Credentials.BaseUrl = TEXT("https://example.invalid");
		Model->State.Credentials.ProjectId = TEXT("42");
		Model->State.Credentials.ProjectName = TEXT("Sample");
		Model->State.Profile = WatchProfile({ FProsettaVolumeScope(TEXT("core")) });
		Model->State.ProfileNames = { TEXT("Default") };
		return Model;
	}

	TSharedPtr<SWidget> FindByTag(const TSharedRef<SWidget>& Root, const FName& Tag)
	{
		if (Root->GetTag() == Tag) { return Root; }
		FChildren* Children = Root->GetChildren();
		if (!Children) { return nullptr; }
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindByTag(Children->GetChildAt(Index), Tag)) { return Found; }
		}
		return nullptr;
	}

	/** Finds the first SButton at or under the tagged widget (CommonUI/ToolWidgets buttons wrap one). */
	TSharedPtr<SButton> FindButton(const TSharedRef<SWidget>& Root, const FName& Tag)
	{
		TSharedPtr<SWidget> Tagged = FindByTag(Root, Tag);
		if (!Tagged) { return nullptr; }
		TArray<TSharedRef<SWidget>> Stack = { Tagged.ToSharedRef() };
		while (!Stack.IsEmpty())
		{
			TSharedRef<SWidget> Widget = Stack.Pop();
			const FName Type = Widget->GetType();
			if (Type == FName(TEXT("SButton")) || Type == FName(TEXT("SSimpleButton")) || Type == FName(TEXT("SPrimaryButton")))
			{
				return StaticCastSharedRef<SButton>(Widget);
			}
			if (FChildren* Children = Widget->GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index) { Stack.Add(Children->GetChildAt(Index)); }
			}
		}
		return nullptr;
	}

	/** Bound Slate attributes (Visibility_Lambda, IsEnabled_Lambda, WidgetIndex) refresh during prepass. */
	void Layout(const TSharedRef<SWidget>& Widget)
	{
		Widget->SlatePrepass(1.0f);
	}
}

// ------------------------------------------------------------------ profile

DASHBOARD_TEST(FProsettaDashboardProfileValidateTest, "Profile.Validate")
bool FProsettaDashboardProfileValidateTest::RunTest(const FString&)
{
	TArray<FText> Errors;
	TestTrue(TEXT("live watch is valid"), WatchProfile({}).Validate(Errors));
	TestTrue(TEXT("no errors"), Errors.IsEmpty());

	Errors.Reset();
	TestFalse(TEXT("watch refuses a fixed tag"), WatchProfile({ FProsettaVolumeScope(TEXT("core"), TEXT("1.0")) }).Validate(Errors));
	TestEqual(TEXT("one error"), Errors.Num(), 1);
	TestTrue(TEXT("names the pinned release"), Errors.Num() == 1 && Errors[0].ToString().Contains(TEXT("pins release")));

	Errors.Reset();
	FProsettaSyncProfile Pinned = WatchProfile({ FProsettaVolumeScope(TEXT("core"), TEXT("1.0")) });
	Pinned.Mode = EProsettaSyncMode::SyncOnce;
	TestTrue(TEXT("sync once accepts a fixed tag"), Pinned.Validate(Errors));

	Errors.Reset();
	FProsettaSyncProfile Latest = WatchProfile({ FProsettaVolumeScope(TEXT("core"), TEXT("latest")) });
	TestTrue(TEXT("'latest' is not a fixed tag"), Latest.Validate(Errors));

	Errors.Reset();
	FProsettaSyncProfile Release = WatchProfile({});
	Release.Channel = EProsettaWatchChannel::LatestRelease;
	TestFalse(TEXT("latest-release needs one volume"), Release.Validate(Errors));
	Release.Volumes = { FProsettaVolumeScope(TEXT("a")), FProsettaVolumeScope(TEXT("b")) };
	Errors.Reset();
	TestFalse(TEXT("latest-release refuses two volumes"), Release.Validate(Errors));
	Release.Volumes = { FProsettaVolumeScope(TEXT("a")) };
	Errors.Reset();
	TestTrue(TEXT("latest-release with one volume"), Release.Validate(Errors));

	Errors.Reset();
	TestFalse(TEXT("bad slug"), WatchProfile({ FProsettaVolumeScope(TEXT("bad slug!")) }).Validate(Errors));
	Errors.Reset();
	TestFalse(TEXT("duplicate slug"), WatchProfile({ FProsettaVolumeScope(TEXT("core")), FProsettaVolumeScope(TEXT("core")) }).Validate(Errors));
	TestTrue(TEXT("duplicate named"), Errors.Num() == 1 && Errors[0].ToString().Contains(TEXT("twice")));

	Errors.Reset();
	FProsettaSyncProfile VerifyOnly = WatchProfile({ FProsettaVolumeScope(TEXT("bad slug!")) });
	VerifyOnly.Mode = EProsettaSyncMode::Verify;
	TestTrue(TEXT("verify ignores the scope"), VerifyOnly.Validate(Errors));

	Errors.Reset();
	FProsettaSyncProfile Http = WatchProfile({});
	Http.BaseUrl = TEXT("http://api.example.invalid");
	TestFalse(TEXT("plain http base URL refused"), Http.Validate(Errors));
	Http.BaseUrl = TEXT("http://127.0.0.1:61417");
	Errors.Reset();
	TestTrue(TEXT("loopback http accepted"), Http.Validate(Errors));
	Http.BaseUrl = TEXT("https://api.example.invalid");
	Errors.Reset();
	TestTrue(TEXT("https accepted"), Http.Validate(Errors));

	Errors.Reset();
	FProsettaSyncProfile Decoder = WatchProfile({});
	Decoder.bImportOnAccept = true;
	Decoder.AudioDecoderPath.FilePath = TransientDir(TEXT("MissingDecoder")) / TEXT("ffmpeg");
	TestFalse(TEXT("missing decoder refused under watch"), Decoder.Validate(Errors));
	Errors.Reset();
	Decoder.AudioDecoderPath.FilePath = TEXT("ffmpeg");
	TestTrue(TEXT("bare decoder name is left to the PATH lookup"), Decoder.Validate(Errors));
	Decoder.AudioDecoderPath.FilePath = TransientDir(TEXT("MissingDecoder")) / TEXT("ffmpeg");
	Decoder.bImportOnAccept = false;
	Errors.Reset();
	TestTrue(TEXT("decoder ignored without import"), Decoder.Validate(Errors));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardProfileScopeTest, "Profile.ScopeAndFlags")
bool FProsettaDashboardProfileScopeTest::RunTest(const FString&)
{
	TestEqual(TEXT("live scope is empty"), WatchProfile({}).ScopeString(), FString());
	FProsettaSyncProfile Profile = WatchProfile({ FProsettaVolumeScope(TEXT("core"), TEXT("1.0")), FProsettaVolumeScope(TEXT("side")) });
	TestEqual(TEXT("scope string"), Profile.ScopeString(), FString(TEXT("core@1.0,side")));
	TestTrue(TEXT("fixed tag detected"), Profile.IsFixedTag());
	TestFalse(TEXT("latest is not fixed"), WatchProfile({ FProsettaVolumeScope(TEXT("core"), TEXT("latest")) }).IsFixedTag());

	const FProsettaVolumeScope Parsed = FProsettaVolumeScope::Parse(TEXT("core@2.1"));
	TestEqual(TEXT("parsed slug"), Parsed.Slug, FString(TEXT("core")));
	TestEqual(TEXT("parsed tag"), Parsed.Tag, FString(TEXT("2.1")));
	TestEqual(TEXT("round trip"), Parsed.ToString(), FString(TEXT("core@2.1")));
	TestEqual(TEXT("plain slug"), FProsettaVolumeScope::Parse(TEXT("side")).ToString(), FString(TEXT("side")));
	TestTrue(TEXT("valid slug"), FProsettaVolumeScope::IsValidSlug(TEXT("core_2-b")));
	TestFalse(TEXT("space is invalid"), FProsettaVolumeScope::IsValidSlug(TEXT("co re")));
	TestFalse(TEXT("empty is invalid"), FProsettaVolumeScope::IsValidSlug(TEXT("")));

	FProsettaSyncProfile Watch = WatchProfile({ FProsettaVolumeScope(TEXT("core")) });
	Watch.Channel = EProsettaWatchChannel::LatestRelease;
	Watch.bNoMirror = true;
	Watch.BaseUrl = TEXT("https://api.example.invalid");
	Watch.ProjectId = TEXT("7");
	const FString WatchFlags = FString::Join(Watch.BuildWatchFlags(), TEXT(" "));
	TestTrue(TEXT("channel flag"), WatchFlags.Contains(TEXT("--channel ")) && WatchFlags.Contains(Watch.ChannelName()));
	TestTrue(TEXT("volumes flag"), WatchFlags.Contains(TEXT("--volumes core")));
	TestTrue(TEXT("no-mirror flag"), WatchFlags.Contains(TEXT("--no-mirror")));
	TestTrue(TEXT("base url flag"), WatchFlags.Contains(TEXT("--base-url https://api.example.invalid")));
	TestTrue(TEXT("project id flag"), WatchFlags.Contains(TEXT("--project-id 7")));
	TestFalse(TEXT("no token flag"), WatchFlags.Contains(TEXT("token")));

	FProsettaSyncProfile Sync = WatchProfile({});
	Sync.Mode = EProsettaSyncMode::SyncOnce;
	Sync.bRepair = true;
	Sync.bSnapshot = true;
	const FString SyncFlags = FString::Join(Sync.BuildSyncFlags(), TEXT(" "));
	TestTrue(TEXT("repair flag"), SyncFlags.Contains(TEXT("--repair")));
	TestTrue(TEXT("snapshot flag"), SyncFlags.Contains(TEXT("--snapshot")));
	TestFalse(TEXT("no connection flags when empty"), SyncFlags.Contains(TEXT("--base-url")));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardProfileJsonTest, "Profile.JsonRoundTrip")
bool FProsettaDashboardProfileJsonTest::RunTest(const FString&)
{
	FProsettaSyncProfile Profile = WatchProfile({ FProsettaVolumeScope(TEXT("core"), TEXT("1.0")), FProsettaVolumeScope(TEXT("side")) });
	Profile.Name = TEXT("QA");
	Profile.Mode = EProsettaSyncMode::SyncOnce;
	Profile.Channel = EProsettaWatchChannel::LatestRelease;
	Profile.bNoMirror = true;
	Profile.bImportOnAccept = false;
	Profile.bRepair = true;
	Profile.bSnapshot = true;
	Profile.BaseUrl = TEXT("https://api.example.invalid");
	Profile.ProjectId = TEXT("7");
	Profile.AudioDecoderPath.FilePath = TEXT("/opt/ffmpeg");
	Profile.CliBinaryOverride.FilePath = TEXT("/opt/prosetta-sync");
	Profile.bAutoStartWatchOnEditorLaunch = true;

	const FString Json = Profile.ToJson();
	TestFalse(TEXT("json never carries a token key"), Json.ToLower().Contains(TEXT("token")));
	FString SecretKey;
	TestFalse(TEXT("store finds no secret in a profile"), FProsettaProfileStore::ContainsSecretKey(Json, SecretKey));

	FProsettaSyncProfile Back;
	FString Error;
	TestTrue(TEXT("parses"), FProsettaSyncProfile::FromJson(Json, Back, Error));
	TestTrue(TEXT("round trip equal"), Back == Profile);
	TestEqual(TEXT("scope survives"), Back.ScopeString(), FString(TEXT("core@1.0,side")));

	Back.bRepair = false;
	TestTrue(TEXT("inequality"), Back != Profile);

	FProsettaSyncProfile Newer;
	TestFalse(TEXT("newer schema refused"), FProsettaSyncProfile::FromJson(TEXT("{\"schemaVersion\": 99, \"name\": \"X\"}"), Newer, Error));
	TestFalse(TEXT("error names the version"), Error.IsEmpty());
	TestFalse(TEXT("garbage refused"), FProsettaSyncProfile::FromJson(TEXT("not json"), Newer, Error));
	TestTrue(TEXT("unknown keys ignored"), FProsettaSyncProfile::FromJson(TEXT("{\"schemaVersion\": 1, \"name\": \"Y\", \"future\": true}"), Newer, Error));
	TestEqual(TEXT("name read"), Newer.Name, FString(TEXT("Y")));
	return true;
}

// ------------------------------------------------------------------ profile store

DASHBOARD_TEST(FProsettaDashboardProfileStoreTest, "ProfileStore.SaveLoadDelete")
bool FProsettaDashboardProfileStoreTest::RunTest(const FString&)
{
	const FString Directory = TransientDir(TEXT("Profiles"));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	FProsettaProfileStore Store(Directory);
	Store.SetUseSourceControl(false);
	TestTrue(TEXT("empty store lists nothing"), Store.List().IsEmpty());

	const FProsettaSyncProfile Default = Store.EnsureDefault();
	TestEqual(TEXT("default created"), Default.Name, FString(TEXT("Default")));
	TestTrue(TEXT("default file exists"), FPaths::FileExists(Store.PathFor(TEXT("Default"))));
	TestEqual(TEXT("one profile"), Store.List().Num(), 1);

	FProsettaSyncProfile Edited = Default;
	Edited.Volumes = { FProsettaVolumeScope(TEXT("core")) };
	TestTrue(TEXT("edited is dirty"), Store.IsDirty(Edited));
	FString Error;
	TestTrue(TEXT("save"), Store.Save(Edited, Error));
	TestFalse(TEXT("saved is clean"), Store.IsDirty(Edited));

	FProsettaSyncProfile Loaded;
	TestTrue(TEXT("load"), Store.Load(TEXT("Default"), Loaded, Error));
	TestTrue(TEXT("loaded equals saved"), Loaded == Edited);

	TestTrue(TEXT("save as"), Store.SaveAs(Edited, TEXT("QA Release"), Error));
	TArray<FString> Names = Store.List();
	TestEqual(TEXT("two profiles"), Names.Num(), 2);
	TestTrue(TEXT("sorted list"), Names.Num() == 2 && Names[0] == TEXT("Default") && Names[1] == TEXT("QA Release"));
	FProsettaSyncProfile Copy;
	TestTrue(TEXT("load copy"), Store.Load(TEXT("QA Release"), Copy, Error));
	TestEqual(TEXT("copy renamed"), Copy.Name, FString(TEXT("QA Release")));
	TestEqual(TEXT("copy keeps scope"), Copy.ScopeString(), FString(TEXT("core")));

	TestFalse(TEXT("save as invalid name refused"), Store.SaveAs(Edited, TEXT("../escape"), Error));
	TestFalse(TEXT("load missing fails"), Store.Load(TEXT("Nope"), Copy, Error));

	// A profile file holding a secret is refused on load, whatever the nesting.
	FFileHelper::SaveStringToFile(TEXT("{\"schemaVersion\":1,\"name\":\"Leaky\",\"connection\":{\"projectToken\":\"abc\"}}"), *Store.PathFor(TEXT("Leaky")));
	TestFalse(TEXT("secret-bearing profile refused"), Store.Load(TEXT("Leaky"), Copy, Error));
	TestTrue(TEXT("error names the key"), Error.Contains(TEXT("projectToken")));

	TestTrue(TEXT("delete"), Store.Delete(TEXT("QA Release"), Error));
	TestFalse(TEXT("deleted file gone"), FPaths::FileExists(Store.PathFor(TEXT("QA Release"))));
	TestFalse(TEXT("delete missing fails"), Store.Delete(TEXT("QA Release"), Error));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardProfileStoreNamesTest, "ProfileStore.NamesAndSecrets")
bool FProsettaDashboardProfileStoreNamesTest::RunTest(const FString&)
{
	TestTrue(TEXT("simple"), FProsettaProfileStore::IsValidName(TEXT("Default")));
	TestTrue(TEXT("spaces, dots, dashes"), FProsettaProfileStore::IsValidName(TEXT("QA release-2.1_b")));
	TestFalse(TEXT("empty"), FProsettaProfileStore::IsValidName(TEXT("")));
	TestFalse(TEXT("slash"), FProsettaProfileStore::IsValidName(TEXT("a/b")));
	TestFalse(TEXT("backslash"), FProsettaProfileStore::IsValidName(TEXT("a\\b")));
	TestFalse(TEXT("parent"), FProsettaProfileStore::IsValidName(TEXT("..")));
	TestFalse(TEXT("colon"), FProsettaProfileStore::IsValidName(TEXT("a:b")));

	FString Key;
	for (const TCHAR* Json : { TEXT("{\"token\":\"x\"}"), TEXT("{\"a\":{\"b\":{\"clientSecret\":\"x\"}}}"), TEXT("{\"list\":[{\"password\":\"x\"}]}"), TEXT("{\"apiKey\":1}"), TEXT("{\"Authorization\":\"Bearer x\"}") })
	{
		Key.Reset();
		TestTrue(FString::Printf(TEXT("secret found in %s"), Json), FProsettaProfileStore::ContainsSecretKey(FString(Json), Key));
		TestFalse(TEXT("key reported"), Key.IsEmpty());
	}
	TestFalse(TEXT("plain profile clean"), FProsettaProfileStore::ContainsSecretKey(FString(TEXT("{\"name\":\"ok\",\"volumes\":[\"core\"]}")), Key));
	TestFalse(TEXT("invalid json is not a secret"), FProsettaProfileStore::ContainsSecretKey(FString(TEXT("nope")), Key));
	return true;
}

// ------------------------------------------------------------------ user settings

DASHBOARD_TEST(FProsettaDashboardRecentConnectionsTest, "UserSettings.RecentConnections")
bool FProsettaDashboardRecentConnectionsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UProsettaDashboardUserSettings> Settings(NewObject<UProsettaDashboardUserSettings>(GetTransientPackage()));
	const FDateTime T0(2026, 1, 1);
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Settings->PushRecentConnection(TEXT("https://api.example.invalid"), FString::FromInt(Index), FString::Printf(TEXT("P%d"), Index), T0 + FTimespan::FromMinutes(Index));
	}
	TestEqual(TEXT("capped"), Settings->RecentConnections.Num(), Settings->MaxRecentConnections);
	TestEqual(TEXT("newest first"), Settings->RecentConnections[0].ProjectId, FString(TEXT("9")));
	TestEqual(TEXT("oldest dropped"), Settings->RecentConnections.Last().ProjectId, FString(TEXT("2")));

	Settings->PushRecentConnection(TEXT("https://api.example.invalid"), TEXT("5"), TEXT("P5 renamed"), T0 + FTimespan::FromHours(1));
	TestEqual(TEXT("still capped"), Settings->RecentConnections.Num(), Settings->MaxRecentConnections);
	TestEqual(TEXT("moved to front"), Settings->RecentConnections[0].ProjectId, FString(TEXT("5")));
	TestEqual(TEXT("name refreshed"), Settings->RecentConnections[0].ProjectName, FString(TEXT("P5 renamed")));
	int32 Fives = 0;
	for (const FProsettaRecentConnection& C : Settings->RecentConnections) { if (C.ProjectId == TEXT("5")) { ++Fives; } }
	TestEqual(TEXT("no duplicate"), Fives, 1);
	TestTrue(TEXT("same target"), Settings->RecentConnections[0].SameTarget(TEXT("https://api.example.invalid"), TEXT("5")));
	TestFalse(TEXT("other host differs"), Settings->RecentConnections[0].SameTarget(TEXT("https://other.invalid"), TEXT("5")));
	TestEqual(TEXT("describe"), Settings->RecentConnections[0].Describe(), FString(TEXT("P5 renamed (https://api.example.invalid)")));
	return true;
}

// ------------------------------------------------------------------ CLI runner

DASHBOARD_TEST(FProsettaDashboardCliCommandLineTest, "CliRunner.CommandLine")
bool FProsettaDashboardCliCommandLineTest::RunTest(const FString&)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("init");
	Request.Args = { TEXT("--json"), TEXT("two words"), TEXT("") };
	Request.BaseUrl = TEXT("https://api.example.invalid");
	Request.ProjectId = TEXT("42");
	Request.StdinSecret = TEXT("placeholder-secret-value");
	const FString Line = FProsettaCliRunner::BuildCommandLine(TEXT("/path/to/Scripts/prosetta.py"), Request);
	TestFalse(TEXT("secret never on the command line"), Line.Contains(TEXT("placeholder-secret-value")));
	TestTrue(TEXT("token-stdin flag"), Line.Contains(TEXT("--token-stdin")));
	TestTrue(TEXT("unbuffered script"), Line.StartsWith(TEXT("-u \"/path/to/Scripts/prosetta.py\" init")));
	TestTrue(TEXT("args quoted when spaced"), Line.Contains(TEXT("\"two words\"")));
	TestTrue(TEXT("empty arg quoted"), Line.Contains(TEXT("\"\"")));
	TestTrue(TEXT("base url"), Line.Contains(TEXT("--base-url https://api.example.invalid")));
	TestTrue(TEXT("project id"), Line.Contains(TEXT("--project-id 42")));
	TestFalse(TEXT("describe never echoes the secret"), Request.Describe().Contains(TEXT("placeholder-secret-value")));

	FProsettaCliRequest Plain;
	Plain.Command = TEXT("credentials");
	Plain.Args = { TEXT("--json") };
	const FString PlainLine = FProsettaCliRunner::BuildCommandLine(TEXT("p.py"), Plain);
	TestFalse(TEXT("no stdin flag without a secret"), PlainLine.Contains(TEXT("--token-stdin")));
	TestFalse(TEXT("no overrides without values"), PlainLine.Contains(TEXT("--base-url")) || PlainLine.Contains(TEXT("--project-id")));

	for (const TCHAR* Notice : { TEXT("Prosetta wrapper failed: boom"), TEXT("usage: prosetta.py"), TEXT("Usage: x"), TEXT("error: bad"), TEXT("Error: bad"), TEXT("warning: w"), TEXT("Warning: w"), TEXT("Watch no longer polls") })
	{
		TestTrue(FString::Printf(TEXT("notice: %s"), Notice), FProsettaCliRunner::IsNoticeLine(Notice));
	}
	TestFalse(TEXT("plain line is not a notice"), FProsettaCliRunner::IsNoticeLine(TEXT("Synced 3 artifacts")));
	TestFalse(TEXT("token-ish line is not a notice"), FProsettaCliRunner::IsNoticeLine(TEXT("token=abc")));
	return true;
}

// ------------------------------------------------------------------ model

namespace
{
	struct FModelFixture
	{
		FString Root;
		TUniquePtr<FProsettaProfileStore> Store;
		FProsettaDashboardModelDeps Deps;
		bool bPIE = false;
		bool bSafe = true;
		int32 Reloads = 0;
		int32 PresentationReloads = 0;
		int32 RePoints = 0;
		int32 Toasts = 0;
		TArray<FString> ToastTexts;

		FModelFixture()
		{
			Root = TransientDir(TEXT("Model"));
			Store = MakeUnique<FProsettaProfileStore>(Root / TEXT("Profiles"));
			Store->SetUseSourceControl(false);
			Deps.Store = Store.Get();
			Deps.MirrorRoot = Root / TEXT("Mirror");
			Deps.IsPIE = [this]() { return bPIE; };
			Deps.SafeToApply = [this]() { return bSafe; };
			Deps.Reload = [this](const FProsettaSyncProfile&, const FString&, bool bWithPresentation, FString&) { ++Reloads; if (bWithPresentation) { ++PresentationReloads; } return true; };
			Deps.RePoint = [this](const FString&, const FString&, FString&) { ++RePoints; return true; };
			Deps.Toast = [this](const FText& Text, EProsettaToastKind) { ++Toasts; ToastTexts.Add(Text.ToString()); };
		}
		~FModelFixture()
		{
			Store.Reset();
			IFileManager::Get().DeleteDirectory(*Root, false, true);
		}
		TSharedRef<FProsettaDashboardModel> Make() { return MakeShared<FProsettaDashboardModel>(Deps); }
	};

	void WriteManifest(const FString& MirrorRoot, const FString& GenerationId, const FString& Scope = TEXT(""))
	{
		const FString Json = FString::Printf(TEXT(R"({"schemaVersion":2,"projectId":199999,"projectIdentifier":"sample","projectName":"Sample","scope":"%s","generatedAt":"2026-01-01T00:00:00Z","generation":{"id":"%s","revision":"r1"},"artifacts":{"compiled-story":{"relativePath":"compiled/story.json","checksum":"sha256:aaaa"}}})"), *Scope, *GenerationId);
		FFileHelper::SaveStringToFile(Json, *(MirrorRoot / TEXT("manifest.json")));
	}
}

DASHBOARD_TEST(FProsettaDashboardModelNoCliTest, "Model.NoCliIsFirstRun")
bool FProsettaDashboardModelNoCliTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();
	TestEqual(TEXT("no runner -> NoCli"), State.Connection, EProsettaDashboardConnection::NoCli);
	TestTrue(TEXT("first run"), State.NeedsFirstRun());
	TestFalse(TEXT("not connected"), State.IsConnected());
	TestTrue(TEXT("status names the CLI"), State.StatusText.Contains(TEXT("CLI")));
	TestEqual(TEXT("default profile ensured"), State.Profile.Name, FString(TEXT("Default")));
	TestEqual(TEXT("profile listed"), State.ProfileNames.Num(), 1);
	TestFalse(TEXT("no manifest"), State.bHasManifest);

	int32 Broadcasts = 0;
	Model->OnStateChanged().AddLambda([&Broadcasts]() { ++Broadcasts; });
	Model->Connect();
	TestEqual(TEXT("connect without a runner still broadcasts"), Broadcasts, 1);
	TestEqual(TEXT("still NoCli"), State.Connection, EProsettaDashboardConnection::NoCli);

	FString Error;
	TestFalse(TEXT("switch refused without a CLI"), Model->SwitchVolume(TEXT("core"), TEXT(""), false, Error));
	TestTrue(TEXT("error names the CLI"), Error.Contains(TEXT("CLI")));
	TestFalse(TEXT("invalid slug refused"), Model->SwitchVolume(TEXT("bad slug"), TEXT(""), false, Error));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelWrapperMissingTest, "Model.WrapperMissingIsFirstRun")
bool FProsettaDashboardModelWrapperMissingTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	Fixture.Deps.NoRunnerReason = TEXT("The prosetta.py wrapper was not found (looked in /Game/Scripts/prosetta.py).");
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();
	TestEqual(TEXT("no runner -> NoCli"), State.Connection, EProsettaDashboardConnection::NoCli);
	TestTrue(TEXT("first run"), State.NeedsFirstRun());
	TestEqual(TEXT("reason kept for the checklist"), State.CliSetupProblem, Fixture.Deps.NoRunnerReason);
	TestEqual(TEXT("headline says the wrapper is missing"), State.StatusText, Fixture.Deps.NoRunnerReason);

	TSharedRef<SProsettaDashboard> Dashboard = SNew(SProsettaDashboard, StaticCastSharedRef<IProsettaDashboardModel>(Model));
	TestEqual(TEXT("first-run page"), Dashboard->ActivePage(), 0);
	TestFalse(TEXT("cli unchecked"), Dashboard->HasCli());
	TestTrue(TEXT("cli row names the wrapper"), Dashboard->CliItemText().ToString().Contains(TEXT("prosetta.py")));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardScriptsTest, "Scripts.ShippedWithPlugin")
bool FProsettaDashboardScriptsTest::RunTest(const FString&)
{
	// A project without its own Scripts folder runs the copies in Plugins/Inkpot/Scripts.
	for (const TCHAR* Name : { TEXT("prosetta.py"), TEXT("ffmpeg.py") })
	{
		const TArray<FString> Candidates = ProsettaScriptCandidates(Name);
		TestEqual(FString::Printf(TEXT("%s: project then plugin"), Name), Candidates.Num(), 2);
		if (Candidates.Num() == 2)
		{
			TestTrue(FString::Printf(TEXT("%s: project copy first"), Name), Candidates[0].EndsWith(FString(TEXT("Scripts/")) + Name));
			TestTrue(FString::Printf(TEXT("%s: plugin ships it"), Name), FPaths::FileExists(Candidates[1]));
		}
		TestFalse(FString::Printf(TEXT("%s: found"), Name), FindProsettaScript(Name).IsEmpty());
	}
	TestTrue(TEXT("lock file beside ffmpeg.py"), FPaths::FileExists(FPaths::GetPath(FindProsettaScript(TEXT("ffmpeg.py"))) / TEXT("ffmpeg-lock.json")));
	TestTrue(TEXT("missing scripts are empty"), FindProsettaScript(TEXT("no-such-script.py")).IsEmpty());
	TestEqual(TEXT("default stage dir is outside the mirror"), FPaths::GetCleanFilename(ProsettaDefaultImportStageDir()), FString(TEXT("ProsettaImport")));
	TestFalse(TEXT("default story asset path"), ProsettaDefaultStoryAssetPath().IsEmpty());
	return true;
}

DASHBOARD_TEST(FProsettaDashboardRePointTest, "RePoint.StorySourceHost")
bool FProsettaDashboardRePointTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("ProsettaRePointTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	// RePointWorld's transactions join this one, which is cancelled so the editor's undo buffer keeps nothing from this throwaway world.
	FScopedTransaction Scratch(NSLOCTEXT("ProsettaDashboardTests", "RePointScratch", "Prosetta re-point test"));
	// Projects may name their own host class (Project Settings > Prosetta Sync); this test covers the plugin default.
	UProsettaSyncProjectSettings* Settings = GetMutableDefault<UProsettaSyncProjectSettings>();
	TGuardValue<TSoftClassPtr<AActor>> HostClassGuard(Settings->StorySourceHostClass, TSoftClassPtr<AActor>());
	FString Error;
	TestTrue(TEXT("default host class"), UProsettaDashboardSubsystem::StorySourceHostClass() == AProsettaStorySourceActor::StaticClass());
	TestNull(TEXT("no host yet"), ProsettaStorySource::FindHost(World));
	TestTrue(TEXT("re-point spawns a host"), UProsettaDashboardSubsystem::RePointWorld(World, TEXT("core"), TEXT("1.0"), Error));
	AActor* Host = ProsettaStorySource::FindHost(World);
	TestTrue(TEXT("spawned the default host"), Host && Host->IsA<AProsettaStorySourceActor>());
	TestEqual(TEXT("level follows the volume"), ProsettaStorySource::ForWorld(World).Describe(), FString(TEXT("volume core@1.0")));
	TestTrue(TEXT("latest clears the tag"), UProsettaDashboardSubsystem::RePointWorld(World, TEXT("side"), TEXT("latest"), Error));
	TestEqual(TEXT("same host reused"), ProsettaStorySource::FindHost(World), Host);
	TestEqual(TEXT("re-pointed"), ProsettaStorySource::ForWorld(World).Describe(), FString(TEXT("volume side")));
	TestFalse(TEXT("no world is refused"), UProsettaDashboardSubsystem::RePointWorld(nullptr, TEXT("core"), TEXT(""), Error));
	TestFalse(TEXT("refusal explains"), Error.IsEmpty());
	Scratch.Cancel();
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelClassifyTest, "Model.ClassifyConnection")
bool FProsettaDashboardModelClassifyTest::RunTest(const FString&)
{
	using E = EProsettaDashboardConnection;
	auto C = [](const TCHAR* Watch, const TCHAR* Transport, bool bOk, bool bKnown) { return FProsettaDashboardModel::ClassifyConnection(Watch, Transport, bOk, bKnown); };
	TestEqual(TEXT("unknown -> Disconnected"), C(TEXT(""), TEXT(""), false, false), E::Disconnected);
	TestEqual(TEXT("ok -> Connected"), C(TEXT("watching"), TEXT("running"), true, true), E::Connected);
	TestEqual(TEXT("rejected -> AuthFailed"), C(TEXT("watching"), TEXT(""), false, true), E::AuthFailed);
	TestEqual(TEXT("auth beats blocked"), C(TEXT("blocked by 409"), TEXT(""), false, true), E::AuthFailed);
	TestEqual(TEXT("blocked"), C(TEXT("Watch blocked by Prosetta"), TEXT(""), true, true), E::Blocked);
	TestEqual(TEXT("CLI error"), C(TEXT("CLI error: boom"), TEXT(""), true, true), E::Error);
	TestEqual(TEXT("import failed"), C(TEXT("Import failed: x"), TEXT(""), true, true), E::Error);
	TestEqual(TEXT("unable to launch"), C(TEXT("Unable to launch python"), TEXT(""), true, true), E::Error);
	TestEqual(TEXT("transport exited"), C(TEXT("watching"), TEXT("wrapper exited (1)"), true, true), E::Error);
	TestEqual(TEXT("transport not started"), C(TEXT(""), TEXT("not started: no token"), true, true), E::Error);
	TestEqual(TEXT("reconnecting -> Degraded"), C(TEXT("reconnecting in 5 s"), TEXT(""), true, true), E::Degraded);
	TestEqual(TEXT("will retry -> Degraded"), C(TEXT("generation failed; will retry"), TEXT(""), true, true), E::Degraded);
	TestEqual(TEXT("degraded without whoami"), C(TEXT("reconnecting"), TEXT(""), false, false), E::Degraded);

	TestTrue(TEXT("Describe has text"), !FProsettaDashboardModel::DescribeConnection(E::Blocked).IsEmpty());
	FProsettaDashboardState State;
	for (E Each : { E::Connected, E::Degraded, E::Blocked }) { State.Connection = Each; TestTrue(TEXT("IsConnected"), State.IsConnected()); }
	for (E Each : { E::NoCli, E::NoCredentials, E::Disconnected, E::Connecting, E::AuthFailed, E::Error }) { State.Connection = Each; TestFalse(TEXT("not connected"), State.IsConnected()); }
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelParseTest, "Model.ParseReports")
bool FProsettaDashboardModelParseTest::RunTest(const FString&)
{
	FProsettaCredentialsSummary Summary;
	TestTrue(TEXT("credentials report"), FProsettaDashboardModel::ParseCredentialsReport(ParseJsonObject(
		TEXT(R"({"source":"local","baseUrl":"https://api.example.invalid","projectId":"42","projectName":"Sample","localConfigPath":"/p/.prosetta-sync/config.json","localConfigExists":true,"sessionOverride":false,"cli":{"available":true,"command":"prosetta-sync"}})")), Summary));
	TestEqual(TEXT("source"), Summary.Source, FString(TEXT("local")));
	TestTrue(TEXT("has credentials"), Summary.HasCredentials());
	TestEqual(TEXT("base url"), Summary.BaseUrl, FString(TEXT("https://api.example.invalid")));
	TestEqual(TEXT("project id"), Summary.ProjectId, FString(TEXT("42")));
	TestEqual(TEXT("project name"), Summary.ProjectName, FString(TEXT("Sample")));
	TestTrue(TEXT("local config exists"), Summary.bLocalConfigExists);
	TestFalse(TEXT("no session override"), Summary.bSessionOverride);
	TestTrue(TEXT("cli available"), Summary.bCliAvailable);
	TestEqual(TEXT("cli command"), Summary.CliCommand, FString(TEXT("prosetta-sync")));
	TestFalse(TEXT("describe hides nothing secret"), Summary.Describe().IsEmpty());

	FProsettaCredentialsSummary None;
	TestTrue(TEXT("none report"), FProsettaDashboardModel::ParseCredentialsReport(ParseJsonObject(TEXT(R"({"source":"none","cli":{"available":false}})")), None));
	TestFalse(TEXT("no credentials"), None.HasCredentials());
	TestFalse(TEXT("cli missing"), None.bCliAvailable);
	TestFalse(TEXT("null report refused"), FProsettaDashboardModel::ParseCredentialsReport(nullptr, None));

	// The wrapper's report (prosetta.py describe_credentials) is the only credential-source report, and it
	// carries file facts, never a token. Even a report that wrongly carried one must not reach the summary.
	FProsettaCredentialsSummary Leaky;
	TestTrue(TEXT("report with candidates and a stray token parses"), FProsettaDashboardModel::ParseCredentialsReport(ParseJsonObject(
		TEXT(R"({"schemaVersion":1,"source":"native","token":"must-not-leak","candidates":[{"kind":"native","path":"/n/config.json","exists":true,"hasToken":true,"mode":"600","warning":null}],"cli":{"available":false,"command":"dotnet"}})")), Leaky));
	TestEqual(TEXT("native source kept"), Leaky.Source, FString(TEXT("native")));
	TestTrue(TEXT("native counts as credentials"), Leaky.HasCredentials());
	TestFalse(TEXT("summary never echoes a token"), Leaky.Describe().Contains(TEXT("must-not-leak")));
	TestEqual(TEXT("no warnings for a 600 file"), Leaky.Warnings.Num(), 0);

	FProsettaCredentialsSummary Loose;
	TestTrue(TEXT("report with a file warning parses"), FProsettaDashboardModel::ParseCredentialsReport(ParseJsonObject(
		TEXT(R"({"source":"home","candidates":[{"kind":"home","path":"/h/config.json","exists":true,"hasToken":true,"mode":"644","warning":"Token file is readable by other users (mode 644); run chmod 600 on it."},{"kind":"native","exists":false,"warning":null}],"cli":{"available":true}})")), Loose));
	TestEqual(TEXT("one warning kept"), Loose.Warnings.Num(), 1);
	TestTrue(TEXT("warning shown to the user"), Loose.Describe().Contains(TEXT("chmod 600")));

	FString Owner, Project;
	FProsettaDashboardModel::ParseWhoami(ParseJsonObject(TEXT(R"({"user":{"email":"dev@example.invalid"},"project":{"name":"Sample","identifier":"sample"}})")), Owner, Project);
	TestEqual(TEXT("owner from user.email"), Owner, FString(TEXT("dev@example.invalid")));
	TestEqual(TEXT("project from project.name"), Project, FString(TEXT("Sample")));
	Owner.Reset(); Project.Reset();
	FProsettaDashboardModel::ParseWhoami(ParseJsonObject(TEXT(R"({"tokenName":"qa-bot","projectName":"Other"})")), Owner, Project);
	TestEqual(TEXT("owner from tokenName"), Owner, FString(TEXT("qa-bot")));
	TestEqual(TEXT("project from projectName"), Project, FString(TEXT("Other")));
	Owner.Reset(); Project.Reset();
	FProsettaDashboardModel::ParseWhoami(ParseJsonObject(TEXT(R"({"token":"must-not-leak"})")), Owner, Project);
	TestTrue(TEXT("a token field is never taken as the owner"), Owner.IsEmpty());

	FProsettaChangeEntry Entry;
	const FDateTime When(2026, 3, 4, 5, 6);
	TestTrue(TEXT("completion receipt"), FProsettaDashboardModel::ParseRecentChange(
		TEXT(R"({"schemaVersion":1,"type":"generation-completed","projectId":199999,"scope":"volumes=core@1.0","channel":"live","generationId":"0123456789abcdefXYZ","changedArtifacts":["compiled-story","lines"],"removedArtifacts":["old"],"releaseTagId":"1.0"})"), When, Entry));
	TestEqual(TEXT("kind"), Entry.Kind, EProsettaChangeKind::Generation);
	TestEqual(TEXT("time"), Entry.Time, When);
	TestEqual(TEXT("short id"), Entry.ShortGenerationId(), FString(TEXT("0123456789ab")));
	TestEqual(TEXT("scope"), Entry.Scope, FString(TEXT("volumes=core@1.0")));
	TestEqual(TEXT("channel"), Entry.Channel, FString(TEXT("live")));
	const FString Described = Entry.Describe();
	TestTrue(TEXT("describe has id"), Described.Contains(TEXT("0123456789ab")));
	TestTrue(TEXT("describe has scope"), Described.Contains(TEXT("volumes=core@1.0")));

	TestTrue(TEXT("blocked event"), FProsettaDashboardModel::ParseRecentChange(TEXT(R"({"schemaVersion":1,"type":"blocked","projectId":1,"code":"E409","message":"content locked"})"), When, Entry));
	TestEqual(TEXT("blocked kind"), Entry.Kind, EProsettaChangeKind::Blocked);
	TestEqual(TEXT("blocked message"), Entry.Message, FString(TEXT("content locked")));
	TestTrue(TEXT("error event"), FProsettaDashboardModel::ParseRecentChange(TEXT(R"({"schemaVersion":1,"type":"generation-failed","projectId":1,"code":"E500"})"), When, Entry));
	TestEqual(TEXT("error kind"), Entry.Kind, EProsettaChangeKind::Error);
	TestEqual(TEXT("message falls back to code"), Entry.Message, FString(TEXT("E500")));
	TestFalse(TEXT("plain text is not an event"), FProsettaDashboardModel::ParseRecentChange(TEXT("Synced."), When, Entry));
	TestFalse(TEXT("json without type is not an event"), FProsettaDashboardModel::ParseRecentChange(TEXT("{\"a\":1}"), When, Entry));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelReloadPIETest, "Model.ReloadDeferredDuringPIE")
bool FProsettaDashboardModelReloadPIETest::RunTest(const FString&)
{
	FModelFixture Fixture;
	WriteManifest(Fixture.Deps.MirrorRoot, TEXT("gen-1"));
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();
	TestTrue(TEXT("manifest read"), State.bHasManifest);
	TestEqual(TEXT("generation id"), State.Manifest.GenerationId, FString(TEXT("gen-1")));

	Fixture.bPIE = true;
	Model->NoteGenerationAccepted(TEXT("gen-1"), true);
	TestTrue(TEXT("reload pending during Play"), State.bReloadPending);
	TestEqual(TEXT("reload not run"), Fixture.Reloads, 0);
	TestEqual(TEXT("generation logged"), State.Changes.Num(), 1);
	TestEqual(TEXT("generation kind"), State.Changes[0].Kind, EProsettaChangeKind::Generation);

	Model->ReimportLastGeneration();
	TestEqual(TEXT("second request coalesces"), Fixture.Reloads, 0);

	FProsettaDashboardPollInput Input;
	Input.bPIE = true;
	Input.PendingCount = 2;
	Model->Poll(Input);
	TestTrue(TEXT("still pending"), State.bReloadPending);
	TestTrue(TEXT("status mentions the queue"), State.StatusText.Contains(TEXT("2 generation(s) queued")));

	Fixture.bSafe = false;
	Fixture.bPIE = false;
	Input.bPIE = false;
	Input.bSafeToApply = false;
	Model->Poll(Input);
	TestEqual(TEXT("unsafe editor defers too"), Fixture.Reloads, 0);

	Fixture.bSafe = true;
	Input.bSafeToApply = true;
	Model->Poll(Input);
	TestEqual(TEXT("reload ran once Play stopped"), Fixture.Reloads, 1);
	TestFalse(TEXT("no longer pending"), State.bReloadPending);
	TestEqual(TEXT("rebuild logged"), State.Changes[0].Message, FString(TEXT("Story asset rebuilt from the mirror.")));
	Model->Poll(Input);
	TestEqual(TEXT("not repeated"), Fixture.Reloads, 1);
	TestFalse(TEXT("TryRunPendingReload idle"), Model->TryRunPendingReload());
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelAcceptedPresentationTest, "Model.AcceptedGenerationImportsPresentationOnce")
bool FProsettaDashboardModelAcceptedPresentationTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	WriteManifest(Fixture.Deps.MirrorRoot, TEXT("gen-1"));
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();

	// One accepted generation: one reload, and it asks for the presentation import.
	Model->NoteGenerationAccepted(TEXT("gen-1"), true);
	TestEqual(TEXT("one reload"), Fixture.Reloads, 1);
	TestEqual(TEXT("with presentation"), Fixture.PresentationReloads, 1);

	// Import On Accept off: no reload at all.
	Model->NoteGenerationAccepted(TEXT("gen-2"), false);
	TestEqual(TEXT("no reload when off"), Fixture.Reloads, 1);

	// Several generations accepted, then Rebuild, while Play defers the reload: still one run, one import.
	Fixture.bPIE = true;
	Model->NoteGenerationAccepted(TEXT("gen-3"), true);
	Model->NoteGenerationAccepted(TEXT("gen-4"), true);
	Model->ReimportLastGeneration();
	TestEqual(TEXT("deferred"), Fixture.Reloads, 1);
	Fixture.bPIE = false;
	FProsettaDashboardPollInput Input;
	Input.bSafeToApply = true;
	Model->Poll(Input);
	TestEqual(TEXT("coalesced into one reload"), Fixture.Reloads, 2);
	TestEqual(TEXT("coalesced into one import"), Fixture.PresentationReloads, 2);
	Model->Poll(Input);
	TestEqual(TEXT("not repeated"), Fixture.PresentationReloads, 2);

	// A story-only request that queues first must still upgrade to the import when an accept joins it.
	Fixture.bSafe = false;
	Model->EnqueueReload(false);
	Model->NoteGenerationAccepted(TEXT("gen-5"), true);
	Fixture.bSafe = true;
	Input.bSafeToApply = true;
	Model->Poll(Input);
	TestEqual(TEXT("one reload for the joined request"), Fixture.Reloads, 3);
	TestEqual(TEXT("joined request imports"), Fixture.PresentationReloads, 3);

	// The flag resets: a later plain reload is story-only.
	Model->EnqueueReload(false);
	TestEqual(TEXT("plain reload ran"), Fixture.Reloads, 4);
	TestEqual(TEXT("plain reload is story-only"), Fixture.PresentationReloads, 3);
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelExternalChangeTest, "Model.ExternalChange")
bool FProsettaDashboardModelExternalChangeTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	WriteManifest(Fixture.Deps.MirrorRoot, TEXT("gen-1"));
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();

	// Detection only: a terminal sync replaced the manifest.
	WriteManifest(Fixture.Deps.MirrorRoot, TEXT("gen-2"), TEXT("volumes=core"));
	Model->NoteExternalChange(TEXT("manifest.json changed"));
	TestTrue(TEXT("external change pending"), State.bExternalChangePending);
	TestEqual(TEXT("entry kind"), State.Changes[0].Kind, EProsettaChangeKind::ExternalChange);
	TestEqual(TEXT("nothing imported"), Fixture.Reloads, 0);

	Model->DismissExternalChange();
	TestFalse(TEXT("dismiss clears"), State.bExternalChangePending);
	TestEqual(TEXT("dismiss never reloads"), Fixture.Reloads, 0);

	Model->NoteExternalChange(TEXT("manifest.json changed"));
	Model->ApplyPending();
	TestFalse(TEXT("apply clears"), State.bExternalChangePending);
	TestEqual(TEXT("apply reloads once"), Fixture.Reloads, 1);
	TestEqual(TEXT("manifest re-read"), State.Manifest.GenerationId, FString(TEXT("gen-2")));
	TestTrue(TEXT("scope re-read"), State.Manifest.Scope.Contains(TEXT("core")));

	// Poll notices a newer manifest mtime without flagging an external change (the directory watcher does that).
	FProsettaDashboardPollInput Input;
	Input.ManifestMTime = FDateTime(2026, 5, 5);
	WriteManifest(Fixture.Deps.MirrorRoot, TEXT("gen-3"));
	Model->Poll(Input);
	TestEqual(TEXT("poll refreshed the manifest"), State.Manifest.GenerationId, FString(TEXT("gen-3")));
	TestFalse(TEXT("poll does not flag"), State.bExternalChangePending);
	TestEqual(TEXT("poll does not reload"), Fixture.Reloads, 1);

	Fixture.bPIE = true;
	FString Error;
	TestFalse(TEXT("switch refused during Play"), Model->SwitchVolume(TEXT("core"), TEXT(""), true, Error));
	TestTrue(TEXT("error says stop Play or names the CLI"), Error.Contains(TEXT("Play")) || Error.Contains(TEXT("CLI")));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelChangesTest, "Model.ChangesCapAndReceipts")
bool FProsettaDashboardModelChangesTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();
	for (int32 Index = 0; Index < FProsettaDashboardModel::MaxChanges + 25; ++Index)
	{
		FProsettaChangeEntry Entry;
		Entry.Time = FDateTime(2026, 1, 1) + FTimespan::FromSeconds(Index);
		Entry.Message = FString::FromInt(Index);
		Model->AddChange(MoveTemp(Entry));
	}
	TestEqual(TEXT("capped"), State.Changes.Num(), FProsettaDashboardModel::MaxChanges);
	TestEqual(TEXT("newest first"), State.Changes[0].Message, FString::FromInt(FProsettaDashboardModel::MaxChanges + 24));
	Model->ClearChanges();
	TestTrue(TEXT("cleared"), State.Changes.IsEmpty());

	const FString Receipts = Fixture.Root / TEXT("Receipts");
	FFileHelper::SaveStringToFile(TEXT(R"({"schemaVersion":1,"type":"generation-completed","projectId":1,"scope":"","channel":"live","generationId":"aaaaaaaaaaaaaaaa"})"), *(Receipts / TEXT("a.json")));
	FFileHelper::SaveStringToFile(TEXT(R"({"schemaVersion":1,"type":"blocked","projectId":1,"message":"locked"})"), *(Receipts / TEXT("b.json")));
	FFileHelper::SaveStringToFile(TEXT("not a receipt"), *(Receipts / TEXT("c.json")));
	FFileHelper::SaveStringToFile(TEXT("{}"), *(Receipts / TEXT("ignored.txt")));
	TestEqual(TEXT("two receipts seeded"), Model->SeedChangesFromReceipts(Receipts), 2);
	TestEqual(TEXT("two changes"), State.Changes.Num(), 2);
	TestEqual(TEXT("missing directory seeds nothing"), Model->SeedChangesFromReceipts(Fixture.Root / TEXT("Nope")), 0);
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelProfilesTest, "Model.ProfileEditing")
bool FProsettaDashboardModelProfilesTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();

	FProsettaSyncProfile Edited = State.Profile;
	Edited.Volumes = { FProsettaVolumeScope(TEXT("core"), TEXT("1.0")) };
	Model->SetProfile(Edited);
	TestTrue(TEXT("dirty after edit"), State.bProfileDirty);
	TestEqual(TEXT("watch + fixed tag flagged"), State.ProfileProblems.Num(), 1);

	Edited.Mode = EProsettaSyncMode::SyncOnce;
	Model->SetProfile(Edited);
	TestTrue(TEXT("sync once clears the problem"), State.ProfileProblems.IsEmpty());

	FString Error;
	TestTrue(TEXT("save"), Model->SaveProfile(Error));
	TestFalse(TEXT("clean after save"), State.bProfileDirty);
	TestTrue(TEXT("save as"), Model->SaveProfileAs(TEXT("Release"), Error));
	TestEqual(TEXT("active profile follows save as"), State.Profile.Name, FString(TEXT("Release")));
	TestEqual(TEXT("two profiles"), State.ProfileNames.Num(), 2);

	Edited = State.Profile;
	Edited.bRepair = true;
	Model->SetProfile(Edited);
	TestTrue(TEXT("dirty"), State.bProfileDirty);
	Model->RevertProfile();
	TestFalse(TEXT("revert restores disk"), State.bProfileDirty);
	TestFalse(TEXT("repair reverted"), State.Profile.bRepair);

	TestTrue(TEXT("load default"), Model->LoadProfile(TEXT("Default"), Error));
	TestEqual(TEXT("default active"), State.Profile.Name, FString(TEXT("Default")));
	TestTrue(TEXT("delete release"), Model->DeleteProfile(TEXT("Release"), Error));
	TestEqual(TEXT("one profile left"), State.ProfileNames.Num(), 1);
	TestTrue(TEXT("deleting the active profile recreates the default"), Model->DeleteProfile(TEXT("Default"), Error));
	TestEqual(TEXT("default recreated"), State.ProfileNames.Num(), 1);
	TestEqual(TEXT("default active again"), State.Profile.Name, FString(TEXT("Default")));
	return true;
}

namespace
{
	/** A runner that records the config/sync calls the model makes and completes them synchronously. */
	class FRecordingRunner final : public FProsettaCliRunner
	{
	public:
		FRecordingRunner() : FProsettaCliRunner(FString(), FString(), FString()) {}
		TArray<FString> Ops;
		bool bFailConfig = false;

		virtual int32 ConfigSetVolumes(const FString& Scope, FProsettaCliCompleted OnComplete) override
		{
			Ops.Add(Scope.IsEmpty() ? FString(TEXT("config clear")) : FString(TEXT("config set ")) + Scope);
			FProsettaCliResult Result;
			Result.bLaunched = true;
			Result.ExitCode = bFailConfig ? 1 : 0;
			if (bFailConfig) { Result.UserError = TEXT("config is read-only"); }
			OnComplete.ExecuteIfBound(Result);
			return 0;
		}
		virtual int32 Sync(const FProsettaSyncProfile&, FProsettaCliCompleted OnComplete, FProsettaCliLine) override
		{
			Ops.Add(TEXT("sync"));
			FProsettaCliResult Result;
			Result.bLaunched = true;
			Result.ExitCode = 0;
			OnComplete.ExecuteIfBound(Result);
			return 0;
		}
	};
}

DASHBOARD_TEST(FProsettaDashboardModelEmptyScopeSyncTest, "Model.EmptyScopeClearsConfigOnSync")
bool FProsettaDashboardModelEmptyScopeSyncTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	TSharedRef<FRecordingRunner> Runner = MakeShared<FRecordingRunner>();
	Fixture.Deps.Runner = Runner;
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();

	FString Error;
	TestTrue(TEXT("switch to core"), Model->SwitchVolume(TEXT("core"), TEXT(""), false, Error));
	TestEqual(TEXT("switch wrote the scope then synced"), FString::Join(Runner->Ops, TEXT(";")), FString(TEXT("config set core;sync")));
	TestEqual(TEXT("profile follows the swap"), State.Profile.ScopeString(), FString(TEXT("core")));
	Runner->Ops.Reset();

	// Unchanged non-empty scope: no redundant config write.
	FProsettaSyncProfile Edited = State.Profile;
	Edited.Mode = EProsettaSyncMode::SyncOnce;
	Model->SetProfile(Edited);
	Model->SyncOnce();
	TestEqual(TEXT("same scope only syncs"), FString::Join(Runner->Ops, TEXT(";")), FString(TEXT("sync")));
	Runner->Ops.Reset();

	// Untick the volume: the config still says core, so an empty scope must clear it before syncing.
	Edited.Volumes.Reset();
	Model->SetProfile(Edited);
	Model->SyncOnce();
	TestEqual(TEXT("empty scope clears the config, then syncs"), FString::Join(Runner->Ops, TEXT(";")), FString(TEXT("config clear;sync")));
	TestTrue(TEXT("config now known empty"), State.Credentials.ConfigVolumes.IsEmpty());
	Runner->Ops.Reset();

	Model->SyncOnce();
	TestEqual(TEXT("already clear: only syncs"), FString::Join(Runner->Ops, TEXT(";")), FString(TEXT("sync")));
	Runner->Ops.Reset();

	// A failed clear must not sync with the stale scope.
	TestTrue(TEXT("switch back to core"), Model->SwitchVolume(TEXT("core"), TEXT(""), false, Error));
	Runner->Ops.Reset();
	Edited = State.Profile;
	Edited.Volumes.Reset();
	Model->SetProfile(Edited);
	Runner->bFailConfig = true;
	Model->SyncOnce();
	TestEqual(TEXT("failed clear does not sync"), FString::Join(Runner->Ops, TEXT(";")), FString(TEXT("config clear")));
	TestTrue(TEXT("error surfaced"), State.LastError.Contains(TEXT("config is read-only")));
	TestFalse(TEXT("not left busy"), State.IsBusy());
	return true;
}

DASHBOARD_TEST(FProsettaDashboardModelEmptyScopeWatchTest, "Model.EmptyScopeClearsConfigBeforeWatch")
bool FProsettaDashboardModelEmptyScopeWatchTest::RunTest(const FString&)
{
	FModelFixture Fixture;
	TSharedRef<FRecordingRunner> Runner = MakeShared<FRecordingRunner>();
	Fixture.Deps.Runner = Runner;
	int32 Starts = 0;
	Fixture.Deps.StartWatch = [&Starts](const FProsettaSyncProfile&) { ++Starts; };
	TSharedRef<FProsettaDashboardModel> Model = Fixture.Make();
	const FProsettaDashboardState& State = Model->GetState();

	FString Error;
	TestTrue(TEXT("switch to core"), Model->SwitchVolume(TEXT("core"), TEXT(""), false, Error));
	Runner->Ops.Reset();
	FProsettaSyncProfile Edited = State.Profile;
	Edited.Mode = EProsettaSyncMode::Watch;
	Edited.Volumes.Reset();
	Model->SetProfile(Edited);

	Runner->bFailConfig = true;
	Model->StartWatch();
	TestEqual(TEXT("failed clear does not start the watch"), Starts, 0);
	TestFalse(TEXT("not left busy"), State.IsBusy());
	Runner->bFailConfig = false;
	Runner->Ops.Reset();

	Model->StartWatch();
	TestEqual(TEXT("empty scope clears the stale config scope"), FString::Join(Runner->Ops, TEXT(";")), FString(TEXT("config clear")));
	TestEqual(TEXT("then the watch starts"), Starts, 1);
	Runner->Ops.Reset();

	Model->StartWatch();
	TestTrue(TEXT("no second config write"), Runner->Ops.IsEmpty());
	TestEqual(TEXT("watch started again"), Starts, 2);
	return true;
}

// ------------------------------------------------------------------ widgets

DASHBOARD_TEST(FProsettaDashboardUIFirstRunTest, "UI.FirstRunChecklist")
bool FProsettaDashboardUIFirstRunTest::RunTest(const FString&)
{
	TSharedRef<FFakeDashboardModel> Model = MakeShared<FFakeDashboardModel>();
	Model->State.Connection = EProsettaDashboardConnection::NoCli;
	Model->State.StatusText = TEXT("Prosetta CLI not available");
	TSharedRef<SProsettaDashboard> Dashboard = SNew(SProsettaDashboard, Model);
	const TSharedRef<SWidget> Root = Dashboard;
	Layout(Dashboard);
	TestEqual(TEXT("first-run page"), Dashboard->ActivePage(), 0);
	TestFalse(TEXT("no cli"), Dashboard->HasCli());
	TestFalse(TEXT("no credentials"), Dashboard->HasCredentials());
	TestFalse(TEXT("no scope"), Dashboard->HasScope());
	TestNotNull(TEXT("checklist drawn"), FindByTag(Dashboard, TEXT("Prosetta.FirstRun")).Get());
	TestNotNull(TEXT("cli row"), FindByTag(Dashboard, TEXT("Prosetta.Check.Cli")).Get());

	TSharedPtr<SButton> Retry = FindButton(Dashboard, TEXT("Prosetta.FirstRunRetry"));
	TestNotNull(TEXT("retry button"), Retry.Get());
	if (Retry) { Retry->SimulateClick(); }
	TestEqual(TEXT("retry connects"), Model->Connects, 1);

	Model->State.Connection = EProsettaDashboardConnection::NoCredentials;
	Model->State.Credentials.bCliAvailable = true;
	Model->Broadcast(); Layout(Root);
	TestEqual(TEXT("still first run without credentials"), Dashboard->ActivePage(), 0);
	TestTrue(TEXT("cli now ticked"), Dashboard->HasCli());
	TestTrue(TEXT("credentials text explains"), !Dashboard->CredentialsItemText().IsEmpty());

	Model->State.Connection = EProsettaDashboardConnection::Connected;
	Model->State.Credentials.Source = TEXT("local");
	Model->State.Profile.Volumes = { FProsettaVolumeScope(TEXT("core")) };
	Model->Broadcast(); Layout(Root);
	TestEqual(TEXT("dashboard page once connected"), Dashboard->ActivePage(), 1);
	TestTrue(TEXT("scope from the profile"), Dashboard->HasScope());
	TestNotNull(TEXT("main page"), FindByTag(Dashboard, TEXT("Prosetta.Main")).Get());
	TestTrue(TEXT("panels built"), Dashboard->GetSettingsPanel().IsValid() && Dashboard->GetVolumesPanel().IsValid() && Dashboard->GetChangesLog().IsValid() && Dashboard->GetHeader().IsValid());

	Model->State.Connection = EProsettaDashboardConnection::Error;
	Model->State.LastError = TEXT("CLI error: boom");
	Model->State.StatusText = TEXT("CLI error: boom");
	Model->Broadcast(); Layout(Root);
	TestEqual(TEXT("error banner text"), Dashboard->ErrorBannerText().ToString(), FString(TEXT("CLI error: boom")));
	Model->State.Connection = EProsettaDashboardConnection::AuthFailed;
	Model->State.StatusText = TEXT("Prosetta rejected the token; update credentials.");
	Model->Broadcast(); Layout(Root);
	TestTrue(TEXT("banner joins status and error"), Dashboard->ErrorBannerText().ToString().Contains(TEXT("boom")));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardUIBannersTest, "UI.ExternalChangeBanner")
bool FProsettaDashboardUIBannersTest::RunTest(const FString&)
{
	TSharedRef<FFakeDashboardModel> Model = ConnectedFakeModel();
	TSharedRef<SProsettaDashboard> Dashboard = SNew(SProsettaDashboard, Model);
	const TSharedRef<SWidget> Root = Dashboard;
	Layout(Dashboard);
	TSharedPtr<SWidget> Banner = FindByTag(Dashboard, TEXT("Prosetta.Banner.External"));
	TestNotNull(TEXT("banner exists"), Banner.Get());
	if (!Banner) { return false; }
	TestEqual(TEXT("hidden without a change"), Banner->GetVisibility(), EVisibility::Collapsed);
	Model->State.bExternalChangePending = true;
	Model->Broadcast(); Layout(Root);
	TestEqual(TEXT("shown on change"), Banner->GetVisibility(), EVisibility::Visible);

	TSharedPtr<SButton> Dismiss = FindButton(Dashboard, TEXT("Prosetta.DismissExternal"));
	TestNotNull(TEXT("dismiss button"), Dismiss.Get());
	if (Dismiss) { Dismiss->SimulateClick(); }
	Layout(Root);
	TestEqual(TEXT("dismiss routed"), Model->Dismisses, 1);
	TestEqual(TEXT("hidden again"), Banner->GetVisibility(), EVisibility::Collapsed);
	TSharedPtr<SWidget> ErrorBanner = FindByTag(Dashboard, TEXT("Prosetta.Banner.Error"));
	TestTrue(TEXT("error banner hidden without an error"), ErrorBanner && ErrorBanner->GetVisibility() == EVisibility::Collapsed);

	Model->State.bExternalChangePending = true;
	Model->Broadcast(); Layout(Root);
	TSharedPtr<SButton> Apply = FindButton(Dashboard, TEXT("Prosetta.ApplyExternal"));
	TestNotNull(TEXT("apply button"), Apply.Get());
	TestTrue(TEXT("apply enabled outside Play"), Apply && Apply->IsEnabled());
	Model->State.bPIE = true;
	Model->Broadcast(); Layout(Root);
	TestFalse(TEXT("apply disabled during Play"), Apply && Apply->IsEnabled());
	Model->State.bPIE = false;
	Model->Broadcast(); Layout(Root);
	if (Apply) { Apply->SimulateClick(); }
	TestEqual(TEXT("apply routed"), Model->Applies, 1);

	TSharedPtr<SWidget> PIEBanner = FindByTag(Dashboard, TEXT("Prosetta.Banner.PIE"));
	TestNotNull(TEXT("pie banner"), PIEBanner.Get());
	Model->State.bPIE = true;
	Model->State.PendingCount = 1;
	Model->Broadcast(); Layout(Root);
	TestTrue(TEXT("pie banner shown with a queue"), PIEBanner && PIEBanner->GetVisibility() == EVisibility::Visible);
	return true;
}

DASHBOARD_TEST(FProsettaDashboardUISettingsTest, "UI.SettingsPanel")
bool FProsettaDashboardUISettingsTest::RunTest(const FString&)
{
	TSharedRef<FFakeDashboardModel> Model = ConnectedFakeModel();
	FProsettaVolumeSummary Core; Core.Slug = TEXT("core"); Core.Name = TEXT("Core");
	FProsettaVolumeSummary Side; Side.Slug = TEXT("side"); Side.Name = TEXT("Side story"); Side.Tags = { TEXT("1.0") };
	Model->State.Volumes.Volumes = { Core, Side };
	Model->State.bHasVolumes = true;
	TSharedRef<SProsettaSettingsPanel> Panel = SNew(SProsettaSettingsPanel, Model);
	const TSharedRef<SWidget> Root = Panel;
	Layout(Panel);

	TestEqual(TEXT("watch label"), Panel->RunLabel().ToString(), FString(TEXT("Start watch")));
	TestTrue(TEXT("can run when valid"), Panel->CanRun());
	TestEqual(TEXT("profile label"), Panel->ProfileLabel().ToString(), FString(TEXT("Default")));
	Model->State.bProfileDirty = true;
	Model->Broadcast(); Layout(Root);
	TestTrue(TEXT("dirty marker"), Panel->ProfileLabel().ToString().Contains(TEXT("•")));

	TMap<FString, bool> Rows = Panel->GetVolumeRows();
	TestEqual(TEXT("two volume rows"), Rows.Num(), 2);
	TestTrue(TEXT("core checked"), Rows.FindRef(TEXT("core")));
	TestFalse(TEXT("side unchecked"), Rows.FindRef(TEXT("side")));

	Panel->SetVolumeChecked(TEXT("side"), true);
	TestEqual(TEXT("edit routed"), Model->SetProfiles, 1);
	TestEqual(TEXT("two volumes"), Model->State.Profile.Volumes.Num(), 2);
	Panel->SetVolumeTag(TEXT("side"), TEXT("1.0"));
	TestTrue(TEXT("tag applied"), Model->State.Profile.IsFixedTag());
	TestFalse(TEXT("fixed tag blocks run"), Panel->CanRun());

	Panel->SetMode(EProsettaSyncMode::SyncOnce);
	TestEqual(TEXT("sync label"), Panel->RunLabel().ToString(), FString(TEXT("Sync now")));
	TestTrue(TEXT("hint empty"), Panel->ModeHint().IsEmpty());
	TestTrue(TEXT("can run again"), Panel->CanRun());
	Panel->SetMode(EProsettaSyncMode::Watch);
	TestEqual(TEXT("watch refused with fixed tag"), Model->State.Profile.Mode, EProsettaSyncMode::SyncOnce);
	TestTrue(TEXT("hint explains"), Panel->ModeHint().ToString().Contains(TEXT("release tag")));
	Panel->SetVolumeTag(TEXT("side"), TEXT(""));
	Panel->SetMode(EProsettaSyncMode::Watch);
	TestEqual(TEXT("watch accepted once cleared"), Model->State.Profile.Mode, EProsettaSyncMode::Watch);
	Panel->SetMode(EProsettaSyncMode::Verify);
	TestEqual(TEXT("verify label"), Panel->RunLabel().ToString(), FString(TEXT("Verify")));
	Panel->SetMode(EProsettaSyncMode::Watch);

	Panel->SetChannel(EProsettaWatchChannel::LatestRelease);
	TestEqual(TEXT("latest release trims to one volume"), Model->State.Profile.Volumes.Num(), 1);
	Panel->SetVolumeChecked(TEXT("side"), true);
	TestEqual(TEXT("single volume under latest release"), Model->State.Profile.Volumes.Num(), 1);
	TestEqual(TEXT("replaced by side"), Model->State.Profile.Volumes[0].Slug, FString(TEXT("side")));

	Model->State.bWatchRunning = true;
	Model->Broadcast(); Layout(Root);
	TestEqual(TEXT("restart label"), Panel->RunLabel().ToString(), FString(TEXT("Restart watch")));
	Model->State.Busy = TEXT("Syncing");
	Model->Broadcast(); Layout(Root);
	TestFalse(TEXT("busy blocks run"), Panel->CanRun());
	Model->State.Busy.Empty();
	Model->State.Credentials.Source = TEXT("none");
	Model->Broadcast(); Layout(Root);
	TestFalse(TEXT("no credentials blocks run"), Panel->CanRun());

	TSharedPtr<SButton> Run = FindButton(Panel, TEXT("Prosetta.Run"));
	TestNotNull(TEXT("run button"), Run.Get());
	Model->State.Credentials.Source = TEXT("local");
	Model->Broadcast(); Layout(Root);
	if (Run) { Run->SimulateClick(); }
	TestEqual(TEXT("run routed"), Model->Runs, 1);
	FString Error;
	TestTrue(TEXT("save as"), Panel->SaveProfileAs(TEXT("QA"), Error));
	TestEqual(TEXT("renamed"), Model->State.Profile.Name, FString(TEXT("QA")));
	TestNotNull(TEXT("edited struct"), Panel->GetEditedProfile());
	return true;
}

DASHBOARD_TEST(FProsettaDashboardUIVolumesTest, "UI.VolumesPanel")
bool FProsettaDashboardUIVolumesTest::RunTest(const FString&)
{
	FProsettaDashboardState State;
	FProsettaVolumeSummary Core; Core.Slug = TEXT("core"); Core.Name = TEXT("Core"); Core.bIsDefault = true;
	FProsettaVolumeSummary Side; Side.Slug = TEXT("side"); Side.Name = TEXT("Side story"); Side.Tags = { TEXT("1.0"), TEXT("1.1-rc") };
	FProsettaVolumeSummary Alpha; Alpha.Slug = TEXT("alpha"); Alpha.Name = TEXT("Alpha");
	State.Volumes.Volumes = { Side, Alpha, Core };
	State.bHasVolumes = true;
	TestTrue(TEXT("manifest parses"), UProsettaSyncInspector::ParseManifest(TEXT(R"({"schemaVersion":2,"projectId":1,"scope":"volumes=side@1.0","artifacts":{}})"), State.Manifest));
	State.bHasManifest = true;
	State.Profile.Volumes = { FProsettaVolumeScope(TEXT("alpha")) };

	TArray<TSharedPtr<FProsettaVolumeRow>> Rows = SProsettaVolumesPanel::BuildRows(State);
	TestEqual(TEXT("three rows"), Rows.Num(), 3);
	TestEqual(TEXT("default first"), Rows[0]->Volume.Slug, FString(TEXT("core")));
	TestEqual(TEXT("then alphabetical"), Rows[1]->Volume.Slug, FString(TEXT("alpha")));
	TestTrue(TEXT("alpha in profile"), Rows[1]->bInProfile);
	TestEqual(TEXT("alpha badge"), Rows[1]->Badges(), FString(TEXT("profile")));
	TestTrue(TEXT("side mirrored"), Rows[2]->bMirrored);
	TestEqual(TEXT("side mirrored tag"), Rows[2]->MirroredTag, FString(TEXT("1.0")));
	TestTrue(TEXT("side canonical (single scope)"), Rows[2]->bCanonical);
	TestEqual(TEXT("side badges"), Rows[2]->Badges(), FString(TEXT("mirrored@1.0 · canonical")));
	TestTrue(TEXT("core has no badge"), Rows[0]->Badges().IsEmpty());

	TestTrue(TEXT("empty filter matches"), SProsettaVolumesPanel::MatchesFilter(*Rows[0], TEXT("")));
	TestTrue(TEXT("slug match"), SProsettaVolumesPanel::MatchesFilter(*Rows[2], TEXT("sid")));
	TestTrue(TEXT("name match"), SProsettaVolumesPanel::MatchesFilter(*Rows[2], TEXT("story")));
	TestTrue(TEXT("tag match"), SProsettaVolumesPanel::MatchesFilter(*Rows[2], TEXT("1.1")));
	TestFalse(TEXT("no match"), SProsettaVolumesPanel::MatchesFilter(*Rows[0], TEXT("zzz")));

	// Without a volumes listing the manifest scope seeds stub rows.
	FProsettaDashboardState Offline = State;
	Offline.bHasVolumes = false;
	Offline.Volumes.Volumes.Reset();
	Rows = SProsettaVolumesPanel::BuildRows(Offline);
	TestEqual(TEXT("one stub row"), Rows.Num(), 1);
	TestEqual(TEXT("stub slug"), Rows[0]->Volume.Slug, FString(TEXT("side")));
	TestTrue(TEXT("stub carries the tag"), Rows[0]->Volume.Tags.Contains(TEXT("1.0")));

	TSharedRef<FFakeDashboardModel> Model = ConnectedFakeModel();
	Model->State.Volumes = State.Volumes;
	Model->State.bHasVolumes = true;
	TSharedRef<SProsettaVolumesPanel> Panel = SNew(SProsettaVolumesPanel, Model);
	Layout(Panel);
	TestEqual(TEXT("all rows"), Panel->GetRows().Num(), 3);
	Panel->SetFilter(TEXT("side"));
	TestEqual(TEXT("filtered"), Panel->GetRows().Num(), 1);
	Panel->SetFilter(TEXT(""));
	TestEqual(TEXT("unfiltered"), Panel->GetRows().Num(), 3);
	TSharedPtr<SButton> Refresh = FindButton(Panel, TEXT("Prosetta.VolumesRefresh"));
	TestNotNull(TEXT("refresh button"), Refresh.Get());
	if (Refresh) { Refresh->SimulateClick(); }
	TestEqual(TEXT("refresh routed"), Model->RefreshVolumesCalls, 1);
	TestNotNull(TEXT("list drawn"), FindByTag(Panel, TEXT("Prosetta.VolumesList")).Get());

	// The switch dialog body and the shared Perform action.
	TSharedRef<FProsettaVolumeSwitchDialog::SBody> Body = SNew(FProsettaVolumeSwitchDialog::SBody, Side, false);
	TestEqual(TEXT("latest is the empty tag"), Body->GetSelectedTag(), FString());
	Body->SetSelectedTag(TEXT("1.1-rc"));
	TestEqual(TEXT("tag selected"), Body->GetSelectedTag(), FString(TEXT("1.1-rc")));
	Body->SetSelectedTag(TEXT("nope"));
	TestEqual(TEXT("unlisted tag is appended"), Body->GetSelectedTag(), FString(TEXT("nope")));
	Body->SetSelectedTag(TEXT(""));
	TestEqual(TEXT("back to latest"), Body->GetSelectedTag(), FString());
	TestFalse(TEXT("repoint off by default"), Body->GetRepoint());
	const FString Explanation = FProsettaVolumeSwitchDialog::SBody::Explanation(Side, true).ToString();
	TestTrue(TEXT("explains the config command"), Explanation.Contains(TEXT("config set volumes side")));
	TestTrue(TEXT("explains Play"), Explanation.Contains(TEXT("Play")));
	TestFalse(TEXT("promises no remote write"), !Explanation.Contains(TEXT("Nothing is written to Prosetta")));

	TestTrue(TEXT("perform switches"), FProsettaVolumeSwitchDialog::Perform(Model, TEXT("side"), TEXT("1.0"), true, false));
	TestEqual(TEXT("switch routed"), Model->SwitchCalls, 1);
	TestEqual(TEXT("slug"), Model->LastSwitchSlug, FString(TEXT("side")));
	TestEqual(TEXT("tag"), Model->LastSwitchTag, FString(TEXT("1.0")));
	TestTrue(TEXT("repoint"), Model->bLastSwitchRepoint);
	Model->bRefuseSwitch = true;
	TestFalse(TEXT("refusal reported"), FProsettaVolumeSwitchDialog::Perform(Model, TEXT("side"), TEXT(""), false, false));
	return true;
}

DASHBOARD_TEST(FProsettaDashboardUICredentialsTest, "UI.CredentialsForm")
bool FProsettaDashboardUICredentialsTest::RunTest(const FString&)
{
	TestTrue(TEXT("https"), SProsettaCredentialsForm::IsValidBaseUrl(TEXT("https://api.example.invalid")));
	TestTrue(TEXT("http"), SProsettaCredentialsForm::IsValidBaseUrl(TEXT(" http://localhost:3000 ")));
	TestFalse(TEXT("scheme-less"), SProsettaCredentialsForm::IsValidBaseUrl(TEXT("api.example.invalid")));
	TestFalse(TEXT("bare scheme"), SProsettaCredentialsForm::IsValidBaseUrl(TEXT("https://")));
	TestTrue(TEXT("relay state under Saved"), SProsettaCredentialsForm::RelayStatePath().Contains(TEXT("ProsettaSync")));

	TSharedRef<FFakeDashboardModel> Model = MakeShared<FFakeDashboardModel>();
	Model->State.Connection = EProsettaDashboardConnection::NoCredentials;
	Model->State.Profile.BaseUrl = TEXT("https://api.example.invalid");
	int32 Submitted = 0;
	TSharedRef<SProsettaCredentialsForm> Form = SNew(SProsettaCredentialsForm, Model).OnSubmitted_Lambda([&Submitted]() { ++Submitted; });
	Layout(Form);
	TestEqual(TEXT("base url seeded from the profile"), Form->GetBaseUrl(), FString(TEXT("https://api.example.invalid")));
	TestFalse(TEXT("invalid without id and token"), Form->IsValid());
	TestFalse(TEXT("submit refused"), Form->Submit());
	TestEqual(TEXT("nothing written"), Model->WriteCredentialsCalls, 0);

	Form->SetProjectId(TEXT("42"));
	Form->SetToken(TEXT("placeholder-token-value"));
	TestTrue(TEXT("has token"), Form->HasToken());
	TestTrue(TEXT("valid"), Form->IsValid());
	const FString Preview = Form->CommandPreview();
	TestEqual(TEXT("preview"), Preview, FString(TEXT("prosetta.py init --base-url https://api.example.invalid --project-id 42 --token-stdin")));
	TestFalse(TEXT("preview never shows the token"), Preview.Contains(TEXT("placeholder-token-value")));

	TestTrue(TEXT("submit"), Form->Submit());
	TestEqual(TEXT("written once"), Model->WriteCredentialsCalls, 1);
	TestEqual(TEXT("base url passed"), Model->LastWrittenBaseUrl, FString(TEXT("https://api.example.invalid")));
	TestEqual(TEXT("project passed"), Model->LastWrittenProjectId, FString(TEXT("42")));
	TestEqual(TEXT("token passed by length only"), Model->LastWrittenTokenLength, 23);
	TestFalse(TEXT("token box cleared"), Form->HasToken());
	TestEqual(TEXT("submitted event"), Submitted, 1);

	Form->SetBaseUrl(SProsettaCredentialsForm::StagingRelayUrl);
	TestEqual(TEXT("preset url"), Form->GetBaseUrl(), FString(SProsettaCredentialsForm::StagingRelayUrl));
	TSharedPtr<SWidget> Preset = FindByTag(Form, TEXT("Prosetta.PresetNote"));
	TestNotNull(TEXT("preset note drawn"), Preset.Get());
	return true;
}

DASHBOARD_TEST(FProsettaDashboardUIChangesTest, "UI.ChangesLogAndHeader")
bool FProsettaDashboardUIChangesTest::RunTest(const FString&)
{
	TSharedRef<FFakeDashboardModel> Model = ConnectedFakeModel();
	TSharedRef<SProsettaChangesLog> Log = SNew(SProsettaChangesLog, Model);
	TSharedRef<SWidget> Root = Log;
	Layout(Log);
	TestEqual(TEXT("empty title"), Log->Title().ToString(), FString(TEXT("Recent changes (0)")));
	TestEqual(TEXT("no rows"), Log->NumRows(), 0);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FProsettaChangeEntry Entry;
		Entry.Kind = static_cast<EProsettaChangeKind>(Index);
		Entry.Message = FString::FromInt(Index);
		Model->State.Changes.Add(Entry);
	}
	Model->Broadcast(); Layout(Root);
	TestEqual(TEXT("title counts"), Log->Title().ToString(), FString(TEXT("Recent changes (3)")));
	TestEqual(TEXT("rows follow"), Log->NumRows(), 3);
	Log->Expand();
	TestTrue(TEXT("expanded"), Log->IsExpanded());
	for (EProsettaChangeKind Kind : { EProsettaChangeKind::Generation, EProsettaChangeKind::Sync, EProsettaChangeKind::Verify, EProsettaChangeKind::Error, EProsettaChangeKind::Blocked, EProsettaChangeKind::Info, EProsettaChangeKind::ExternalChange })
	{
		TestNotNull(TEXT("icon per kind"), SProsettaChangesLog::IconFor(Kind));
	}
	TSharedPtr<SButton> Clear = FindButton(Log, TEXT("Prosetta.ChangesClear"));
	TestNotNull(TEXT("clear button"), Clear.Get());
	if (Clear) { Clear->SimulateClick(); }
	TestEqual(TEXT("clear routed"), Model->Clears, 1);
	TestEqual(TEXT("rows cleared"), Log->NumRows(), 0);

	TSharedRef<SProsettaConnectionHeader> Header = SNew(SProsettaConnectionHeader, Model);
	Root = Header;
	Layout(Header);
	TestEqual(TEXT("no mirror age"), Header->AgeText().ToString(), FString(TEXT("no mirror yet")));
	TestTrue(TEXT("scope line from profile"), Header->ScopeLine().ToString().Contains(TEXT("core")));
	TestTrue(TEXT("scope line has project"), Header->ScopeLine().ToString().Contains(TEXT("Sample")));
	TestTrue(TEXT("scope line has channel"), Header->ScopeLine().ToString().Contains(Model->State.Profile.ChannelName()));

	TestTrue(TEXT("manifest parses"), UProsettaSyncInspector::ParseManifest(TEXT(R"({"schemaVersion":2,"projectId":1,"projectName":"FromManifest","scope":"volumes=core@1.0","artifacts":{"a":{"relativePath":"a","checksum":"sha256:1234"}}})"), Model->State.Manifest));
	Model->State.bHasManifest = true;
	Model->State.DataAge = TEXT("5 min ago");
	Model->State.DataAgeMinutes = 5;
	Model->Broadcast(); Layout(Root);
	TestEqual(TEXT("age text"), Header->AgeText().ToString(), FString(TEXT("synced 5 min ago")));
	TestTrue(TEXT("fresh is gray"), Header->AgeColor() == FSlateColor(FStyleColors::AccentGray));
	Model->State.DataAgeMinutes = 45;
	TestTrue(TEXT("stale is amber"), Header->AgeColor() == FSlateColor(FStyleColors::Warning));
	TestTrue(TEXT("scope line prefers the manifest"), Header->ScopeLine().ToString().Contains(TEXT("FromManifest")) && Header->ScopeLine().ToString().Contains(TEXT("core@1.0")));
	TSharedPtr<SButton> Reconnect = FindButton(Header, TEXT("Prosetta.Reconnect"));
	TestNotNull(TEXT("reconnect button"), Reconnect.Get());
	if (Reconnect) { Reconnect->SimulateClick(); }
	TestEqual(TEXT("reconnect routed"), Model->Connects, 1);
	TSharedRef<SWidget> Menu = Header->MakeConnectionMenu();
	TestTrue(TEXT("menu builds"), Menu->GetChildren() != nullptr);
	return true;
}

DASHBOARD_TEST(FProsettaDashboardTabTest, "Tab.Registered")
bool FProsettaDashboardTabTest::RunTest(const FString&)
{
	FProsettaDashboardTab::Register();
	TestTrue(TEXT("tab spawner registered"), FGlobalTabmanager::Get()->HasTabSpawner(FProsettaDashboardTab::TabId));
	TestEqual(TEXT("tab id"), FProsettaDashboardTab::TabId, FName(TEXT("ProsettaDashboard")));
	// Register twice is harmless (the module and this test may both call it).
	FProsettaDashboardTab::Register();
	TestTrue(TEXT("still registered"), FGlobalTabmanager::Get()->HasTabSpawner(FProsettaDashboardTab::TabId));
	return true;
}

#undef DASHBOARD_TEST
#endif // WITH_DEV_AUTOMATION_TESTS
