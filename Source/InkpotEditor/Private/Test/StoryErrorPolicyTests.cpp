#if !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/InkpotStories.h"
#include "Inkpot/NativeBindings/InkpotNativeBinding.h"
#include "Asset/InkpotStoryAsset.h"
#include "Inkpot/InkpotStoryInternal.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "Inkpot/Prosetta/InkpotProsettaLibrary.h"
#include "Settings/InkpotSettings.h"
#include "Ink/Object.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"

// Registered with arity 2; ArityStoryJSON calls it with one argument.
namespace Prosetta::Bindings::InkpotPolicyTest
{
	int inkpot_policy_arity(int a, int b) { return a + b; }
}

INKPOT_NATIVE_BINDING_MODULE_BEGIN(InkpotPolicyTest, EInkpotNativeBindingSource::Local, "")
	INKPOT_NATIVE_BINDING(inkpot_policy_arity, true)
INKPOT_NATIVE_BINDING_MODULE_END(InkpotPolicyTest)

namespace
{
	// Prints "ok"; the mis-matched EXTERNAL call sits in a knot that is never reached.
	const TCHAR* ArityStoryJSON = TEXT("{\"inkVersion\":21,\"root\":[[\"^ok\",\"\\n\",\"end\",null],\"done\",{\"unused\":[\"ev\",1,{\"x()\":\"inkpot_policy_arity\",\"exArgs\":1},\"pop\",\"/ev\",\"end\",null]}],\"listDefs\":{}}");

	// Calls one unbound EXTERNAL so validation has something to report.
	const TCHAR* UnboundStoryJSON = TEXT("{\"inkVersion\":21,\"root\":[[\"ev\",{\"x()\":\"inkpot_policy_missing\",\"exArgs\":0},\"pop\",\"/ev\",\"^ok\",\"\\n\",\"end\",null],\"done\",null],\"listDefs\":{}}");

	struct FScopedSettings
	{
		UInkpotSettings* Settings;
		EInkpotStoryErrorAction Error, Warning;
		bool bMessageLog, bScreen, bProvenance, bValidateBindings;
		FScopedSettings()
		: Settings(GetMutableDefault<UInkpotSettings>()), Error(Settings->StoryErrorAction), Warning(Settings->StoryWarningAction),
		  bMessageLog(Settings->bStoryErrorsToMessageLog), bScreen(Settings->bStoryErrorsOnScreen), bProvenance(Settings->bIncludeProsettaProvenanceInLogs),
		  bValidateBindings(Settings->bValidateExternalBindingsOnFirstContinue) {}
		~FScopedSettings()
		{
			Settings->StoryErrorAction = Error;
			Settings->StoryWarningAction = Warning;
			Settings->bStoryErrorsToMessageLog = bMessageLog;
			Settings->bStoryErrorsOnScreen = bScreen;
			Settings->bIncludeProsettaProvenanceInLogs = bProvenance;
			Settings->bValidateExternalBindingsOnFirstContinue = bValidateBindings;
		}
	};

	FProsettaStoryProvenance SampleProvenance()
	{
		FProsettaStoryProvenance provenance;
		provenance.ProjectId = 42;
		provenance.Channel = TEXT("live");
		provenance.VolumeSlug = TEXT("core");
		provenance.Revision = TEXT("17");
		provenance.GenerationId = TEXT("9f3c1a2b4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8");
		provenance.BuildId = TEXT("77aa01b2c3d4e5f60718293a4b5c6d7e8f9a0b1c2d3e4f5061728394a5b6c7d8");
		return provenance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotStoryErrorPolicyTest, "Inkpot.Prosetta.StoryErrorPolicy.Actions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotStoryErrorPolicyTest::RunTest(const FString& InParameters)
{
	FScopedSettings scoped;
	scoped.Settings->bStoryErrorsOnScreen = false;
	scoped.Settings->bIncludeProsettaProvenanceInLogs = true;

	TSharedPtr<FInkpotStoryInternal> internal = MakeShared<FInkpotStoryInternal>(FString(UnboundStoryJSON), 4242);
	if (!TestTrue(TEXT("story loads"), internal->IsValidStory())) return false;
	TStrongObjectPtr<UInkpotStory> story(NewObject<UInkpotStory>(GetTransientPackage()));
	story->Initialise(internal);
	story->SetProsettaProvenance(SampleProvenance());

	TestEqual(TEXT("log context carries provenance"), story->GetLogContext(), FString(TEXT("story 4242 | Prosetta project 42 live core@17 gen 9f3c1a2b build 77aa01b2")));
	scoped.Settings->bIncludeProsettaProvenanceInLogs = false;
	TestEqual(TEXT("provenance prefix can be disabled"), story->GetLogContext(), FString(TEXT("story 4242")));
	scoped.Settings->bIncludeProsettaProvenanceInLogs = true;

	int32 broadcasts = 0;
	EInkpotStoryErrorType lastType = EInkpotStoryErrorType::Author;
	FString lastMessage;
	story->OnStoryError().AddLambda([&](UInkpotStory*, const FString& Message, EInkpotStoryErrorType Type) { ++broadcasts; lastType = Type; lastMessage = Message; });

	// Log: the message reaches the Output Log with the story / provenance prefix and is broadcast.
	scoped.Settings->StoryErrorAction = EInkpotStoryErrorAction::Log;
	TArray<FString> unbound;
	AddExpectedError(TEXT("[Inkpot story 4242 | Prosetta project 42 live core@17 gen 9f3c1a2b build 77aa01b2] Calls EXTERNAL 'inkpot_policy_missing'"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestFalse(TEXT("validation reports the unbound external"), story->ValidateExternalBindings(unbound, true));
	TestEqual(TEXT("broadcast once"), broadcasts, 1);
	TestTrue(TEXT("broadcast type"), lastType == EInkpotStoryErrorType::Error);
	TestTrue(TEXT("broadcast message is unprefixed"), lastMessage.StartsWith(TEXT("Calls EXTERNAL")));

	// BroadcastOnly: nothing is logged, the event still fires.
	scoped.Settings->StoryErrorAction = EInkpotStoryErrorAction::BroadcastOnly;
	TestFalse(TEXT("validation still fails"), story->ValidateExternalBindings(unbound, true));
	TestEqual(TEXT("broadcast again without logging"), broadcasts, 2);

	// Warnings follow their own action; ReportStoryError is the public entry point.
	scoped.Settings->StoryWarningAction = EInkpotStoryErrorAction::Log;
	AddExpectedMessage(TEXT("[Inkpot story 4242 | Prosetta project 42 live core@17 gen 9f3c1a2b build 77aa01b2] custom warning"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	story->ReportStoryError(TEXT("custom warning"), EInkpotStoryErrorType::Warning);
	TestEqual(TEXT("warning broadcast"), broadcasts, 3);
	TestTrue(TEXT("warning type"), lastType == EInkpotStoryErrorType::Warning);
	scoped.Settings->StoryWarningAction = EInkpotStoryErrorAction::BroadcastOnly;
	story->ReportStoryError(TEXT("silent warning"), EInkpotStoryErrorType::Warning);
	TestEqual(TEXT("silent warning broadcast"), broadcasts, 4);

	// InkPlusPlus exception-substitute messages are attributed to the executing story.
	scoped.Settings->StoryErrorAction = EInkpotStoryErrorAction::Log;
	scoped.Settings->bValidateExternalBindingsOnFirstContinue = false;
	AddExpectedError(TEXT("[Inkpot story 4242 | Prosetta project 42 live core@17 gen 9f3c1a2b build 77aa01b2] Ink runtime exception: Cannot observe variable 'nope'"), EAutomationExpectedErrorFlags::Contains, 1, false);
	story->ObserveVariableNative(TEXT("nope"), [](UInkpotStory*, const FString&, const FInkpotValue&) {});
	TestEqual(TEXT("exception broadcast"), broadcasts, 5);
	TestTrue(TEXT("exception message"), lastMessage.Contains(TEXT("Cannot observe variable 'nope'")));
	scoped.Settings->bValidateExternalBindingsOnFirstContinue = true;
	TestTrue(TEXT("exception hook installed"), Ink::FObject::StoryException.OnThrow.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotStoryErrorLegacyConfigTest, "Inkpot.Prosetta.StoryErrorPolicy.LegacyConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotStoryErrorLegacyConfigTest::RunTest(const FString& InParameters)
{
	FScopedSettings scoped;
	UInkpotSettings* settings = scoped.Settings;
	const FString filename = settings->GetClass()->GetConfigName();
	const FString realSection = settings->GetClass()->GetPathName();
	if (!TestNotNull(TEXT("GConfig"), GConfig)) return false;

	// Without a DefaultInkpot.ini the branch is not in GConfig and writes are dropped, so
	// register an empty in-memory file for the duration of the test.
	const bool bAddedFile = GConfig->Find(filename) == nullptr;
	if (bAddedFile)
		GConfig->Add(filename, FConfigFile());
	ON_SCOPE_EXIT { if (bAddedFile) GConfig->Remove(filename); };
	if (!TestNotNull(TEXT("settings config file is available"), GConfig->Find(filename))) return false;
	TestEqual(TEXT("settings section"), realSection, FString(TEXT("/Script/Inkpot.InkpotSettings")));

	auto reset = [settings]()
	{
		settings->StoryErrorAction = EInkpotStoryErrorAction::Log;
		settings->StoryWarningAction = EInkpotStoryErrorAction::Log;
	};

	// A throwaway section in the settings' own config file, removed whatever happens.
	const FString section = TEXT("/Script/Inkpot.InkpotSettings_LegacyConfigTest");
	ON_SCOPE_EXIT { GConfig->RemoveSectionFromBranch(*section, *filename); };

	reset();
	TestFalse(TEXT("no legacy key: nothing to migrate"), settings->ApplyLegacyStoryErrorConfig(section, filename));
	TestTrue(TEXT("no legacy key: error stays Log"), settings->StoryErrorAction == EInkpotStoryErrorAction::Log);

	GConfig->SetBool(*section, TEXT("bLogStoryErrors"), true, filename);
	reset();
	TestTrue(TEXT("legacy true is recognised"), settings->ApplyLegacyStoryErrorConfig(section, filename));
	TestTrue(TEXT("legacy true: error stays Log"), settings->StoryErrorAction == EInkpotStoryErrorAction::Log);
	TestTrue(TEXT("legacy true: warning stays Log"), settings->StoryWarningAction == EInkpotStoryErrorAction::Log);

	GConfig->SetBool(*section, TEXT("bLogStoryErrors"), false, filename);
	reset();
	TestTrue(TEXT("legacy false is recognised"), settings->ApplyLegacyStoryErrorConfig(section, filename));
	TestTrue(TEXT("legacy false: error is BroadcastOnly"), settings->StoryErrorAction == EInkpotStoryErrorAction::BroadcastOnly);
	TestTrue(TEXT("legacy false: warning is BroadcastOnly"), settings->StoryWarningAction == EInkpotStoryErrorAction::BroadcastOnly);

	// An explicit StoryErrorAction in config wins ( LoadConfig already applied it to the property ).
	GConfig->SetString(*section, TEXT("StoryErrorAction"), TEXT("Ensure"), filename);
	reset();
	settings->StoryErrorAction = EInkpotStoryErrorAction::Ensure;
	TestTrue(TEXT("explicit action: legacy still recognised"), settings->ApplyLegacyStoryErrorConfig(section, filename));
	TestTrue(TEXT("explicit error action is kept"), settings->StoryErrorAction == EInkpotStoryErrorAction::Ensure);
	TestTrue(TEXT("unset warning action migrates"), settings->StoryWarningAction == EInkpotStoryErrorAction::BroadcastOnly);

	// The real settings section, as PostInitProperties reads it; any existing value is restored.
	FString previous;
	const bool bHadPrevious = GConfig->GetString(*realSection, TEXT("bLogStoryErrors"), previous, filename);
	ON_SCOPE_EXIT
	{
		if (bHadPrevious)
			GConfig->SetString(*realSection, TEXT("bLogStoryErrors"), *previous, filename);
		else
			GConfig->RemoveKey(*realSection, TEXT("bLogStoryErrors"), filename);
	};
	FString explicitError, explicitWarning;
	const bool bRealHasError = GConfig->GetString(*realSection, TEXT("StoryErrorAction"), explicitError, filename);
	const bool bRealHasWarning = GConfig->GetString(*realSection, TEXT("StoryWarningAction"), explicitWarning, filename);
	GConfig->SetBool(*realSection, TEXT("bLogStoryErrors"), false, filename);
	reset();
	TestTrue(TEXT("real section: legacy recognised"), settings->ApplyLegacyStoryErrorConfig(realSection, filename));
	TestTrue(TEXT("real section: error migrates unless explicit"), settings->StoryErrorAction == (bRealHasError ? EInkpotStoryErrorAction::Log : EInkpotStoryErrorAction::BroadcastOnly));
	TestTrue(TEXT("real section: warning migrates unless explicit"), settings->StoryWarningAction == (bRealHasWarning ? EInkpotStoryErrorAction::Log : EInkpotStoryErrorAction::BroadcastOnly));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotStoryErrorPolicyBindingTest, "Inkpot.Prosetta.StoryErrorPolicy.BeginStoryBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotStoryErrorPolicyBindingTest::RunTest(const FString& InParameters)
{
	FScopedSettings scoped;
	scoped.Settings->bStoryErrorsOnScreen = false;
	scoped.Settings->bStoryErrorsToMessageLog = false;
	scoped.Settings->StoryErrorAction = EInkpotStoryErrorAction::Log;
	const bool bAutoBind = scoped.Settings->bAutoBindNativeFunctions;
	const bool bValidate = scoped.Settings->bValidateExternalBindingsOnFirstContinue;
	scoped.Settings->bAutoBindNativeFunctions = true;
	scoped.Settings->bValidateExternalBindingsOnFirstContinue = true;
	ON_SCOPE_EXIT
	{
		scoped.Settings->bAutoBindNativeFunctions = bAutoBind;
		scoped.Settings->bValidateExternalBindingsOnFirstContinue = bValidate;
	};

	TStrongObjectPtr<UInkpotStories> stories(NewObject<UInkpotStories>(GetTransientPackage()));
	stories->Initialise();
	UInkpotStoryAsset* asset = NewObject<UInkpotStoryAsset>(GetTransientPackage());
	asset->SetCompiledJSON(ArityStoryJSON);

	// The mismatch is logged while BeginStory binds, exactly as before ( and once more by the direct bind below ) ...
	AddExpectedError(TEXT("Native function 'inkpot_policy_arity' ( module InkpotPolicyTest ) takes 2 parameter(s) but the story calls it with 1"), EAutomationExpectedErrorFlags::Contains, 2, false);
	UInkpotStory* story = stories->BeginStory(asset);
	if (!TestTrue(TEXT("story began"), IsValid(story) && story->GetID() != FInkpotStoryInternal::BadStoryHandle)) return false;
	TestEqual(TEXT("binding error held for subscribers"), story->GetPendingStoryErrorCount(), 1);

	// ... and broadcast to a handler bound after BeginStory, at the first Continue.
	TArray<FString> messages;
	story->OnStoryError().AddLambda([&messages](UInkpotStory*, const FString& Message, EInkpotStoryErrorType Type)
	{
		if (Type == EInkpotStoryErrorType::Error) messages.Add(Message);
	});
	TestEqual(TEXT("nothing broadcast before Continue"), messages.Num(), 0);

	// First-Continue validation follows the flushed binding error: the external stayed unbound.
	AddExpectedError(TEXT("Calls EXTERNAL 'inkpot_policy_arity'"), EAutomationExpectedErrorFlags::Contains, 1, false);
	const FString output = story->Continue();
	TestEqual(TEXT("story still plays"), output, FString(TEXT("ok\n")));
	if (TestEqual(TEXT("binding error and validation error broadcast"), messages.Num(), 2))
	{
		TestTrue(TEXT("binding error first"), messages[0].StartsWith(TEXT("Native function 'inkpot_policy_arity'")));
		TestTrue(TEXT("validation error second"), messages[1].StartsWith(TEXT("Calls EXTERNAL 'inkpot_policy_arity'")));
	}
	TestEqual(TEXT("queue drained"), story->GetPendingStoryErrorCount(), 0);

	// Outside BeginStory / Reload, binding errors are still broadcast immediately.
	story->BindRegisteredNativeFunctions(TArray<FString>(), false);
	TestEqual(TEXT("direct bind not deferred"), story->GetPendingStoryErrorCount(), 0);
	TestEqual(TEXT("direct bind broadcast"), messages.Num(), 3);

	stories->EndStory(story);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaStoryProvenanceTest, "Inkpot.Prosetta.StoryProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaStoryProvenanceTest::RunTest(const FString& InParameters)
{
	// Unscoped sync mirror manifest ( schema 2 ) with a map revision.
	{
		const FString manifest = TEXT(
			"{\"schemaVersion\":2,\"generatedAt\":\"2026-10-10T12:00:00.000Z\",\"projectId\":42,\"projectIdentifier\":\"neo-cab\",\"projectName\":\"Neo Cab\","
			"\"generation\":{\"id\":\"9f3c1a2b4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8\",\"revision\":{\"42\":\"17\"}},"
			"\"artifacts\":[{\"key\":\"lines\",\"relativePath\":\"lines/lines.json\",\"checksum\":\"aa\"},{\"key\":\"compiled-story\",\"relativePath\":\"compiled/story.json\",\"checksum\":\"77AA01B2C3\"}]}");
		FProsettaStoryProvenance provenance;
		TestTrue(TEXT("unscoped manifest parses"), FProsettaStoryProvenance::FromSyncManifestJSON(manifest, FString(), provenance));
		TestTrue(TEXT("set"), provenance.IsSet());
		TestEqual(TEXT("project id"), provenance.ProjectId, 42);
		TestEqual(TEXT("identifier"), provenance.ProjectIdentifier, FString(TEXT("neo-cab")));
		TestEqual(TEXT("generation"), provenance.GenerationId.Left(8), FString(TEXT("9f3c1a2b")));
		TestEqual(TEXT("revision from map"), provenance.Revision, FString(TEXT("17")));
		TestEqual(TEXT("build id lower-cased"), provenance.BuildId, FString(TEXT("77aa01b2c3")));
		TestTrue(TEXT("no volume"), provenance.VolumeSlug.IsEmpty());
		TestEqual(TEXT("generated at"), provenance.GeneratedAt, FString(TEXT("2026-10-10T12:00:00.000Z")));
		TestEqual(TEXT("log string"), provenance.ToLogString(), FString(TEXT("Prosetta project 42 rev@17 gen 9f3c1a2b build 77aa01b2")));
	}

	// Volume scoped manifest with two volumes: the slug selects the build id; no slug stays ambiguous.
	{
		const FString manifest = TEXT(
			"{\"schemaVersion\":2,\"projectId\":42,\"projectName\":\"Neo Cab\",\"scope\":{\"mode\":\"volumes\",\"canonical\":\"volumes=core,side\",\"volumes\":["
			"{\"volumeId\":\"v1\",\"slug\":\"core\",\"tag\":\"v1.2.0\",\"isDefault\":true,\"compiledStoryArtifactKey\":\"compiled-story:core\"},"
			"{\"volumeId\":\"v2\",\"slug\":\"side\",\"tag\":null,\"isDefault\":false,\"compiledStoryArtifactKey\":\"compiled-story:side\"}]},"
			"\"generation\":{\"id\":\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\",\"revision\":\"5\"},"
			"\"artifacts\":[{\"key\":\"compiled-story:core\",\"checksum\":\"c0re\"},{\"key\":\"compiled-story:side\",\"checksum\":\"51de\"}]}");
		FProsettaStoryProvenance core, ambiguous;
		TestTrue(TEXT("core parses"), FProsettaStoryProvenance::FromSyncManifestJSON(manifest, TEXT("core"), core));
		TestEqual(TEXT("core slug"), core.VolumeSlug, FString(TEXT("core")));
		TestEqual(TEXT("core tag"), core.VolumeTag, FString(TEXT("v1.2.0")));
		TestEqual(TEXT("core build"), core.BuildId, FString(TEXT("c0re")));
		TestEqual(TEXT("string revision"), core.Revision, FString(TEXT("5")));
		TestEqual(TEXT("scope canonical"), core.Scope, FString(TEXT("volumes=core,side")));
		TestEqual(TEXT("core log string"), core.ToLogString(), FString(TEXT("Prosetta project 42 core#v1.2.0@5 gen abcdef01 build c0re")));
		TestTrue(TEXT("ambiguous parses"), FProsettaStoryProvenance::FromSyncManifestJSON(manifest, FString(), ambiguous));
		TestTrue(TEXT("ambiguous has no build id"), ambiguous.BuildId.IsEmpty());
		TestTrue(TEXT("ambiguous has no volume"), ambiguous.VolumeSlug.IsEmpty());
	}

	// Watch event adds channel, release and fills gaps.
	{
		TSharedPtr<FJsonObject> manifest = MakeShared<FJsonObject>();
		manifest->SetNumberField(TEXT("schemaVersion"), 2);
		manifest->SetNumberField(TEXT("projectId"), 7);
		TSharedPtr<FJsonObject> event = MakeShared<FJsonObject>();
		event->SetNumberField(TEXT("projectId"), 7);
		event->SetStringField(TEXT("channel"), TEXT("latest-release"));
		event->SetStringField(TEXT("scope"), TEXT("volumes=core"));
		event->SetStringField(TEXT("generationId"), TEXT("0011223344556677889900112233445566778899001122334455667788990011"));
		TSharedPtr<FJsonObject> revision = MakeShared<FJsonObject>();
		revision->SetStringField(TEXT("7"), TEXT("3"));
		event->SetObjectField(TEXT("revision"), revision);
		TSharedPtr<FJsonObject> release = MakeShared<FJsonObject>();
		release->SetStringField(TEXT("tagId"), TEXT("tag_01HZ"));
		release->SetStringField(TEXT("publishedAt"), TEXT("2026-10-09T08:00:00.000Z"));
		event->SetObjectField(TEXT("release"), release);
		FProsettaStoryProvenance provenance;
		TestTrue(TEXT("event provenance"), FProsettaStoryProvenance::FromSyncManifest(manifest, event, FString(), provenance));
		TestEqual(TEXT("channel"), provenance.Channel, FString(TEXT("latest-release")));
		TestEqual(TEXT("event scope"), provenance.Scope, FString(TEXT("volumes=core")));
		TestEqual(TEXT("event revision"), provenance.Revision, FString(TEXT("3")));
		TestEqual(TEXT("release tag"), provenance.ReleaseTagId, FString(TEXT("tag_01HZ")));
		TestEqual(TEXT("release published"), provenance.ReleasePublishedAt, FString(TEXT("2026-10-09T08:00:00.000Z")));
		TestEqual(TEXT("event log string"), provenance.ToLogString(), FString(TEXT("Prosetta project 7 latest-release rev@3 release tag_01HZ gen 00112233")));
	}

	// Compiled wrapper and empty cases.
	{
		FProsettaStoryProvenance provenance;
		TestTrue(TEXT("wrapper parses"), FProsettaStoryProvenance::FromCompiledWrapper(TEXT("{\"projectName\":\"Neo Cab\",\"compiledAt\":\"2026-04-24T16:39:12.000Z\",\"story\":{}}"), provenance));
		TestEqual(TEXT("wrapper project name"), provenance.ProjectName, FString(TEXT("Neo Cab")));
		TestEqual(TEXT("wrapper compiled at"), provenance.GeneratedAt, FString(TEXT("2026-04-24T16:39:12.000Z")));
		TestEqual(TEXT("wrapper log string"), provenance.ToLogString(), FString(TEXT("Prosetta project 'Neo Cab'")));
		TestFalse(TEXT("plain story is not a wrapper"), FProsettaStoryProvenance::FromCompiledWrapper(TEXT("{\"inkVersion\":21,\"root\":[]}"), provenance));
		TestFalse(TEXT("malformed rejected"), FProsettaStoryProvenance::FromSyncManifestJSON(TEXT("{"), FString(), provenance));
		TestFalse(TEXT("empty is unset"), FProsettaStoryProvenance().IsSet());
		TestTrue(TEXT("empty log string"), FProsettaStoryProvenance().ToLogString().IsEmpty());
		TestTrue(TEXT("library wrapper"), UInkpotProsettaLibrary::ProvenanceToLogString(FProsettaStoryProvenance()).IsEmpty());
		TestTrue(TEXT("default manifest path"), UInkpotProsettaLibrary::GetDefaultSyncManifestPath().EndsWith(TEXT("Prosetta/manifest.json")));
	}
	return true;
}
#endif
