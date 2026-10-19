#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "ProsettaSyncProfile.generated.h"

/** What the dashboard's Run button does with a profile. */
UENUM()
enum class EProsettaSyncMode : uint8
{
	/** `prosetta.py watch`: stream completed generations into the editor. */
	Watch,
	/** `prosetta.py sync --json`: one pull into the mirror. */
	SyncOnce,
	/** `prosetta.py verify --json`: check the mirror without pulling. */
	Verify,
};

/** Which generations a watch follows. */
UENUM()
enum class EProsettaWatchChannel : uint8
{
	Live UMETA(DisplayName="live"),
	LatestRelease UMETA(DisplayName="latest-release"),
};

/** One entry of the CLI sync scope: `<slug>` or `<slug>@<tag>`. */
USTRUCT()
struct PROSETTADASHBOARD_API FProsettaVolumeScope
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Scope")
	FString Slug;
	/** Empty or "latest" follows the channel; anything else pins a release and needs Sync once. */
	UPROPERTY(EditAnywhere, Category="Scope")
	FString Tag;

	FProsettaVolumeScope() = default;
	FProsettaVolumeScope(FString InSlug, FString InTag = FString()) : Slug(MoveTemp(InSlug)), Tag(MoveTemp(InTag)) {}
	bool IsFixedTag() const { return !Tag.IsEmpty() && Tag != TEXT("latest"); }
	/** `slug` or `slug@tag`. */
	FString ToString() const;
	static FProsettaVolumeScope Parse(const FString& Entry);
	static bool IsValidSlug(const FString& Slug);
	bool operator==(const FProsettaVolumeScope& Other) const { return Slug == Other.Slug && Tag == Other.Tag; }
};

/**
 * A named, tracked sync configuration: Config/ProsettaSync/Profiles/<Name>.json. Holds no secret:
 * the token stays in the ignored CLI config (or PROSETTA_PROJECT_TOKEN) and C++ never reads it.
 * CachePath/MirrorPath are informational; the wrapper injects the real ones.
 */
USTRUCT()
struct PROSETTADASHBOARD_API FProsettaSyncProfile
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY()
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY(VisibleAnywhere, Category="Profile")
	FString Name = TEXT("Default");

	UPROPERTY(EditAnywhere, Category="Profile")
	EProsettaSyncMode Mode = EProsettaSyncMode::Watch;

	/** Which generations a watch follows; latest-release needs exactly one volume. */
	UPROPERTY(EditAnywhere, Category="Watch", meta=(EditCondition="Mode == EProsettaSyncMode::Watch", EditConditionHides))
	EProsettaWatchChannel Channel = EProsettaWatchChannel::Live;

	/**
	 * Sync scope. Empty syncs the whole live project (canonical story): the dashboard clears the CLI's local `volumes`
	 * config (`config clear volumes`) before Sync and before starting the watch, so a scope left by an earlier volume
	 * switch is not followed. An empty scope is not the same as ticking the default volume, which is an explicit scope
	 * with its own cache and manifest scope marker.
	 */
	UPROPERTY(EditAnywhere, Category="Scope", meta=(EditCondition="Mode != EProsettaSyncMode::Verify", EditConditionHides))
	TArray<FProsettaVolumeScope> Volumes;

	/** Watch without refreshing Saved/Prosetta (generations stay in the cache only). */
	UPROPERTY(EditAnywhere, Category="Watch", meta=(EditCondition="Mode == EProsettaSyncMode::Watch", EditConditionHides))
	bool bNoMirror = false;

	/** Refresh the dialog presentation and story asset when a watched generation is accepted. */
	UPROPERTY(EditAnywhere, Category="Watch", meta=(EditCondition="Mode == EProsettaSyncMode::Watch", EditConditionHides))
	bool bImportOnAccept = true;

	/** FFmpeg used for compressed voice takes; empty uses the verified cache. */
	UPROPERTY(EditAnywhere, Category="Watch", meta=(EditCondition="Mode == EProsettaSyncMode::Watch && bImportOnAccept", EditConditionHides, FilePathFilter="ffmpeg"))
	FFilePath AudioDecoderPath;

	/** `sync --repair`: re-download artifacts whose checksum does not match. */
	UPROPERTY(EditAnywhere, Category="Sync", meta=(EditCondition="Mode == EProsettaSyncMode::SyncOnce", EditConditionHides))
	bool bRepair = false;

	/** `sync --snapshot`: force a full snapshot pull instead of a delta. */
	UPROPERTY(EditAnywhere, Category="Sync", meta=(EditCondition="Mode == EProsettaSyncMode::SyncOnce", EditConditionHides))
	bool bSnapshot = false;

	/** Overrides the local config's base URL for this profile only (passed to the wrapper, never written to config). Empty uses the local config. */
	UPROPERTY(EditAnywhere, Category="Connection")
	FString BaseUrl;

	/** Overrides the local config's project id for this profile only. Empty uses the local config. */
	UPROPERTY(EditAnywhere, Category="Connection")
	FString ProjectId;

	/** Exported as PROSETTA_SYNC_BIN so the dashboard and watch resolve the same CLI binary. */
	UPROPERTY(EditAnywhere, Category="Advanced")
	FFilePath CliBinaryOverride;

	/** Start the watch when the editor finishes loading. */
	UPROPERTY(EditAnywhere, Category="Advanced")
	bool bAutoStartWatchOnEditorLaunch = false;

	/** Where the CLI keeps generations; injected by the wrapper, shown for reference. */
	UPROPERTY(VisibleAnywhere, Category="Paths")
	FString CachePath;

	/** The Unreal mirror the wrapper refreshes; always Saved/Prosetta. */
	UPROPERTY(VisibleAnywhere, Category="Paths")
	FString MirrorPath;

	/** False with every rule the profile breaks (see the dashboard's validation matrix). */
	bool Validate(TArray<FText>& OutErrors) const;
	/** `core@1.0,side` for `config set volumes`; empty for the live project. */
	FString ScopeString() const;
	/** Any volume pinned to a release tag. */
	bool IsFixedTag() const;
	/** The CLI's channel name. */
	FString ChannelName() const;
	/** `--channel … [--volumes …] [--no-mirror] [--base-url …] [--project-id …]` after `watch`. */
	TArray<FString> BuildWatchFlags() const;
	/** `[--repair] [--snapshot] [--base-url …] [--project-id …]` after `sync --json`. */
	TArray<FString> BuildSyncFlags() const;
	/** `[--base-url …] [--project-id …]`, the connection overrides every command takes. */
	TArray<FString> ConnectionFlags() const;

	/** Pretty JSON; never contains a token field. */
	FString ToJson() const;
	/** Unknown keys are ignored; a newer schemaVersion is refused. */
	static bool FromJson(const FString& Json, FProsettaSyncProfile& Out, FString& OutError);
	bool operator==(const FProsettaSyncProfile& Other) const;
	bool operator!=(const FProsettaSyncProfile& Other) const { return !(*this == Other); }
};
