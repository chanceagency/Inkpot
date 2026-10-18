#include "ProsettaDashboardSubsystem.h"
#include "ProsettaDashboardModule.h"
#include "ProsettaCliRunner.h"
#include "ProsettaDashboardSettings.h"
#include "ProsettaProfileStore.h"
#include "ProsettaProjectWatch.h"
#include "ProsettaPython.h"
#include "ProsettaStoryAssetBuild.h"
#include "Inkpot/Prosetta/ProsettaEditorWatch.h"
#include "Inkpot/Prosetta/ProsettaStorySource.h"
#include "Inkpot/Prosetta/ProsettaStorySourceActor.h"

#include "DirectoryWatcherModule.h"
#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "Engine/Engine.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "Inkpot/Inkpot.h"
#include "Inkpot/Prosetta/ProsettaSyncedArtifact.h"
#include "InkpotEditorModule.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Prosetta/ProsettaWatchSubsystem.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ProsettaDashboard"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaDashboard, Log, All);

namespace
{
	constexpr float PollIntervalSeconds = 0.5f;
	constexpr double ManifestDebounceSeconds = 0.5;
	/** Manifest writes this soon after the dashboard's own CLI run (sync, volume switch) are its own, not external. */
	constexpr double OwnRunGraceSeconds = 3.0;
}

void UProsettaDashboardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IsRunningCommandlet()) { return; }
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UProsettaDashboardSubsystem::Tick), PollIntervalSeconds);
	GenerationHandle = UProsettaWatchSubsystem::OnGenerationAccepted.AddUObject(this, &UProsettaDashboardSubsystem::HandleGenerationAccepted);
	PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddUObject(this, &UProsettaDashboardSubsystem::HandlePreBeginPIE);
	EndPIEHandle = FEditorDelegates::EndPIE.AddUObject(this, &UProsettaDashboardSubsystem::HandleEndPIE);
	MapOpenedHandle = FEditorDelegates::OnMapOpened.AddUObject(this, &UProsettaDashboardSubsystem::HandleMapOpened);
}

void UProsettaDashboardSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	UProsettaWatchSubsystem::OnGenerationAccepted.Remove(GenerationHandle);
	FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
	FEditorDelegates::EndPIE.Remove(EndPIEHandle);
	FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
	if (DirectoryWatchHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("DirectoryWatcher"))
	{
		FDirectoryWatcherModule& Module = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
		if (IDirectoryWatcher* Watcher = Module.Get()) { Watcher->UnregisterDirectoryChangedCallback_Handle(WatchedDirectory, DirectoryWatchHandle); }
	}
	Model.Reset();
	Super::Deinitialize();
}

TSharedRef<FProsettaDashboardModel> UProsettaDashboardSubsystem::GetModel()
{
	if (!Model)
	{
		Model = MakeShared<FProsettaDashboardModel>(BuildDeps());
		Model->SeedChangesFromReceipts(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ProsettaSync"), TEXT("Watch")));
		Model->Poll(BuildPollInput());
		EnsureManifestWatcher();
	}
	return Model.ToSharedRef();
}

FProsettaDashboardModelDeps UProsettaDashboardSubsystem::BuildDeps()
{
	FProsettaDashboardModelDeps Deps;
	FString Python, Error;
	const FString Script = FindProsettaScript(TEXT("prosetta.py"));
	if (Script.IsEmpty())
	{
		Error = FString::Printf(TEXT("The prosetta.py wrapper was not found (looked in %s). Restore Plugins/Inkpot/Scripts or copy prosetta.py to the project's Scripts folder."),
			*FString::Join(ProsettaScriptCandidates(TEXT("prosetta.py")), TEXT(", ")));
	}
	else if (FindProsettaPython(Python, Error))
	{
		Deps.Runner = MakeShared<FProsettaCliRunner>(Python, Script, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	}
	if (!Deps.Runner)
	{
		Deps.NoRunnerReason = Error;
		UE_LOG(LogProsettaDashboard, Warning, TEXT("Prosetta dashboard has no CLI: %s"), *Error);
	}
	FProsettaDashboardModule& Module = FProsettaDashboardModule::Get();
	Deps.Store = &Module.GetProfileStore();
	Deps.UserSettings = UProsettaDashboardUserSettings::Get();
	if (const UProsettaSyncProjectSettings* Settings = GetDefault<UProsettaSyncProjectSettings>())
	{
		if (!Settings->DefaultProfileName.IsEmpty()) { Deps.DefaultProfileName = Settings->DefaultProfileName; }
	}
	Deps.MirrorRoot = ProsettaStorySource::MirrorRoot();
	Deps.SafeToApply = [] { return UProsettaWatchSubsystem::IsSafeToApply(); };
	Deps.IsPIE = [] { return GEditor && GEditor->PlayWorld != nullptr; };
	Deps.StartWatch = [](const FProsettaSyncProfile& Profile)
	{
		if (!FProsettaDashboardModule::IsAvailable()) { return; }
		FProsettaDashboardModule& M = FProsettaDashboardModule::Get();
		if (FProsettaProjectWatch* Watch = M.GetProjectWatch()) { Watch->Start(M.WatchOptionsFromProfile(Profile)); }
	};
	Deps.StopWatch = [] { if (IProsettaEditorWatch::Installed) { IProsettaEditorWatch::Installed->Stop(); } };
	Deps.IsWatchRunning = [] { return IProsettaEditorWatch::Installed && IProsettaEditorWatch::Installed->IsRunning(); };
	Deps.ApplyProfileEnvironment = [](const FProsettaSyncProfile& Profile) { if (FProsettaDashboardModule::IsAvailable()) { FProsettaDashboardModule::Get().ApplyProfileEnvironment(Profile); } };
	Deps.RePoint = [this](const FString& Slug, const FString& Tag, FString& OutError) { return RePointLevel(Slug, Tag, OutError); };
	Deps.Reload = [this](const FProsettaSyncProfile& Profile, const FString& Slug, bool bWithPresentation, FString& OutError) { return RebuildStory(Profile, Slug, bWithPresentation, OutError); };
	Deps.ApplyPendingImport = []
	{
		UProsettaWatchSubsystem* Watch = GEditor ? GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>() : nullptr;
		return Watch && Watch->TryApplyPending();
	};
	Deps.Toast = [this](const FText& Message, EProsettaToastKind Kind) { ShowToast(Message, Kind); };
	Deps.OpenBlotter = [this] { ToggleEditorUtilityWidget(FSoftObjectPath(TEXT("/Inkpot/Inkpot/Editor/InkpotDebug.InkpotDebug")), BlotterTabId); };
	Deps.OpenOutliner = [this] { ToggleEditorUtilityWidget(FSoftObjectPath(TEXT("/Inkpot/Inkpot/Editor/InkpotOutliner.InkpotOutliner")), OutlinerTabId); };
	return Deps;
}

FProsettaDashboardPollInput UProsettaDashboardSubsystem::BuildPollInput() const
{
	FProsettaDashboardPollInput Input;
	Input.Now = FDateTime::UtcNow();
	if (IProsettaEditorWatch::Installed)
	{
		Input.TransportStatus = IProsettaEditorWatch::Installed->Status();
		Input.bWatchRunning = IProsettaEditorWatch::Installed->IsRunning();
	}
	if (UProsettaWatchSubsystem* Watch = GEditor ? GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>() : nullptr)
	{
		Input.WatchStatus = Watch->GetStatus();
		Input.PendingCount = Watch->GetPendingCount();
		Input.bCheckingVersion = Watch->IsCheckingVersion();
		Input.bExternalWatch = Watch->HasExternalWatch();
	}
	Input.bSafeToApply = UProsettaWatchSubsystem::IsSafeToApply();
	Input.bPIE = GEditor && GEditor->PlayWorld != nullptr;
	if (UInkpot* Inkpot = GEngine ? GEngine->GetEngineSubsystem<UInkpot>() : nullptr) { Input.RunningStories = Inkpot->GetStories().Num(); }
	Input.ManifestMTime = IFileManager::Get().GetTimeStamp(*ProsettaStorySource::ManifestPath(ProsettaStorySource::MirrorRoot()));
	return Input;
}

bool UProsettaDashboardSubsystem::Tick(float)
{
	if (!Model || bSuspendPolling) { return true; }
	if (Model->GetState().IsBusy()) { LastOwnRunSeconds = FPlatformTime::Seconds(); }
	if (bManifestChangeQueued && LastManifestChangeSeconds - LastOwnRunSeconds < OwnRunGraceSeconds)
	{
		// The dashboard's own sync rewrote the manifest; it already refreshed and queued the rebuild.
		bManifestChangeQueued = false;
	}
	if (bManifestChangeQueued && FPlatformTime::Seconds() - LastManifestChangeSeconds >= ManifestDebounceSeconds)
	{
		bManifestChangeQueued = false;
		Model->NoteExternalChange(TEXT("Saved/Prosetta/manifest.json changed outside the editor (terminal sync or watch). Apply to rebuild the story asset."));
		FNotificationInfo Info(LOCTEXT("ExternalChange", "Prosetta mirror changed outside the editor."));
		Info.SubText = LOCTEXT("ExternalChangeSub", "Apply rebuilds the story asset from the mirror once Play is stopped.");
		Info.ExpireDuration = 8.0f;
		Info.bFireAndForget = false;
		TSharedPtr<SNotificationItem> Item;
		TWeakPtr<FProsettaDashboardModel> Weak = Model;
		auto Finish = [](TSharedPtr<SNotificationItem> I) { if (I) { I->SetCompletionState(SNotificationItem::CS_None); I->ExpireAndFadeout(); } };
		TSharedPtr<TSharedPtr<SNotificationItem>> ItemBox = MakeShared<TSharedPtr<SNotificationItem>>();
		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("Apply", "Apply"), LOCTEXT("ApplyTip", "Rebuild the story asset from the changed mirror."), FSimpleDelegate::CreateLambda([Weak, ItemBox, Finish]
		{
			if (TSharedPtr<FProsettaDashboardModel> M = Weak.Pin()) { M->ApplyPending(); }
			Finish(*ItemBox);
		})));
		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("Dismiss", "Dismiss"), LOCTEXT("DismissTip", "Keep the current assets."), FSimpleDelegate::CreateLambda([Weak, ItemBox, Finish]
		{
			if (TSharedPtr<FProsettaDashboardModel> M = Weak.Pin()) { M->DismissExternalChange(); }
			Finish(*ItemBox);
		})));
		*ItemBox = FSlateNotificationManager::Get().AddNotification(Info);
		if (*ItemBox) { (*ItemBox)->SetCompletionState(SNotificationItem::CS_Pending); }
	}
	if (!DirectoryWatchHandle.IsValid()) { EnsureManifestWatcher(); }
	Model->Poll(BuildPollInput());
	return true;
}

void UProsettaDashboardSubsystem::EnsureManifestWatcher()
{
	if (DirectoryWatchHandle.IsValid() || IsRunningCommandlet()) { return; }
	const FString Directory = ProsettaStorySource::MirrorRoot();
	if (!IFileManager::Get().DirectoryExists(*Directory)) { return; }
	FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
	IDirectoryWatcher* Watcher = Module.Get();
	if (!Watcher) { return; }
	WatchedDirectory = Directory;
	// Detection only: the callback records the change; the user applies it through the model.
	Watcher->RegisterDirectoryChangedCallback_Handle(Directory, IDirectoryWatcher::FDirectoryChanged::CreateUObject(this, &UProsettaDashboardSubsystem::HandleDirectoryChanged), DirectoryWatchHandle, 0);
}

void UProsettaDashboardSubsystem::HandleDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	for (const FFileChangeData& Change : Changes)
	{
		if (FPaths::GetCleanFilename(Change.Filename) == TEXT("manifest.json"))
		{
			LastManifestChangeSeconds = FPlatformTime::Seconds();
			bManifestChangeQueued = true;
			return;
		}
	}
}

void UProsettaDashboardSubsystem::HandleGenerationAccepted(const UProsettaGenerationAsset& Generation)
{
	if (!Model) { return; }
	// A newly accepted generation is already in the mirror; our own import of it is not an "external" change.
	bManifestChangeQueued = false;
	Model->NoteGenerationAccepted(Generation.GenerationId, Model->GetState().Profile.bImportOnAccept);
}

void UProsettaDashboardSubsystem::HandlePreBeginPIE(bool) { if (Model) { Model->Poll(BuildPollInput()); } }
void UProsettaDashboardSubsystem::HandleEndPIE(bool) { if (Model) { Model->Poll(BuildPollInput()); } }
void UProsettaDashboardSubsystem::HandleMapOpened(const FString&, bool) { if (Model) { Model->Poll(BuildPollInput()); } }

bool UProsettaDashboardSubsystem::RePointLevel(const FString& Slug, const FString& Tag, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("no editor world is open"); return false; }
	return RePointWorld(World, Slug, Tag, OutError);
}

UClass* UProsettaDashboardSubsystem::StorySourceHostClass()
{
	const UProsettaSyncProjectSettings* Settings = GetDefault<UProsettaSyncProjectSettings>();
	UClass* Class = Settings ? Settings->StorySourceHostClass.LoadSynchronous() : nullptr;
	return Class && Class->ImplementsInterface(UProsettaStorySourceHost::StaticClass()) ? Class : AProsettaStorySourceActor::StaticClass();
}

bool UProsettaDashboardSubsystem::RePointWorld(UWorld* World, const FString& Slug, const FString& Tag, FString& OutError)
{
	if (!World) { OutError = TEXT("no editor world is open"); return false; }
	const FScopedTransaction Transaction(LOCTEXT("RePointLevel", "Re-point level at Prosetta volume"));
	AActor* HostActor = ProsettaStorySource::FindHost(World);
	if (!HostActor)
	{
		UClass* Class = StorySourceHostClass();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HostActor = World->SpawnActor(Class, &FTransform::Identity, Params);
		if (!HostActor) { OutError = FString::Printf(TEXT("could not spawn the story source host %s"), *Class->GetName()); return false; }
	}
	IProsettaStorySourceHost* Host = Cast<IProsettaStorySourceHost>(HostActor);
	FProsettaStorySource Source = Host->GetProsettaStorySource();
	Source.VolumeSlug = Slug;
	Source.VolumeTag = Tag == TEXT("latest") ? FString() : Tag;
	Host->SetProsettaStorySource(Source);
	return true;
}

bool UProsettaDashboardSubsystem::RebuildStory(const FProsettaSyncProfile& Profile, const FString& Slug, bool bWithPresentation, FString& OutError)
{
	FProsettaStoryAssetBuildOptions Options;
	Options.MirrorRoot = ProsettaStorySource::MirrorRoot();
	Options.VolumeSlug = Slug;
	Options.AssetPath = Slug.IsEmpty() ? ProsettaDefaultStoryAssetPath() : ProsettaVolumeAssetPath(Slug);
	Options.StageDir = ProsettaDefaultImportStageDir();
	Options.AudioDecoder = Profile.AudioDecoderPath.FilePath;
	Options.bImportPresentation = bWithPresentation && Profile.bImportOnAccept;
	if (FProsettaDashboardModule::IsAvailable()) { Options.ImportPresentation = FProsettaDashboardModule::Get().GetPresentationImporter(); }
	const FProsettaStoryAssetBuildResult Result = RebuildProsettaStoryAsset(Options);
	if (!Result.bSucceeded) { OutError = Result.Error; }
	else { UE_LOG(LogProsettaDashboard, Log, TEXT("Prosetta story asset rebuilt at %s (%s)."), *Result.ObjectPath, Result.bReimported ? TEXT("reimported") : TEXT("created")); }
	return Result.bSucceeded;
}

void UProsettaDashboardSubsystem::ToggleEditorUtilityWidget(const FSoftObjectPath& WidgetPath, FName& InOutTabId)
{
	UEditorUtilitySubsystem* Utility = GEditor ? GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>() : nullptr;
	if (!Utility) { return; }
	if (!InOutTabId.IsNone() && Utility->DoesTabExist(InOutTabId))
	{
		Utility->CloseTabByID(InOutTabId);
		InOutTabId = NAME_None;
		return;
	}
	UEditorUtilityWidgetBlueprint* Blueprint = Cast<UEditorUtilityWidgetBlueprint>(WidgetPath.TryLoad());
	if (!Blueprint)
	{
		UE_LOG(LogProsettaDashboard, Warning, TEXT("Inkpot utility widget %s not found."), *WidgetPath.ToString());
		return;
	}
	FName TabId;
	if (Utility->SpawnAndRegisterTabAndGetID(Blueprint, TabId)) { InOutTabId = TabId; }
}

void UProsettaDashboardSubsystem::ShowToast(const FText& Message, EProsettaToastKind Kind)
{
	if (!FSlateApplication::IsInitialized()) { return; }
	FNotificationInfo Info(Message);
	Info.ExpireDuration = Kind == EProsettaToastKind::Fail ? 8.0f : 4.0f;
	Info.bUseSuccessFailIcons = Kind != EProsettaToastKind::Info;
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(Kind == EProsettaToastKind::Success ? SNotificationItem::CS_Success : Kind == EProsettaToastKind::Fail ? SNotificationItem::CS_Fail : SNotificationItem::CS_None);
	}
}

#undef LOCTEXT_NAMESPACE
