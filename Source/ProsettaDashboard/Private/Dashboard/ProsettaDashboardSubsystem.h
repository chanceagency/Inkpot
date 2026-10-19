#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Containers/Ticker.h"
#include "ProsettaDashboardModel.h"
#include "ProsettaDashboardSubsystem.generated.h"

class UProsettaGenerationAsset;

/**
 * Owns the dashboard model for the editor session: builds its real dependencies (CLI runner,
 * profile store, watch, importer, level re-point, story asset rebuild), polls editor state twice a
 * second, and watches Saved/Prosetta/manifest.json for changes made outside the editor.
 * Detection only: nothing is imported from the directory watcher callback; the user applies the
 * change from the dashboard or a toast, and only when the importer says it is safe.
 */
UCLASS()
class PROSETTADASHBOARD_API UProsettaDashboardSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The live model; created lazily so a commandlet never spawns processes. */
	TSharedRef<FProsettaDashboardModel> GetModel();
	bool HasModel() const { return Model.IsValid(); }
	/** Pure-function snapshot of what the ticker feeds the model, for tests and the debug menu. */
	FProsettaDashboardPollInput BuildPollInput() const;
	/** Tab ids last opened for the Inkpot blotter / outliner utility widgets, toggled on repeat. */
	void ToggleEditorUtilityWidget(const FSoftObjectPath& WidgetPath, FName& InOutTabId);
	void ShowToast(const FText& Message, EProsettaToastKind Kind);

	/**
	 * Points World's story source host at Slug@Tag as one undoable edit, spawning StorySourceHostClass
	 * when the level has no IProsettaStorySourceHost. "latest" clears the tag.
	 */
	static bool RePointWorld(UWorld* World, const FString& Slug, const FString& Tag, FString& OutError);
	/** The host class from Project Settings > Prosetta Sync, or AProsettaStorySourceActor. */
	static UClass* StorySourceHostClass();

	/** Set in tests to stop the manifest watcher and ticker from touching the model. */
	bool bSuspendPolling = false;

private:
	FProsettaDashboardModelDeps BuildDeps();
	bool Tick(float DeltaTime);
	void EnsureManifestWatcher();
	void HandleDirectoryChanged(const TArray<struct FFileChangeData>& Changes);
	void HandleGenerationAccepted(const UProsettaGenerationAsset& Generation);
	void HandlePreBeginPIE(bool bSimulating);
	void HandleEndPIE(bool bSimulating);
	void HandleMapOpened(const FString& Filename, bool bAsTemplate);
	bool RePointLevel(const FString& Slug, const FString& Tag, FString& OutError);
	bool RebuildStory(const FProsettaSyncProfile& Profile, const FString& Slug, bool bWithPresentation, FString& OutError);

	TSharedPtr<FProsettaDashboardModel> Model;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle GenerationHandle;
	FDelegateHandle PreBeginPIEHandle;
	FDelegateHandle EndPIEHandle;
	FDelegateHandle MapOpenedHandle;
	FDelegateHandle DirectoryWatchHandle;
	FString WatchedDirectory;
	double LastManifestChangeSeconds = 0.0;
	/** Last tick the dashboard's own CLI was running; manifest changes close to it are not external. */
	double LastOwnRunSeconds = -1000.0;
	bool bManifestChangeQueued = false;
	FName BlotterTabId;
	FName OutlinerTabId;
};
