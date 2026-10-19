#include "ProsettaDashboardModel.h"
#include "ProsettaCliRunner.h"
#include "ProsettaProfileStore.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "ProsettaDashboard"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaDashboardModel, Log, All);

namespace
{
	/** A string, number or bool field as text; empty when missing or null. */
	FString FieldAsString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		if (!Object.IsValid()) { return FString(); }
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
		if (!Value.IsValid() || Value->IsNull()) { return FString(); }
		FString Text;
		if (Value->TryGetString(Text)) { return Text; }
		double Number;
		if (Value->TryGetNumber(Number)) { return FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)) ? FString::Printf(TEXT("%.0f"), Number) : FString::SanitizeFloat(Number); }
		bool Flag;
		if (Value->TryGetBool(Flag)) { return Flag ? TEXT("true") : TEXT("false"); }
		return FString();
	}

	const TSharedPtr<FJsonObject>* ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Child = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Key, Child) ? Child : nullptr;
	}

	bool ContainsIgnoreCase(const FString& Haystack, const TCHAR* Needle)
	{
		return Haystack.Contains(Needle, ESearchCase::IgnoreCase);
	}

	/** A string or an array of strings ("a,b@1") as one comma-separated scope; empty when missing or null. */
	FString ScopeField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(Key, Items)) { return FieldAsString(Object, Key); }
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& Item : *Items)
		{
			FString Part;
			if (Item.IsValid() && Item->TryGetString(Part) && !Part.IsEmpty()) { Parts.Add(Part); }
		}
		return FString::Join(Parts, TEXT(","));
	}

	/** True when the wrapper or Prosetta judged the token itself (rejected, expired, other project, missing). */
	bool IsCredentialRejection(const FProsettaCliResult& Result)
	{
		if (Result.bCredentialRejection) { return true; }
		FString Text = Result.UserError;
		for (const FString& Notice : Result.Notices) { Text += TEXT("\n") + Notice; }
		for (const TCHAR* Mark : { TEXT("INVALID_OR_EXPIRED_TOKEN"), TEXT("rejected the token"), TEXT("different project"), TEXT("HTTP 401"), TEXT("HTTP 403"), TEXT("No usable token") })
		{
			if (Text.Contains(Mark, ESearchCase::IgnoreCase)) { return true; }
		}
		return false;
	}

	FString KindName(EProsettaChangeKind Kind)
	{
		switch (Kind)
		{
		case EProsettaChangeKind::Generation: return TEXT("generation");
		case EProsettaChangeKind::Sync: return TEXT("sync");
		case EProsettaChangeKind::Verify: return TEXT("verify");
		case EProsettaChangeKind::Error: return TEXT("error");
		case EProsettaChangeKind::Blocked: return TEXT("blocked");
		case EProsettaChangeKind::ExternalChange: return TEXT("mirror");
		default: return TEXT("info");
		}
	}
}

FString FProsettaChangeEntry::Describe() const
{
	FString Text = FString::Printf(TEXT("[%s] %s"), *KindName(Kind), *Time.ToString(TEXT("%H:%M:%S")));
	if (!GenerationId.IsEmpty()) { Text += TEXT(" ") + ShortGenerationId(); }
	if (!Scope.IsEmpty()) { Text += TEXT(" ") + Scope; }
	if (!Channel.IsEmpty()) { Text += TEXT("/") + Channel; }
	if (!ReleaseTag.IsEmpty()) { Text += TEXT(" @") + ReleaseTag; }
	if (!ChangedKeys.IsEmpty()) { Text += FString::Printf(TEXT(" changed %s"), *FString::Join(ChangedKeys, TEXT(","))); }
	if (!RemovedKeys.IsEmpty()) { Text += FString::Printf(TEXT(" removed %s"), *FString::Join(RemovedKeys, TEXT(","))); }
	if (!Message.IsEmpty()) { Text += TEXT(": ") + Message; }
	return Text;
}

FString FProsettaCredentialsSummary::Describe() const
{
	if (!HasCredentials()) { return TEXT("No Prosetta credentials found."); }
	FString Text = Owner.IsEmpty() ? TEXT("Connected") : FString::Printf(TEXT("Connected as %s"), *Owner);
	if (!ProjectName.IsEmpty()) { Text += FString::Printf(TEXT(" to %s"), *ProjectName); }
	else if (!ProjectId.IsEmpty()) { Text += FString::Printf(TEXT(" to project %s"), *ProjectId); }
	if (!BaseUrl.IsEmpty()) { Text += FString::Printf(TEXT(" (%s)"), *BaseUrl); }
	Text += FString::Printf(TEXT(" via %s"), *Source);
	for (const FString& Warning : Warnings) { Text += TEXT(" Warning: ") + Warning; }
	return Text;
}

bool FProsettaDashboardState::IsConnected() const
{
	return Connection == EProsettaDashboardConnection::Connected || Connection == EProsettaDashboardConnection::Degraded || Connection == EProsettaDashboardConnection::Blocked;
}

FProsettaDashboardModel::FProsettaDashboardModel(FProsettaDashboardModelDeps InDeps)
	: Deps(MoveTemp(InDeps))
{
	if (Deps.MirrorRoot.IsEmpty()) { Deps.MirrorRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Prosetta")); }
	State.CliScript = Deps.Runner ? Deps.Runner->GetScript() : FString();
	State.CliSetupProblem = Deps.Runner ? FString() : Deps.NoRunnerReason;
	State.Connection = Deps.Runner ? EProsettaDashboardConnection::Disconnected : EProsettaDashboardConnection::NoCli;
	if (Deps.UserSettings) { State.RecentConnections = Deps.UserSettings->RecentConnections; }
	ReloadProfiles();
	RefreshManifest();
	Recompute();
}

FProsettaDashboardModel::~FProsettaDashboardModel()
{
	if (Deps.Runner) { Deps.Runner->CancelAll(); }
}

// ---- state plumbing -------------------------------------------------------------------------

void FProsettaDashboardModel::Broadcast()
{
	StateChanged.Broadcast();
}

void FProsettaDashboardModel::SetBusy(const FString& What)
{
	++InFlight;
	State.Busy = What;
	Recompute();
	Broadcast();
}

void FProsettaDashboardModel::ClearBusy()
{
	InFlight = FMath::Max(0, InFlight - 1);
	if (InFlight == 0) { State.Busy.Empty(); }
	Recompute();
	Broadcast();
}

void FProsettaDashboardModel::Fail(const FString& Error, EProsettaChangeKind Kind)
{
	State.LastError = Error;
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Kind = Kind;
	Entry.Message = Error;
	AddChange(MoveTemp(Entry));
	UE_LOG(LogProsettaDashboardModel, Warning, TEXT("%s"), *Error);
	if (Deps.Toast) { Deps.Toast(FText::FromString(Error), EProsettaToastKind::Fail); }
}

void FProsettaDashboardModel::Recompute()
{
	if (!Deps.Runner)
	{
		State.Connection = EProsettaDashboardConnection::NoCli;
	}
	else if (bConnecting)
	{
		State.Connection = EProsettaDashboardConnection::Connecting;
	}
	else if (bCredentialsKnown && !State.Credentials.bCliAvailable)
	{
		State.Connection = EProsettaDashboardConnection::NoCli;
	}
	else if (bCredentialsKnown && !State.Credentials.HasCredentials())
	{
		State.Connection = EProsettaDashboardConnection::NoCredentials;
	}
	else if (bWhoamiError)
	{
		// whoami could not run or reach Prosetta (offline, timeout, wrapper error): not a verdict on the token.
		State.Connection = EProsettaDashboardConnection::Error;
	}
	else
	{
		State.Connection = ClassifyConnection(State.WatchStatus, State.TransportStatus, bWhoamiOk, bWhoamiKnown);
	}

	switch (State.Connection)
	{
	case EProsettaDashboardConnection::NoCli: State.StatusText = !State.CliSetupProblem.IsEmpty() ? State.CliSetupProblem : TEXT("Prosetta CLI not available: install prosetta-sync or set PROSETTA_SYNC_BIN."); break;
	case EProsettaDashboardConnection::NoCredentials: State.StatusText = TEXT("No credentials: run init from the dashboard or prosetta.py init --token-stdin in a terminal."); break;
	case EProsettaDashboardConnection::Connecting: State.StatusText = State.Busy.IsEmpty() ? TEXT("Connecting…") : State.Busy + TEXT("…"); break;
	case EProsettaDashboardConnection::AuthFailed: State.StatusText = TEXT("Prosetta rejected the token; update credentials."); break;
	case EProsettaDashboardConnection::Blocked: State.StatusText = TEXT("Watch blocked by Prosetta; last assets retained."); break;
	case EProsettaDashboardConnection::Degraded: State.StatusText = TEXT("Connected; the watch is retrying."); break;
	case EProsettaDashboardConnection::Error: State.StatusText = bWhoamiError || State.WatchStatus.IsEmpty() ? State.LastError : State.WatchStatus; break;
	case EProsettaDashboardConnection::Connected: State.StatusText = State.Credentials.Describe(); break;
	default: State.StatusText = State.Credentials.HasCredentials() ? TEXT("Not connected; press Connect.") : TEXT("Not connected."); break;
	}
	if (State.bPIE && State.PendingCount > 0)
	{
		State.StatusText += FString::Printf(TEXT(" %d generation(s) queued until Play stops."), State.PendingCount);
	}
	State.ProfileProblems.Reset();
	State.Profile.Validate(State.ProfileProblems);
	State.bProfileDirty = Deps.Store ? IsProfileDirty() : false;
}

bool FProsettaDashboardModel::IsProfileDirty()
{
	// Recompute runs on every poll: re-read the profile file only when the working copy or the file changed.
	const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Deps.Store->PathFor(State.Profile.Name));
	if (!bDirtyCacheValid || Stamp != DirtyCacheStamp || DirtyCacheProfile != State.Profile)
	{
		bDirtyCacheValue = Deps.Store->IsDirty(State.Profile);
		DirtyCacheStamp = Stamp;
		DirtyCacheProfile = State.Profile;
		bDirtyCacheValid = true;
	}
	return bDirtyCacheValue;
}

EProsettaDashboardConnection FProsettaDashboardModel::ClassifyConnection(const FString& WatchStatus, const FString& TransportStatus, bool bInWhoamiOk, bool bInWhoamiKnown)
{
	if (bInWhoamiKnown && !bInWhoamiOk) { return EProsettaDashboardConnection::AuthFailed; }
	if (ContainsIgnoreCase(WatchStatus, TEXT("blocked"))) { return EProsettaDashboardConnection::Blocked; }
	static const TCHAR* ErrorMarks[] = { TEXT("CLI error"), TEXT("Watch error"), TEXT("Import failed"), TEXT("CLI exited"), TEXT("CLI rejected"), TEXT("Unable to launch") };
	for (const TCHAR* Mark : ErrorMarks)
	{
		if (WatchStatus.StartsWith(Mark)) { return EProsettaDashboardConnection::Error; }
	}
	if (ContainsIgnoreCase(TransportStatus, TEXT("exited (")) || ContainsIgnoreCase(TransportStatus, TEXT("could not be launched")) || ContainsIgnoreCase(TransportStatus, TEXT("not started:")))
	{
		return EProsettaDashboardConnection::Error;
	}
	if (ContainsIgnoreCase(WatchStatus, TEXT("reconnecting")) || ContainsIgnoreCase(WatchStatus, TEXT("will retry")))
	{
		return EProsettaDashboardConnection::Degraded;
	}
	return bInWhoamiKnown && bInWhoamiOk ? EProsettaDashboardConnection::Connected : EProsettaDashboardConnection::Disconnected;
}

FText FProsettaDashboardModel::DescribeConnection(EProsettaDashboardConnection Connection)
{
	switch (Connection)
	{
	case EProsettaDashboardConnection::NoCli: return LOCTEXT("NoCli", "No CLI");
	case EProsettaDashboardConnection::NoCredentials: return LOCTEXT("NoCredentials", "No credentials");
	case EProsettaDashboardConnection::Disconnected: return LOCTEXT("Disconnected", "Disconnected");
	case EProsettaDashboardConnection::Connecting: return LOCTEXT("Connecting", "Connecting");
	case EProsettaDashboardConnection::Connected: return LOCTEXT("Connected", "Connected");
	case EProsettaDashboardConnection::Degraded: return LOCTEXT("Degraded", "Degraded");
	case EProsettaDashboardConnection::Blocked: return LOCTEXT("Blocked", "Blocked");
	case EProsettaDashboardConnection::AuthFailed: return LOCTEXT("AuthFailed", "Authentication failed");
	default: return LOCTEXT("Error", "Error");
	}
}

// ---- user settings ----------------------------------------------------------------------------

FString FProsettaDashboardModel::ActiveProfileName() const
{
	const FString Name = Deps.UserSettings ? Deps.UserSettings->ActiveProfileName : InMemoryActiveProfile;
	return Name.IsEmpty() ? Deps.DefaultProfileName : Name;
}

void FProsettaDashboardModel::SetActiveProfileName(const FString& Name)
{
	if (Deps.UserSettings) { Deps.UserSettings->ActiveProfileName = Name; }
	else { InMemoryActiveProfile = Name; }
	PersistUserSettings();
}

void FProsettaDashboardModel::PersistUserSettings()
{
	if (Deps.UserSettings) { Deps.UserSettings->SaveConfig(); }
}

void FProsettaDashboardModel::PushRecentConnection()
{
	const FString BaseUrl = State.Credentials.BaseUrl;
	const FString ProjectId = State.Credentials.ProjectId;
	if (BaseUrl.IsEmpty()) { return; }
	if (Deps.UserSettings)
	{
		Deps.UserSettings->PushRecentConnection(BaseUrl, ProjectId, State.Credentials.ProjectName);
		State.RecentConnections = Deps.UserSettings->RecentConnections;
		PersistUserSettings();
	}
	else
	{
		InMemoryRecent.RemoveAll([&](const FProsettaRecentConnection& C) { return C.SameTarget(BaseUrl, ProjectId); });
		FProsettaRecentConnection Entry;
		Entry.BaseUrl = BaseUrl;
		Entry.ProjectId = ProjectId;
		Entry.ProjectName = State.Credentials.ProjectName;
		Entry.LastUsed = FDateTime::UtcNow();
		InMemoryRecent.Insert(Entry, 0);
		if (InMemoryRecent.Num() > UProsettaDashboardUserSettings::MaxRecentConnections) { InMemoryRecent.SetNum(UProsettaDashboardUserSettings::MaxRecentConnections); }
		State.RecentConnections = InMemoryRecent;
	}
}

// ---- profiles --------------------------------------------------------------------------------

void FProsettaDashboardModel::ReloadProfiles()
{
	if (!Deps.Store)
	{
		State.ProfileNames = { State.Profile.Name };
		Recompute();
		return;
	}
	State.ProfileNames = Deps.Store->List();
	FString Error;
	FProsettaSyncProfile Loaded;
	if (Deps.Store->Load(ActiveProfileName(), Loaded, Error))
	{
		State.Profile = Loaded;
	}
	else
	{
		State.Profile = Deps.Store->EnsureDefault(Deps.DefaultProfileName);
		State.ProfileNames = Deps.Store->List();
	}
	if (Deps.ApplyProfileEnvironment) { Deps.ApplyProfileEnvironment(State.Profile); }
	Recompute();
}

void FProsettaDashboardModel::SetProfile(const FProsettaSyncProfile& Edited)
{
	State.Profile = Edited;
	Recompute();
	Broadcast();
}

bool FProsettaDashboardModel::LoadProfile(const FString& Name, FString& OutError)
{
	if (!Deps.Store) { OutError = TEXT("No profile store."); return false; }
	FProsettaSyncProfile Loaded;
	if (!Deps.Store->Load(Name, Loaded, OutError)) { return false; }
	const bool bScopeChanged = Loaded.ScopeString() != State.Profile.ScopeString();
	State.Profile = Loaded;
	SetActiveProfileName(Name);
	if (Deps.ApplyProfileEnvironment) { Deps.ApplyProfileEnvironment(State.Profile); }
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Message = bScopeChanged
		? FString::Printf(TEXT("Profile '%s' active; sync scope now '%s' (the next sync re-pulls it)."), *Name, Loaded.ScopeString().IsEmpty() ? TEXT("project") : *Loaded.ScopeString())
		: FString::Printf(TEXT("Profile '%s' active."), *Name);
	AddChange(MoveTemp(Entry));
	Recompute();
	Broadcast();
	if (Deps.Runner && bScopeChanged)
	{
		TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
		SetBusy(TEXT("Applying sync scope"));
		Deps.Runner->ConfigSetVolumes(Loaded.ScopeString(), FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
		{
			TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
			if (!Self) { return; }
			if (!Result.Succeeded()) { Self->Fail(TEXT("Could not write the sync scope to local config: ") + Result.UserError); }
			Self->ClearBusy();
			Self->Connect();
		}));
	}
	else
	{
		Connect();
	}
	return true;
}

bool FProsettaDashboardModel::SaveProfile(FString& OutError)
{
	if (!Deps.Store) { OutError = TEXT("No profile store."); return false; }
	if (!Deps.Store->Save(State.Profile, OutError)) { Fail(OutError); return false; }
	bDirtyCacheValid = false;
	State.ProfileNames = Deps.Store->List();
	SetActiveProfileName(State.Profile.Name);
	if (Deps.ApplyProfileEnvironment) { Deps.ApplyProfileEnvironment(State.Profile); }
	if (Deps.Toast) { Deps.Toast(FText::Format(LOCTEXT("ProfileSaved", "Profile '{0}' saved."), FText::FromString(State.Profile.Name)), EProsettaToastKind::Success); }
	Recompute();
	Broadcast();
	return true;
}

bool FProsettaDashboardModel::SaveProfileAs(const FString& NewName, FString& OutError)
{
	if (!Deps.Store) { OutError = TEXT("No profile store."); return false; }
	if (!Deps.Store->SaveAs(State.Profile, NewName, OutError)) { Fail(OutError); return false; }
	bDirtyCacheValid = false;
	State.Profile.Name = NewName;
	State.ProfileNames = Deps.Store->List();
	SetActiveProfileName(NewName);
	if (Deps.ApplyProfileEnvironment) { Deps.ApplyProfileEnvironment(State.Profile); }
	Recompute();
	Broadcast();
	return true;
}

void FProsettaDashboardModel::RevertProfile()
{
	if (Deps.Store)
	{
		FString Error;
		FProsettaSyncProfile Loaded;
		if (Deps.Store->Load(State.Profile.Name, Loaded, Error)) { State.Profile = Loaded; }
	}
	Recompute();
	Broadcast();
}

bool FProsettaDashboardModel::DeleteProfile(const FString& Name, FString& OutError)
{
	if (!Deps.Store) { OutError = TEXT("No profile store."); return false; }
	if (!Deps.Store->Delete(Name, OutError)) { Fail(OutError); return false; }
	bDirtyCacheValid = false;
	if (Name == State.Profile.Name)
	{
		SetActiveProfileName(Deps.DefaultProfileName);
		State.Profile = Deps.Store->EnsureDefault(Deps.DefaultProfileName);
		if (Deps.ApplyProfileEnvironment) { Deps.ApplyProfileEnvironment(State.Profile); }
	}
	State.ProfileNames = Deps.Store->List();
	Recompute();
	Broadcast();
	return true;
}

void FProsettaDashboardModel::RunProfile()
{
	switch (State.Profile.Mode)
	{
	case EProsettaSyncMode::Watch: StartWatch(); break;
	case EProsettaSyncMode::SyncOnce: SyncOnce(); break;
	case EProsettaSyncMode::Verify: Verify(); break;
	}
}

// ---- credentials / connect ----------------------------------------------------------------------

bool FProsettaDashboardModel::ParseCredentialsReport(const TSharedPtr<FJsonObject>& Report, FProsettaCredentialsSummary& Out)
{
	if (!Report.IsValid()) { return false; }
	Out = FProsettaCredentialsSummary();
	Out.Source = FieldAsString(Report, TEXT("source"));
	if (Out.Source.IsEmpty()) { Out.Source = TEXT("none"); }
	Out.BaseUrl = FieldAsString(Report, TEXT("baseUrl"));
	Out.ProjectId = FieldAsString(Report, TEXT("projectId"));
	Out.ProjectName = FieldAsString(Report, TEXT("projectName"));
	if (Out.ProjectName.IsEmpty()) { Out.ProjectName = FieldAsString(Report, TEXT("projectIdentifier")); }
	Out.LocalConfigPath = FieldAsString(Report, TEXT("localConfigPath"));
	Out.bLocalConfigExists = FieldAsString(Report, TEXT("localConfigExists")) == TEXT("true");
	Out.bSessionOverride = FieldAsString(Report, TEXT("sessionOverride")) == TEXT("true");
	Out.ConfigVolumes = ScopeField(Report, TEXT("volumes"));
	if (const TSharedPtr<FJsonObject>* Cli = ObjectField(Report, TEXT("cli")))
	{
		Out.bCliAvailable = FieldAsString(*Cli, TEXT("available")) == TEXT("true");
		Out.CliCommand = FieldAsString(*Cli, TEXT("command"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
	if (Report->TryGetArrayField(TEXT("candidates"), Candidates))
	{
		for (const TSharedPtr<FJsonValue>& Candidate : *Candidates)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Candidate.IsValid() || !Candidate->TryGetObject(Entry)) { continue; }
			const FString Warning = FieldAsString(*Entry, TEXT("warning"));
			if (!Warning.IsEmpty()) { Out.Warnings.AddUnique(Warning); }
		}
	}
	return true;
}

void FProsettaDashboardModel::ParseWhoami(const TSharedPtr<FJsonObject>& Whoami, FString& OutOwner, FString& OutProjectName)
{
	OutOwner.Empty();
	OutProjectName.Empty();
	if (!Whoami.IsValid()) { return; }
	auto PickOwner = [&OutOwner](const TSharedPtr<FJsonObject>& Object)
	{
		for (const TCHAR* Key : { TEXT("name"), TEXT("displayName"), TEXT("username"), TEXT("email"), TEXT("login"), TEXT("id") })
		{
			if (OutOwner.IsEmpty()) { OutOwner = FieldAsString(Object, Key); }
		}
	};
	for (const TCHAR* Key : { TEXT("user"), TEXT("owner"), TEXT("account"), TEXT("member") })
	{
		if (const TSharedPtr<FJsonObject>* User = ObjectField(Whoami, Key)) { PickOwner(*User); }
	}
	if (OutOwner.IsEmpty())
	{
		for (const TCHAR* Key : { TEXT("userName"), TEXT("username"), TEXT("email"), TEXT("name"), TEXT("tokenName"), TEXT("principal") })
		{
			if (OutOwner.IsEmpty()) { OutOwner = FieldAsString(Whoami, Key); }
		}
	}
	if (const TSharedPtr<FJsonObject>* Project = ObjectField(Whoami, TEXT("project")))
	{
		OutProjectName = FieldAsString(*Project, TEXT("name"));
		if (OutProjectName.IsEmpty()) { OutProjectName = FieldAsString(*Project, TEXT("identifier")); }
	}
	if (OutProjectName.IsEmpty()) { OutProjectName = FieldAsString(Whoami, TEXT("projectName")); }
}

void FProsettaDashboardModel::Connect()
{
	if (!Deps.Runner) { Recompute(); Broadcast(); return; }
	if (bConnecting) { return; }
	bConnecting = true;
	bWhoamiError = false;
	State.LastError.Empty();
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	SetBusy(TEXT("Reading credentials"));
	Deps.Runner->Credentials(FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		FProsettaCredentialsSummary Summary;
		if (!Result.Succeeded() || !ParseCredentialsReport(Result.LastObject(), Summary))
		{
			Self->bConnecting = false;
			Self->bWhoamiKnown = false;
			Self->bCredentialsKnown = false;
			Self->State.Credentials = FProsettaCredentialsSummary();
			Self->Fail(Result.bLaunched ? TEXT("Credentials report failed: ") + Result.UserError : TEXT("Could not run the Prosetta wrapper: ") + Result.UserError);
			Self->ClearBusy();
			return;
		}
		Self->State.Credentials = Summary;
		Self->bCredentialsKnown = true;
		if (!Summary.bCliAvailable)
		{
			Self->bConnecting = false;
			Self->bWhoamiKnown = false;
			Self->State.LastError = TEXT("prosetta-sync CLI not found (install it or set PROSETTA_SYNC_BIN).");
			Self->ClearBusy();
			return;
		}
		if (!Summary.HasCredentials())
		{
			Self->bConnecting = false;
			Self->bWhoamiKnown = false;
			Self->ClearBusy();
			return;
		}
		Self->ClearBusy();
		Self->ConnectWhoami();
	}));
}

void FProsettaDashboardModel::ConnectWhoami()
{
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	SetBusy(TEXT("Checking who the token belongs to"));
	Deps.Runner->Whoami(State.Profile, FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		Self->bConnecting = false;
		Self->bWhoamiOk = Result.Succeeded();
		// Only a verdict on the token counts as AuthFailed; offline, timeouts and wrapper errors stay an Error.
		Self->bWhoamiKnown = Self->bWhoamiOk || IsCredentialRejection(Result);
		Self->bWhoamiError = !Self->bWhoamiKnown;
		if (Self->bWhoamiOk)
		{
			FString Owner, ProjectName;
			ParseWhoami(Result.LastObject(), Owner, ProjectName);
			Self->State.Credentials.Owner = Owner;
			if (!ProjectName.IsEmpty()) { Self->State.Credentials.ProjectName = ProjectName; }
			// Session overrides from the profile win over the config's target for the recent list.
			if (!Self->State.Profile.BaseUrl.IsEmpty()) { Self->State.Credentials.BaseUrl = Self->State.Profile.BaseUrl; }
			if (!Self->State.Profile.ProjectId.IsEmpty()) { Self->State.Credentials.ProjectId = Self->State.Profile.ProjectId; }
			Self->PushRecentConnection();
			FProsettaChangeEntry Entry;
			Entry.Time = FDateTime::UtcNow();
			Entry.Message = Self->State.Credentials.Describe();
			Self->AddChange(MoveTemp(Entry));
		}
		else
		{
			Self->State.LastError = TEXT("whoami failed: ") + Result.UserError;
			FProsettaChangeEntry Entry;
			Entry.Time = FDateTime::UtcNow();
			Entry.Kind = EProsettaChangeKind::Error;
			Entry.Message = Self->State.LastError;
			Self->AddChange(MoveTemp(Entry));
		}
		Self->ClearBusy();
	}));
	Deps.Runner->Version(FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self || !Result.Succeeded()) { return; }
		FString Version = Result.TextLines.IsEmpty() ? FString() : Result.TextLines.Last();
		if (const TSharedPtr<FJsonObject> Object = Result.LastObject()) { Version = FieldAsString(Object, TEXT("version")); }
		if (!Version.IsEmpty())
		{
			Self->State.CliVersion = Version.TrimStartAndEnd();
			if (Self->Deps.UserSettings) { Self->Deps.UserSettings->LastKnownCliVersion = Self->State.CliVersion; }
			Self->Broadcast();
		}
	}));
}

void FProsettaDashboardModel::RefreshWhoAmI()
{
	if (!Deps.Runner || bConnecting) { return; }
	ConnectWhoami();
}

void FProsettaDashboardModel::WriteCredentials(const FString& BaseUrl, const FString& ProjectId, FString&& Token)
{
	if (!Deps.Runner) { return; }
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	SetBusy(TEXT("Writing local credentials"));
	Deps.Runner->Init(BaseUrl, ProjectId, MoveTemp(Token), FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		if (Result.Succeeded())
		{
			if (Self->Deps.Toast) { Self->Deps.Toast(LOCTEXT("CredentialsSaved", "Credentials saved to local config."), EProsettaToastKind::Success); }
			FProsettaChangeEntry Entry;
			Entry.Time = FDateTime::UtcNow();
			Entry.Message = TEXT("Credentials written to the ignored local config (no remote write).");
			Self->AddChange(MoveTemp(Entry));
			Self->ClearBusy();
			Self->Connect();
		}
		else
		{
			Self->Fail(TEXT("init failed: ") + Result.UserError);
			Self->ClearBusy();
		}
	}));
	Token.Empty();
}

void FProsettaDashboardModel::UseRecentConnection(int32 Index)
{
	if (!State.RecentConnections.IsValidIndex(Index)) { return; }
	const FProsettaRecentConnection& Recent = State.RecentConnections[Index];
	State.Profile.BaseUrl = Recent.BaseUrl;
	State.Profile.ProjectId = Recent.ProjectId;
	Recompute();
	Broadcast();
	Connect();
}

// ---- watch / sync / verify ------------------------------------------------------------------------

void FProsettaDashboardModel::StartWatch()
{
	TArray<FText> Problems;
	if (!State.Profile.Validate(Problems))
	{
		Fail(TEXT("Watch not started: ") + Problems[0].ToString());
		return;
	}
	if (!Deps.StartWatch) { Fail(TEXT("No editor watch is installed (unattended editor?)."), EProsettaChangeKind::Info); return; }
	if (Deps.Runner && State.Profile.ScopeString().IsEmpty() && !State.Credentials.ConfigVolumes.IsEmpty())
	{
		// The watch passes --volumes only when non-empty, so an empty scope would silently follow a stale config scope
		// (e.g. left by a volume switch). Clear it first, then start.
		if (State.IsBusy()) { Fail(FString::Printf(TEXT("Watch not started: the dashboard is busy (%s); try again."), *State.Busy), EProsettaChangeKind::Info); return; }
		TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
		SetBusy(TEXT("Clearing the stale sync scope"));
		ApplyConfigScope(FString(), [Weak](const FString& Error)
		{
			TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
			if (!Self) { return; }
			Self->ClearBusy();
			if (!Error.IsEmpty()) { Self->Fail(TEXT("Watch not started: ") + Error); return; }
			Self->StartWatch();
		});
		return;
	}
	if (Deps.IsWatchRunning ? Deps.IsWatchRunning() : State.bWatchRunning)
	{
		// The editor watch ignores Start while running and refuses it while the old CLI is stopping: stop now, start from Poll.
		if (Deps.StopWatch) { Deps.StopWatch(); }
		QueueWatchRestart();
		FProsettaChangeEntry Entry;
		Entry.Time = FDateTime::UtcNow();
		Entry.Message = TEXT("Watch restarting with the current profile.");
		AddChange(MoveTemp(Entry));
		Broadcast();
		return;
	}
	bWatchRestartPending = false;
	Deps.StartWatch(State.Profile);
	if (Deps.IsWatchRunning && !Deps.IsWatchRunning())
	{
		// Start refused (the previous CLI is still stopping, or the script or Python is missing): retry from Poll rather than claim it started.
		QueueWatchRestart();
		FProsettaChangeEntry Pending;
		Pending.Time = FDateTime::UtcNow();
		Pending.Message = TEXT("Watch is waiting for the previous CLI to stop.");
		AddChange(MoveTemp(Pending));
		Broadcast();
		return;
	}
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Message = FString::Printf(TEXT("Watch started (%s, scope %s)."), *State.Profile.ChannelName(), State.Profile.ScopeString().IsEmpty() ? TEXT("project") : *State.Profile.ScopeString());
	AddChange(MoveTemp(Entry));
	Broadcast();
}

void FProsettaDashboardModel::QueueWatchRestart()
{
	bWatchRestartPending = true;
	WatchRestartDeadline = FPlatformTime::Seconds() + WatchRestartTimeoutSeconds;
}

bool FProsettaDashboardModel::TryRestartWatch()
{
	if (!bWatchRestartPending || !Deps.StartWatch) { bWatchRestartPending = false; return false; }
	Deps.StartWatch(State.Profile);
	if (!Deps.IsWatchRunning || Deps.IsWatchRunning())
	{
		bWatchRestartPending = false;
		FProsettaChangeEntry Entry;
		Entry.Time = FDateTime::UtcNow();
		Entry.Message = FString::Printf(TEXT("Watch restarted (%s, scope %s)."), *State.Profile.ChannelName(), State.Profile.ScopeString().IsEmpty() ? TEXT("project") : *State.Profile.ScopeString());
		AddChange(MoveTemp(Entry));
		return true;
	}
	if (FPlatformTime::Seconds() > WatchRestartDeadline)
	{
		bWatchRestartPending = false;
		Fail(TEXT("The watch did not restart; press Start watch again."));
		return true;
	}
	return false;
}

void FProsettaDashboardModel::StopWatch()
{
	bWatchRestartPending = false;
	if (Deps.StopWatch) { Deps.StopWatch(); }
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Message = TEXT("Watch stopped.");
	AddChange(MoveTemp(Entry));
	Broadcast();
}

void FProsettaDashboardModel::AddChangesFromSyncLines(const TArray<TSharedPtr<FJsonObject>>& Objects)
{
	for (const TSharedPtr<FJsonObject>& Object : Objects)
	{
		FString Line;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer)) { continue; }
		FProsettaChangeEntry Entry;
		if (ParseRecentChange(Line, FDateTime::UtcNow(), Entry)) { AddChange(MoveTemp(Entry)); }
	}
}

void FProsettaDashboardModel::HandleSyncResult(const FProsettaCliResult& Result, bool bWasVolumeSwitch)
{
	const FString OldNativeSha = LastNativeBindingsSha;
	RefreshManifest();
	if (!Result.Succeeded())
	{
		Fail(TEXT("sync failed: ") + Result.UserError);
		return;
	}
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Kind = EProsettaChangeKind::Sync;
	Entry.GenerationId = State.Manifest.GenerationId;
	Entry.Scope = State.Manifest.Scope;
	Entry.Message = bWasVolumeSwitch ? TEXT("Volume synced to the mirror.") : TEXT("Sync completed; mirror refreshed.");
	if (const TSharedPtr<FJsonObject> Last = Result.LastObject())
	{
		const FString Status = FieldAsString(Last, TEXT("status"));
		if (!Status.IsEmpty()) { Entry.Message += TEXT(" (") + Status + TEXT(")"); }
	}
	AddChange(MoveTemp(Entry));
	if (!OldNativeSha.IsEmpty() && !LastNativeBindingsSha.IsEmpty() && OldNativeSha != LastNativeBindingsSha && Deps.Toast)
	{
		Deps.Toast(LOCTEXT("NativeBindingsChanged", "Native bindings changed: run Prosetta.NativeBindings.Generate and rebuild C++."), EProsettaToastKind::Info);
	}
	if (Deps.Toast) { Deps.Toast(LOCTEXT("SyncDone", "Prosetta sync completed."), EProsettaToastKind::Success); }
	EnqueueReload(true);
}

void FProsettaDashboardModel::SyncOnce()
{
	if (!Deps.Runner) { return; }
	TArray<FText> Problems;
	if (!State.Profile.Validate(Problems)) { Fail(TEXT("Sync not started: ") + Problems[0].ToString()); return; }
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	SetBusy(TEXT("Syncing"));
	auto RunSync = [Weak]()
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		Self->Deps.Runner->Sync(Self->State.Profile, FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
		{
			TSharedPtr<FProsettaDashboardModel> Inner = Weak.Pin();
			if (!Inner) { return; }
			Inner->HandleSyncResult(Result, false);
			Inner->ClearBusy();
		}));
	};
	// `sync` reads its scope from the local config: write the profile's volumes there first. An empty scope means the whole
	// live project, so it clears a stale config scope (e.g. left by a volume switch) instead of following it.
	const FString Scope = State.Profile.ScopeString();
	if (Scope == State.Credentials.ConfigVolumes) { RunSync(); return; }
	ApplyConfigScope(Scope, [Weak, RunSync](const FString& Error)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		if (!Error.IsEmpty())
		{
			Self->Fail(TEXT("Sync not started: ") + Error);
			Self->ClearBusy();
			return;
		}
		RunSync();
	});
}

void FProsettaDashboardModel::ApplyConfigScope(const FString& Scope, TFunction<void(const FString& Error)> Done)
{
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	Deps.Runner->ConfigSetVolumes(Scope, FProsettaCliCompleted::CreateLambda([Weak, Scope, Done = MoveTemp(Done)](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		if (!Result.Succeeded())
		{
			Done(TEXT("could not write the sync scope to local config: ") + Result.UserError);
			return;
		}
		Self->State.Credentials.ConfigVolumes = Scope;
		Done(FString());
	}));
}

void FProsettaDashboardModel::Verify()
{
	if (!Deps.Runner) { return; }
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	SetBusy(TEXT("Verifying mirror"));
	Deps.Runner->Verify(State.Profile, FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		FProsettaChangeEntry Entry;
		Entry.Time = FDateTime::UtcNow();
		Entry.Kind = Result.Succeeded() ? EProsettaChangeKind::Verify : EProsettaChangeKind::Error;
		Entry.Message = Result.Succeeded() ? TEXT("Verify passed: mirror matches the cache and manifest.") : TEXT("Verify failed: ") + Result.UserError;
		Self->AddChange(MoveTemp(Entry));
		if (Self->Deps.Toast)
		{
			Self->Deps.Toast(Result.Succeeded() ? LOCTEXT("VerifyOk", "Prosetta mirror verified.") : LOCTEXT("VerifyFailed", "Prosetta verify failed; see Recent changes."), Result.Succeeded() ? EProsettaToastKind::Success : EProsettaToastKind::Fail);
		}
		Self->RefreshManifest();
		Self->ClearBusy();
	}));
}

void FProsettaDashboardModel::RefreshVolumes()
{
	if (!Deps.Runner) { return; }
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	SetBusy(TEXT("Listing volumes"));
	Deps.Runner->Volumes(State.Profile, FProsettaCliCompleted::CreateLambda([Weak](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		if (Result.Succeeded() && !Result.JsonLines.IsEmpty())
		{
			FProsettaVolumesListing Listing;
			if (UProsettaSyncInspector::ParseVolumesListing(Result.JsonLines.Last(), Listing))
			{
				Self->State.Volumes = Listing;
				Self->State.bHasVolumes = true;
				if (Self->State.Credentials.ProjectName.IsEmpty()) { Self->State.Credentials.ProjectName = Listing.ProjectName; }
			}
			else
			{
				Self->Fail(TEXT("volumes --json returned an unexpected shape."));
			}
		}
		else
		{
			Self->Fail(TEXT("volumes failed: ") + Result.UserError);
		}
		Self->ClearBusy();
	}));
}

// ---- volume switch ---------------------------------------------------------------------------------

bool FProsettaDashboardModel::SwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, FString& OutError)
{
	if (!Deps.Runner) { OutError = TEXT("No CLI available."); return false; }
	if (!FProsettaVolumeScope::IsValidSlug(Slug)) { OutError = FString::Printf(TEXT("'%s' is not a valid volume slug."), *Slug); return false; }
	if (Deps.IsPIE ? Deps.IsPIE() : State.bPIE) { OutError = TEXT("Stop Play before swapping volumes: the mirror the running story reads would change under it."); return false; }
	if (State.IsBusy()) { OutError = FString::Printf(TEXT("The dashboard is busy (%s)."), *State.Busy); return false; }
	const bool bWatchWasRunning = Deps.IsWatchRunning ? Deps.IsWatchRunning() : State.bWatchRunning;
	const bool bFixedTag = !Tag.IsEmpty() && Tag != TEXT("latest");
	if (bWatchWasRunning) { StopWatch(); }
	const FProsettaSyncProfile Previous = State.Profile;
	// The profile follows the swap so Run and the watch use the same scope.
	FProsettaVolumeScope Scope;
	Scope.Slug = Slug;
	Scope.Tag = Tag;
	State.Profile.Volumes = { Scope };
	if (bFixedTag && State.Profile.Mode == EProsettaSyncMode::Watch) { State.Profile.Mode = EProsettaSyncMode::SyncOnce; }
	Recompute();
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	SetBusy(FString::Printf(TEXT("Switching to volume %s"), *Slug));
	Deps.Runner->ConfigSetVolumes(Scope.ToString(), FProsettaCliCompleted::CreateLambda([Weak, Slug, Tag, bRepoint, bWatchWasRunning, bFixedTag, Previous](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		if (!Result.Succeeded())
		{
			Self->Fail(TEXT("Could not set the sync scope: ") + Result.UserError);
			// Nothing was switched: put the profile back and resume the watch the switch stopped.
			Self->State.Profile = Previous;
			if (bWatchWasRunning) { Self->QueueWatchRestart(); }
			Self->ClearBusy();
			return;
		}
		Self->State.Credentials.ConfigVolumes = Self->State.Profile.ScopeString();
		Self->ContinueSwitchVolume(Slug, Tag, bRepoint, bWatchWasRunning && !bFixedTag);
	}));
	return true;
}

void FProsettaDashboardModel::ContinueSwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, bool bRestartWatch)
{
	TWeakPtr<FProsettaDashboardModel> Weak = AsShared();
	Deps.Runner->Sync(State.Profile, FProsettaCliCompleted::CreateLambda([Weak, Slug, Tag, bRepoint, bRestartWatch](const FProsettaCliResult& Result)
	{
		TSharedPtr<FProsettaDashboardModel> Self = Weak.Pin();
		if (!Self) { return; }
		const FString OldNativeSha = Self->LastNativeBindingsSha;
		Self->RefreshManifest();
		if (!Result.Succeeded())
		{
			Self->Fail(FString::Printf(TEXT("Sync of volume %s failed: %s"), *Slug, *Result.UserError));
			// The scope is already switched; the watch the switch stopped resumes on it.
			if (bRestartWatch) { Self->QueueWatchRestart(); }
			Self->ClearBusy();
			return;
		}
		FProsettaChangeEntry Entry;
		Entry.Time = FDateTime::UtcNow();
		Entry.Kind = EProsettaChangeKind::Sync;
		Entry.GenerationId = Self->State.Manifest.GenerationId;
		Entry.Scope = Self->State.Manifest.Scope;
		Entry.Message = FString::Printf(TEXT("Switched the sync scope to %s%s and re-pulled the mirror."), *Slug, Tag.IsEmpty() ? TEXT("") : *(TEXT("@") + Tag));
		Self->AddChange(MoveTemp(Entry));
		if (!OldNativeSha.IsEmpty() && !Self->LastNativeBindingsSha.IsEmpty() && OldNativeSha != Self->LastNativeBindingsSha && Self->Deps.Toast)
		{
			Self->Deps.Toast(LOCTEXT("NativeBindingsChanged", "Native bindings changed: run Prosetta.NativeBindings.Generate and rebuild C++."), EProsettaToastKind::Info);
		}
		Self->FinishSwitchVolume(Slug, Tag, bRepoint, bRestartWatch);
	}));
}

void FProsettaDashboardModel::FinishSwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, bool bRestartWatch)
{
	if (bRepoint)
	{
		FString Error;
		if (Deps.RePoint && Deps.RePoint(Slug, Tag, Error))
		{
			FProsettaChangeEntry Entry;
			Entry.Time = FDateTime::UtcNow();
			Entry.Message = FString::Printf(TEXT("Open level re-pointed at volume %s (undoable)."), *Slug);
			AddChange(MoveTemp(Entry));
		}
		else
		{
			Fail(TEXT("Level not re-pointed: ") + (Error.IsEmpty() ? TEXT("no editor level") : Error), EProsettaChangeKind::Info);
		}
	}
	ClearBusy();
	EnqueueReload(true);
	if (bRestartWatch) { StartWatch(); }
	if (Deps.Toast) { Deps.Toast(FText::Format(LOCTEXT("VolumeSwitched", "Now syncing volume {0}."), FText::FromString(Slug)), EProsettaToastKind::Success); }
	Broadcast();
}

// ---- reload --------------------------------------------------------------------------------------

void FProsettaDashboardModel::EnqueueReload(bool bWithPresentation)
{
	State.bReloadPending = true;
	bReloadWithPresentation = bReloadWithPresentation || bWithPresentation;
	if (!TryRunPendingReload()) { Broadcast(); }
}

bool FProsettaDashboardModel::TryRunPendingReload()
{
	if (!State.bReloadPending) { return false; }
	const bool bPIE = Deps.IsPIE ? Deps.IsPIE() : State.bPIE;
	const bool bSafe = Deps.SafeToApply ? Deps.SafeToApply() : true;
	if (bPIE || !bSafe) { return false; }
	State.bReloadPending = false;
	DoReload();
	bReloadWithPresentation = false;
	Broadcast();
	return true;
}

bool FProsettaDashboardModel::DoReload()
{
	if (!Deps.Reload) { return false; }
	const FString Slug = State.Profile.Volumes.Num() == 1 ? State.Profile.Volumes[0].Slug : FString();
	FString Error;
	const bool bOk = Deps.Reload(State.Profile, Slug, bReloadWithPresentation, Error);
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Kind = bOk ? EProsettaChangeKind::Info : EProsettaChangeKind::Error;
	Entry.GenerationId = State.Manifest.GenerationId;
	Entry.Message = bOk ? TEXT("Story asset rebuilt from the mirror.") : TEXT("Story asset rebuild failed: ") + Error;
	AddChange(MoveTemp(Entry));
	if (!bOk) { State.LastError = Error; }
	return bOk;
}

void FProsettaDashboardModel::ReimportLastGeneration()
{
	EnqueueReload(true);
}

void FProsettaDashboardModel::ApplyPending()
{
	if (State.bExternalChangePending)
	{
		State.bExternalChangePending = false;
		RefreshManifest();
		EnqueueReload(true);
	}
	if (Deps.ApplyPendingImport && State.PendingCount > 0) { Deps.ApplyPendingImport(); }
	TryRunPendingReload();
	Broadcast();
}

void FProsettaDashboardModel::DismissExternalChange()
{
	State.bExternalChangePending = false;
	Broadcast();
}

void FProsettaDashboardModel::OpenBlotter() { if (Deps.OpenBlotter) { Deps.OpenBlotter(); } }
void FProsettaDashboardModel::OpenOutliner() { if (Deps.OpenOutliner) { Deps.OpenOutliner(); } }

// ---- manifest / changes ---------------------------------------------------------------------------

void FProsettaDashboardModel::RefreshManifest()
{
	FProsettaManifestSummary Manifest;
	const FString Path = FPaths::Combine(Deps.MirrorRoot, TEXT("manifest.json"));
	State.bHasManifest = UProsettaSyncInspector::LoadManifestFile(Path, Manifest);
	State.Manifest = State.bHasManifest ? Manifest : FProsettaManifestSummary();
	LastManifestMTime = IFileManager::Get().GetTimeStamp(*Path);
	if (State.bHasManifest)
	{
		const FDateTime Then = State.Manifest.bHasGeneratedAt ? State.Manifest.GeneratedAt : LastManifestMTime;
		const FDateTime Now = FDateTime::UtcNow();
		State.DataAge = UProsettaSyncInspector::DescribeAge(Then, Now);
		State.DataAgeMinutes = Then <= Now ? static_cast<int32>((Now - Then).GetTotalMinutes()) : 0;
		if (const FProsettaArtifactSummary* Native = State.Manifest.FindArtifact(TEXT("native-bindings"))) { LastNativeBindingsSha = Native->Checksum; }
	}
	else
	{
		State.DataAge.Empty();
		State.DataAgeMinutes = INDEX_NONE;
	}
}

void FProsettaDashboardModel::AddChange(FProsettaChangeEntry Entry)
{
	State.Changes.Insert(MoveTemp(Entry), 0);
	if (State.Changes.Num() > MaxChanges) { State.Changes.SetNum(MaxChanges); }
}

void FProsettaDashboardModel::ClearChanges()
{
	State.Changes.Reset();
	Broadcast();
}

bool FProsettaDashboardModel::ParseRecentChange(const FString& ReceiptJson, const FDateTime& Time, FProsettaChangeEntry& Out)
{
	FProsettaWatchEvent Event;
	if (!UProsettaSyncInspector::ParseWatchEvent(ReceiptJson, Event)) { return false; }
	if (Event.Type.IsEmpty()) { return false; }
	Out = FProsettaChangeEntry();
	Out.Time = Time;
	if (Event.Type == TEXT("generation-completed")) { Out.Kind = EProsettaChangeKind::Generation; }
	else if (Event.Type == TEXT("blocked")) { Out.Kind = EProsettaChangeKind::Blocked; }
	else if (Event.Type == TEXT("error") || Event.Type == TEXT("generation-failed")) { Out.Kind = EProsettaChangeKind::Error; }
	else { Out.Kind = EProsettaChangeKind::Info; }
	Out.GenerationId = Event.GenerationId;
	Out.Scope = Event.Scope;
	Out.Channel = Event.Channel;
	Out.ChangedKeys = Event.ChangedKeys;
	Out.RemovedKeys = Event.RemovedKeys;
	Out.ReleaseTag = Event.ReleaseTagId;
	Out.Message = Event.Message.IsEmpty() ? (Event.Code.IsEmpty() ? Event.Type : Event.Code) : Event.Message;
	return true;
}

int32 FProsettaDashboardModel::SeedChangesFromReceipts(const FString& Directory)
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	struct FReceipt { FDateTime Time; FProsettaChangeEntry Entry; };
	TArray<FReceipt> Receipts;
	for (const FString& File : Files)
	{
		const FString Path = FPaths::Combine(Directory, File);
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path)) { continue; }
		FReceipt Receipt;
		Receipt.Time = IFileManager::Get().GetTimeStamp(*Path);
		if (ParseRecentChange(Json, Receipt.Time, Receipt.Entry)) { Receipts.Add(MoveTemp(Receipt)); }
	}
	Receipts.Sort([](const FReceipt& A, const FReceipt& B) { return A.Time < B.Time; });
	for (FReceipt& Receipt : Receipts) { AddChange(MoveTemp(Receipt.Entry)); }
	if (!Receipts.IsEmpty()) { Broadcast(); }
	return Receipts.Num();
}

void FProsettaDashboardModel::NoteExternalChange(const FString& What)
{
	State.bExternalChangePending = true;
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Kind = EProsettaChangeKind::ExternalChange;
	Entry.Message = What;
	AddChange(MoveTemp(Entry));
	Broadcast();
}

void FProsettaDashboardModel::NoteGenerationAccepted(const FString& GenerationId, bool bReload)
{
	FProsettaChangeEntry Entry;
	Entry.Time = FDateTime::UtcNow();
	Entry.Kind = EProsettaChangeKind::Generation;
	Entry.GenerationId = GenerationId;
	Entry.Message = TEXT("Generation accepted by the watch importer.");
	AddChange(MoveTemp(Entry));
	RefreshManifest();
	if (bReload) { EnqueueReload(false); }
	else { Broadcast(); }
}

void FProsettaDashboardModel::Poll(const FProsettaDashboardPollInput& Input)
{
	bool bChanged = false;
	auto Set = [&bChanged](auto& Field, const auto& Value) { if (!(Field == Value)) { Field = Value; bChanged = true; } };
	Set(State.WatchStatus, Input.WatchStatus);
	Set(State.TransportStatus, Input.TransportStatus);
	Set(State.bWatchRunning, Input.bWatchRunning);
	Set(State.bExternalWatch, Input.bExternalWatch);
	Set(State.bCheckingVersion, Input.bCheckingVersion);
	Set(State.PendingCount, Input.PendingCount);
	Set(State.bPIE, Input.bPIE);
	Set(State.RunningStories, Input.RunningStories);
	if (Input.ManifestMTime != LastManifestMTime)
	{
		RefreshManifest();
		LastManifestMTime = Input.ManifestMTime;
		bChanged = true;
	}
	else if (State.bHasManifest)
	{
		const FDateTime Then = State.Manifest.bHasGeneratedAt ? State.Manifest.GeneratedAt : LastManifestMTime;
		Set(State.DataAge, UProsettaSyncInspector::DescribeAge(Then, Input.Now));
		Set(State.DataAgeMinutes, Then <= Input.Now ? static_cast<int32>((Input.Now - Then).GetTotalMinutes()) : 0);
	}
	if (bWatchRestartPending && !Input.bWatchRunning && TryRestartWatch()) { bChanged = true; }
	const EProsettaDashboardConnection Before = State.Connection;
	const FString StatusBefore = State.StatusText;
	Recompute();
	if (Before != State.Connection || StatusBefore != State.StatusText) { bChanged = true; }
	if (State.bReloadPending && !Input.bPIE && Input.bSafeToApply)
	{
		if (TryRunPendingReload()) { return; }
	}
	if (bChanged) { Broadcast(); }
}

#undef LOCTEXT_NAMESPACE
