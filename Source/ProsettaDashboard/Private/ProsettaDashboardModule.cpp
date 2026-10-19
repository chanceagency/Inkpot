#include "ProsettaDashboardModule.h"
#include "ProsettaDashboardSettings.h"
#include "ProsettaProfileStore.h"
#include "ProsettaProjectWatch.h"
#include "ProsettaSyncProfile.h"
#include "Dashboard/ProsettaDashboardTab.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaDashboardModule, Log, All);

void FProsettaDashboardModule::StartupModule()
{
	// What the editor inherited, restored whenever the active profile has no CLI override of its own.
	InheritedSyncBin = FPlatformMisc::GetEnvironmentVariable(TEXT("PROSETTA_SYNC_BIN"));
	// The wrapper may live in the plugin rather than <Project>/Scripts; tell it which project it serves.
	FPlatformMisc::SetEnvironmentVar(TEXT("PROSETTA_PROJECT_ROOT"), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	if (IsRunningCommandlet()) { return; }
	// Headless automation/import runs must never replay live transport receipts.
	if (!FApp::IsUnattended())
	{
		ProjectWatch = MakeUnique<FProsettaProjectWatch>();
		ProjectWatch->OptionsProvider = [this] { return WatchOptionsFromProfile(LoadActiveProfile()); };
	}
	if (FSlateApplication::IsInitialized()) { FProsettaDashboardTab::Register(); }
	PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FProsettaDashboardModule::HandlePostEngineInit);
}

void FProsettaDashboardModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	FProsettaDashboardTab::Unregister();
	ProjectWatch.Reset();
	ProfileStore.Reset();
	PresentationImporter = nullptr;
}

FProsettaProfileStore& FProsettaDashboardModule::GetProfileStore()
{
	if (!ProfileStore)
	{
		const UProsettaSyncProjectSettings* Settings = GetDefault<UProsettaSyncProjectSettings>();
		ProfileStore = MakeUnique<FProsettaProfileStore>(Settings ? Settings->ResolveProfilesDirectory() : FProsettaProfileStore::DefaultDirectory());
	}
	return *ProfileStore;
}

FProsettaSyncProfile FProsettaDashboardModule::LoadActiveProfile() const
{
	FProsettaDashboardModule* Mutable = const_cast<FProsettaDashboardModule*>(this);
	FProsettaProfileStore& Store = Mutable->GetProfileStore();
	const UProsettaSyncProjectSettings* Settings = GetDefault<UProsettaSyncProjectSettings>();
	const FString DefaultName = Settings && !Settings->DefaultProfileName.IsEmpty() ? Settings->DefaultProfileName : TEXT("Default");
	FString Name = DefaultName;
	if (const UProsettaDashboardUserSettings* User = GetDefault<UProsettaDashboardUserSettings>())
	{
		if (!User->ActiveProfileName.IsEmpty()) { Name = User->ActiveProfileName; }
	}
	FProsettaSyncProfile Profile;
	FString Error;
	if (Store.Load(Name, Profile, Error)) { return Profile; }
	if (Name != DefaultName)
	{
		UE_LOG(LogProsettaDashboardModule, Warning, TEXT("Active Prosetta profile '%s' could not be loaded (%s); using '%s'."), *Name, *Error, *DefaultName);
	}
	return Store.EnsureDefault(DefaultName);
}

FProsettaWatchOptions FProsettaDashboardModule::WatchOptionsFromProfile(const FProsettaSyncProfile& Profile) const
{
	FProsettaWatchOptions Options;
	Options.Channel = Profile.ChannelName();
	Options.Volumes = Profile.ScopeString();
	Options.bNoMirror = Profile.bNoMirror;
	Options.BaseUrl = Profile.BaseUrl;
	Options.ProjectId = Profile.ProjectId;
	Options.AudioDecoderPath = Profile.AudioDecoderPath.FilePath;
	return Options;
}

void FProsettaDashboardModule::ApplyProfileEnvironment(const FProsettaSyncProfile& Profile)
{
	// The wrapper honours PROSETTA_SYNC_BIN; the profile may pin a CLI build without touching config.
	// A relative override is project-relative (profiles are committed); no override keeps the inherited value.
	const FString Bin = Profile.CliBinaryOverride.FilePath.IsEmpty() ? InheritedSyncBin : FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), Profile.CliBinaryOverride.FilePath);
	FPlatformMisc::SetEnvironmentVar(TEXT("PROSETTA_SYNC_BIN"), *Bin);
}

void FProsettaDashboardModule::HandlePostEngineInit()
{
	if (FSlateApplication::IsInitialized())
	{
		FProsettaDashboardTab::Register();
		FProsettaDashboardTab::BindToLevelEditor();
	}
	const FProsettaSyncProfile Profile = LoadActiveProfile();
	ApplyProfileEnvironment(Profile);
	if (ProjectWatch && Profile.bAutoStartWatchOnEditorLaunch && Profile.Mode == EProsettaSyncMode::Watch)
	{
		TArray<FText> Problems;
		if (Profile.Validate(Problems))
		{
			ProjectWatch->Start(WatchOptionsFromProfile(Profile));
		}
		else
		{
			UE_LOG(LogProsettaDashboardModule, Warning, TEXT("Prosetta watch not auto-started: profile '%s' is invalid (%s)."), *Profile.Name, *Problems[0].ToString());
		}
	}
}

IMPLEMENT_MODULE(FProsettaDashboardModule, ProsettaDashboard)
