#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"
#include "ProsettaDashboardSettings.generated.h"

/** Project-wide (Config/DefaultEditor.ini): where tracked sync profiles live and which one a fresh checkout starts with. */
UCLASS(config=Editor, defaultconfig, meta=(DisplayName="Prosetta Sync"))
class PROSETTADASHBOARD_API UProsettaSyncProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UProsettaSyncProjectSettings();
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Relative to the project directory. */
	UPROPERTY(config, EditAnywhere, Category="Profiles")
	FDirectoryPath ProfilesDirectory;

	UPROPERTY(config, EditAnywhere, Category="Profiles")
	FString DefaultProfileName = TEXT("Default");

	/** Asset the dashboard rebuilds from the canonical mirror; volumes use /Game/Prosetta/Volumes/<slug>/Story. */
	UPROPERTY(config, EditAnywhere, Category="Story")
	FString StoryAssetPath = TEXT("/Game/ProsettaSyncedStory");

	/**
	 * Actor the dashboard spawns when a level without a story source host is re-pointed at a volume.
	 * Must implement IProsettaStorySourceHost; defaults to Inkpot's AProsettaStorySourceActor. Point it at
	 * your project's level setup actor when that actor implements the interface.
	 */
	UPROPERTY(config, EditAnywhere, Category="Story", meta=(MustImplement="/Script/Inkpot.ProsettaStorySourceHost"))
	TSoftClassPtr<AActor> StorySourceHostClass;

	/** Absolute profiles directory. */
	FString ResolveProfilesDirectory() const;
};

/** A connection the user has used from the dashboard; never holds a token. */
USTRUCT()
struct PROSETTADASHBOARD_API FProsettaRecentConnection
{
	GENERATED_BODY()
	UPROPERTY(config) FString BaseUrl;
	UPROPERTY(config) FString ProjectId;
	UPROPERTY(config) FString ProjectName;
	UPROPERTY(config) FDateTime LastUsed;
	bool SameTarget(const FString& InBaseUrl, const FString& InProjectId) const { return BaseUrl == InBaseUrl && ProjectId == InProjectId; }
	FString Describe() const;
};

/** Per-user, per-project (Saved/Config/.../EditorPerProjectUserSettings.ini). */
UCLASS(config=EditorPerProjectUserSettings)
class PROSETTADASHBOARD_API UProsettaDashboardUserSettings : public UObject
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxRecentConnections = 8;

	UPROPERTY(config) FString ActiveProfileName;
	UPROPERTY(config) TArray<FProsettaRecentConnection> RecentConnections;
	UPROPERTY(config) bool bLogPanelExpanded = false;
	UPROPERTY(config) FString LastKnownCliVersion;

	/** Moves or inserts the connection at the front, capped; does not save. */
	void PushRecentConnection(const FString& BaseUrl, const FString& ProjectId, const FString& ProjectName, const FDateTime& Now = FDateTime::UtcNow());
	static UProsettaDashboardUserSettings* Get();
};
