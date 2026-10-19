#pragma once
#include "CoreMinimal.h"
#include "ProsettaDashboardSettings.h"
#include "Inkpot/Prosetta/ProsettaSyncInspector.h"
#include "ProsettaSyncProfile.h"

class FJsonObject;
class FProsettaCliRunner;
class FProsettaProfileStore;
struct FProsettaCliResult;

/** Headline connection state, derived from the CLI, the credentials report, whoami and the watch. */
enum class EProsettaDashboardConnection : uint8
{
	/** The prosetta.py wrapper or its Python is missing, or the CLI binary cannot be found. */
	NoCli,
	/** The offline credentials report found no token source (first-run checklist). */
	NoCredentials,
	Disconnected,
	Connecting,
	Connected,
	/** Connected but the watch is reconnecting or a generation failed and is being retried. */
	Degraded,
	/** The watch reported a blocked (409 style) state; last assets retained. */
	Blocked,
	/** whoami was rejected: the token is wrong, revoked or for another project. */
	AuthFailed,
	Error
};

enum class EProsettaChangeKind : uint8 { Generation, Sync, Verify, Error, Blocked, Info, ExternalChange };

enum class EProsettaToastKind : uint8 { Info, Success, Fail };

/** One row of the recent-changes list; built from watch receipts, CLI results and dashboard actions. */
struct PROSETTADASHBOARD_API FProsettaChangeEntry
{
	FDateTime Time;
	EProsettaChangeKind Kind = EProsettaChangeKind::Info;
	FString GenerationId;
	FString Scope;
	FString Channel;
	TArray<FString> ChangedKeys;
	TArray<FString> RemovedKeys;
	FString ReleaseTag;
	FString Message;
	/** First 12 characters of the generation id. */
	FString ShortGenerationId() const { return GenerationId.Left(12); }
	/** One line for the list and the log. */
	FString Describe() const;
};

/** What `prosetta.py credentials --json` reports; never carries a token. */
struct PROSETTADASHBOARD_API FProsettaCredentialsSummary
{
	/** env, local, home, native or none. */
	FString Source = TEXT("none");
	FString BaseUrl;
	FString ProjectId;
	FString ProjectName;
	/** Who the token belongs to, from whoami. */
	FString Owner;
	FString LocalConfigPath;
	bool bLocalConfigExists = false;
	bool bSessionOverride = false;
	/** The `volumes` scope the local config holds ("core@1.0,side"); empty for the whole project. */
	FString ConfigVolumes;
	bool bCliAvailable = false;
	FString CliCommand;
	/** Per-file notices from the wrapper's report, e.g. a token file readable by other users. */
	TArray<FString> Warnings;
	bool HasCredentials() const { return Source != TEXT("none") && !Source.IsEmpty(); }
	/** "Connected as <owner> to <project> (<baseUrl>) via <source>". */
	FString Describe() const;
};

/** Everything the widgets draw. Copied out of the model; mutate through commands only. */
struct PROSETTADASHBOARD_API FProsettaDashboardState
{
	EProsettaDashboardConnection Connection = EProsettaDashboardConnection::Disconnected;
	/** One-line headline for the header. */
	FString StatusText;
	/** Raw statuses for the tooltip and the log. */
	FString WatchStatus;
	FString TransportStatus;
	bool bWatchRunning = false;
	bool bExternalWatch = false;
	bool bCheckingVersion = false;
	bool bPIE = false;
	int32 PendingCount = 0;
	FProsettaCredentialsSummary Credentials;
	TArray<FProsettaRecentConnection> RecentConnections;
	FProsettaManifestSummary Manifest;
	bool bHasManifest = false;
	/** "3 minutes ago"; empty without a manifest. */
	FString DataAge;
	/** Minutes since the manifest was generated; INDEX_NONE without one. */
	int32 DataAgeMinutes = INDEX_NONE;
	FProsettaVolumesListing Volumes;
	bool bHasVolumes = false;
	/** The working copy of the active profile (may differ from disk). */
	FProsettaSyncProfile Profile;
	TArray<FString> ProfileNames;
	bool bProfileDirty = false;
	TArray<FText> ProfileProblems;
	/** Newest first, capped. */
	TArray<FProsettaChangeEntry> Changes;
	int32 RunningStories = 0;
	/** What the CLI is doing for the dashboard right now; empty when idle. */
	FString Busy;
	FString CliVersion;
	FString CliScript;
	/** Why there is no wrapper to run (script or Python missing); empty when the runner exists. */
	FString CliSetupProblem;
	/** The mirror changed on disk outside the dashboard; apply is the user's call. */
	bool bExternalChangePending = false;
	/** A reload waits for Play to stop / the editor to be idle. */
	bool bReloadPending = false;
	FString LastError;

	bool IsBusy() const { return !Busy.IsEmpty(); }
	/** Connected, Degraded or Blocked: the credentials work even when the watch does not. */
	bool IsConnected() const;
	/** The first-run checklist shows instead of the dashboard. */
	bool NeedsFirstRun() const { return Connection == EProsettaDashboardConnection::NoCli || Connection == EProsettaDashboardConnection::NoCredentials; }
};

/** The polled facts the subsystem feeds the model twice a second. */
struct PROSETTADASHBOARD_API FProsettaDashboardPollInput
{
	FString WatchStatus;
	FString TransportStatus;
	bool bWatchRunning = false;
	bool bExternalWatch = false;
	bool bCheckingVersion = false;
	int32 PendingCount = 0;
	bool bPIE = false;
	bool bSafeToApply = true;
	int32 RunningStories = 0;
	/** Mirror manifest mtime; zero when absent. */
	FDateTime ManifestMTime;
	FDateTime Now = FDateTime::UtcNow();
};

DECLARE_MULTICAST_DELEGATE(FProsettaDashboardStateChanged);

/** The widgets' view of the dashboard; FProsettaDashboardModel is the real one, tests may fake it. */
class PROSETTADASHBOARD_API IProsettaDashboardModel
{
public:
	virtual ~IProsettaDashboardModel() = default;
	virtual const FProsettaDashboardState& GetState() const = 0;
	virtual FProsettaDashboardStateChanged& OnStateChanged() = 0;

	// Commands return immediately; results arrive through OnStateChanged.
	virtual void Connect() = 0;
	virtual void StartWatch() = 0;
	virtual void StopWatch() = 0;
	virtual void SyncOnce() = 0;
	virtual void Verify() = 0;
	virtual void RefreshVolumes() = 0;
	virtual void RefreshWhoAmI() = 0;
	/** Runs `init --token-stdin`; the token is moved in and wiped after launch. */
	virtual void WriteCredentials(const FString& BaseUrl, const FString& ProjectId, FString&& Token) = 0;
	virtual void UseRecentConnection(int32 Index) = 0;
	/** False (with OutError) when refused, for example during Play. */
	virtual bool SwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, FString& OutError) = 0;
	/** Replaces the working copy (editing), without saving. */
	virtual void SetProfile(const FProsettaSyncProfile& Edited) = 0;
	/** Makes Name the active profile (config scope follows it) and reconnects. */
	virtual bool LoadProfile(const FString& Name, FString& OutError) = 0;
	virtual bool SaveProfile(FString& OutError) = 0;
	virtual bool SaveProfileAs(const FString& NewName, FString& OutError) = 0;
	virtual void RevertProfile() = 0;
	virtual bool DeleteProfile(const FString& Name, FString& OutError) = 0;
	/** Dispatches Run by the profile's mode. */
	virtual void RunProfile() = 0;
	virtual void ReimportLastGeneration() = 0;
	/** Applies an external mirror change or queued generations now (when safe). */
	virtual void ApplyPending() = 0;
	virtual void DismissExternalChange() = 0;
	virtual void OpenBlotter() = 0;
	virtual void OpenOutliner() = 0;
	virtual void ClearChanges() = 0;
};

/** Editor services the model calls; every one is optional so tests can run it headless and offline. */
struct PROSETTADASHBOARD_API FProsettaDashboardModelDeps
{
	/** Null means "no CLI": the model reports NoCli and runs nothing. */
	TSharedPtr<FProsettaCliRunner> Runner;
	/** Why Runner is null (wrapper script or Python missing), shown on the first-run checklist. */
	FString NoRunnerReason;
	FProsettaProfileStore* Store = nullptr;
	/** Persisted user settings (active profile, recent connections); null keeps them in memory. */
	UProsettaDashboardUserSettings* UserSettings = nullptr;
	FString DefaultProfileName = TEXT("Default");
	FString MirrorRoot;
	TFunction<bool()> SafeToApply;
	TFunction<bool()> IsPIE;
	TFunction<void(const FProsettaSyncProfile&)> StartWatch;
	TFunction<void()> StopWatch;
	TFunction<bool()> IsWatchRunning;
	TFunction<void(const FProsettaSyncProfile&)> ApplyProfileEnvironment;
	/** Re-points the open level; false with the reason when there is no level to re-point. */
	TFunction<bool(const FString& Slug, const FString& Tag, FString& OutError)> RePoint;
	/** Rebuilds the story asset (and presentation when asked) from the mirror. */
	TFunction<bool(const FProsettaSyncProfile&, const FString& Slug, bool bWithPresentation, FString& OutError)> Reload;
	/** The watch importer's TryApplyPending. */
	TFunction<bool()> ApplyPendingImport;
	TFunction<void(const FText&, EProsettaToastKind)> Toast;
	TFunction<void()> OpenBlotter;
	TFunction<void()> OpenOutliner;
};

class PROSETTADASHBOARD_API FProsettaDashboardModel final : public IProsettaDashboardModel, public TSharedFromThis<FProsettaDashboardModel>
{
public:
	static constexpr int32 MaxChanges = 200;
	/** How long a restart keeps retrying Start while the previous watch CLI stops. */
	static constexpr double WatchRestartTimeoutSeconds = 15.0;
	explicit FProsettaDashboardModel(FProsettaDashboardModelDeps InDeps);
	virtual ~FProsettaDashboardModel() override;

	virtual const FProsettaDashboardState& GetState() const override { return State; }
	virtual FProsettaDashboardStateChanged& OnStateChanged() override { return StateChanged; }
	virtual void Connect() override;
	virtual void StartWatch() override;
	virtual void StopWatch() override;
	virtual void SyncOnce() override;
	virtual void Verify() override;
	virtual void RefreshVolumes() override;
	virtual void RefreshWhoAmI() override;
	virtual void WriteCredentials(const FString& BaseUrl, const FString& ProjectId, FString&& Token) override;
	virtual void UseRecentConnection(int32 Index) override;
	virtual bool SwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, FString& OutError) override;
	virtual void SetProfile(const FProsettaSyncProfile& Edited) override;
	virtual bool LoadProfile(const FString& Name, FString& OutError) override;
	virtual bool SaveProfile(FString& OutError) override;
	virtual bool SaveProfileAs(const FString& NewName, FString& OutError) override;
	virtual void RevertProfile() override;
	virtual bool DeleteProfile(const FString& Name, FString& OutError) override;
	virtual void RunProfile() override;
	virtual void ReimportLastGeneration() override;
	virtual void ApplyPending() override;
	virtual void DismissExternalChange() override;
	virtual void OpenBlotter() override;
	virtual void OpenOutliner() override;
	virtual void ClearChanges() override;

	/** Feeds the polled facts; broadcasts only when something the widgets show changed. Runs a pending reload when safe. */
	void Poll(const FProsettaDashboardPollInput& Input);
	/** Re-reads the active profile and the profile list from the store. */
	void ReloadProfiles();
	/** Re-parses <MirrorRoot>/manifest.json. */
	void RefreshManifest();
	/** The mirror changed outside the dashboard: detection only, never an import. */
	void NoteExternalChange(const FString& What);
	/** Records an accepted generation and queues the story asset reload (presentation already refreshed by the module). */
	void NoteGenerationAccepted(const FString& GenerationId, bool bReload);
	void AddChange(FProsettaChangeEntry Entry);
	/** Seeds the change list from the receipts a terminal watch left in Directory (newest first). */
	int32 SeedChangesFromReceipts(const FString& Directory);
	/** Queues a reload; runs it now when SafeToApply says so and Play is not running. */
	void EnqueueReload(bool bWithPresentation);
	/** Runs the queued reload when it is safe; returns true when one ran. */
	bool TryRunPendingReload();
	const FProsettaDashboardModelDeps& GetDeps() const { return Deps; }

	/** Pure classification used by Poll; whoami facts come from the last Connect. */
	static EProsettaDashboardConnection ClassifyConnection(const FString& WatchStatus, const FString& TransportStatus, bool bWhoamiOk, bool bWhoamiKnown);
	/** A change entry from a watch receipt / event line; false when the line is not a watch event. */
	static bool ParseRecentChange(const FString& ReceiptJson, const FDateTime& Time, FProsettaChangeEntry& Out);
	/** Fills a credentials summary from the `credentials --json` report. */
	static bool ParseCredentialsReport(const TSharedPtr<FJsonObject>& Report, FProsettaCredentialsSummary& Out);
	/** Picks an owner / project name out of a `whoami --json` object, whatever its exact shape. */
	static void ParseWhoami(const TSharedPtr<FJsonObject>& Whoami, FString& OutOwner, FString& OutProjectName);
	static FText DescribeConnection(EProsettaDashboardConnection Connection);

private:
	void Broadcast();
	void SetBusy(const FString& What);
	void ClearBusy();
	void Fail(const FString& Error, EProsettaChangeKind Kind = EProsettaChangeKind::Error);
	void Recompute();
	void PersistUserSettings();
	void PushRecentConnection();
	FString ActiveProfileName() const;
	void SetActiveProfileName(const FString& Name);
	/** Second half of Connect: whoami (+ version). */
	void ConnectWhoami();
	/** Continues SwitchVolume after the scope was written. */
	void ContinueSwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, bool bRestartWatch);
	void FinishSwitchVolume(const FString& Slug, const FString& Tag, bool bRepoint, bool bRestartWatch);
	void HandleSyncResult(const FProsettaCliResult& Result, bool bWasVolumeSwitch);
	void AddChangesFromSyncLines(const TArray<TSharedPtr<FJsonObject>>& Objects);
	bool DoReload();
	/** Has Poll start the watch once the stopped CLI is gone, giving up after WatchRestartTimeoutSeconds. */
	void QueueWatchRestart();
	/** Starts the watch a restart stopped, once the old CLI is gone; true when the restart finished or gave up. */
	bool TryRestartWatch();
	/** Store->IsDirty, re-evaluated only when the working copy or the profile file changed. */
	bool IsProfileDirty();

	FProsettaDashboardModelDeps Deps;
	FProsettaDashboardState State;
	FProsettaDashboardStateChanged StateChanged;
	bool bCredentialsKnown = false;
	bool bWhoamiKnown = false;
	bool bWhoamiOk = false;
	/** whoami failed without judging the token (offline, timeout, wrapper error). */
	bool bWhoamiError = false;
	bool bWatchRestartPending = false;
	double WatchRestartDeadline = 0.0;
	bool bDirtyCacheValid = false;
	bool bDirtyCacheValue = false;
	FDateTime DirtyCacheStamp;
	FProsettaSyncProfile DirtyCacheProfile;
	bool bConnecting = false;
	int32 InFlight = 0;
	bool bReloadWithPresentation = false;
	FDateTime LastManifestMTime;
	FString InMemoryActiveProfile;
	TArray<FProsettaRecentConnection> InMemoryRecent;
	/** Native-bindings checksum last seen, to spot a change that needs a C++ rebuild. */
	FString LastNativeBindingsSha;
};
