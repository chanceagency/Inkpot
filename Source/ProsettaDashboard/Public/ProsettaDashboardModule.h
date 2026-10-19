#pragma once

#include "Modules/ModuleManager.h"
#include "ProsettaStoryAssetBuild.h"

class FProsettaProjectWatch;
class FProsettaProfileStore;
struct FProsettaSyncProfile;
struct FProsettaWatchOptions;

/**
 * Editor module behind the Prosetta Dashboard tab (Tools > Prosetta Dashboard, the Prosetta toolbar
 * button, Ctrl+Alt+P). Owns the sync profiles, the editor-owned project watch and the profile's
 * PROSETTA_SYNC_BIN export. Project-specific behaviour is plugged in from the project's own editor module:
 * the presentation importer here, and the level's story source through IProsettaStorySourceHost.
 */
class PROSETTADASHBOARD_API FProsettaDashboardModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FProsettaDashboardModule& Get() { return FModuleManager::LoadModuleChecked<FProsettaDashboardModule>("ProsettaDashboard"); }
	static bool IsAvailable() { return FModuleManager::Get().IsModuleLoaded("ProsettaDashboard"); }

	/** The editor-owned project watch; null in commandlets and unattended runs. */
	FProsettaProjectWatch* GetProjectWatch() const { return ProjectWatch.Get(); }
	/** Named sync profiles under the project's profiles directory (Config/ProsettaSync/Profiles by default). */
	FProsettaProfileStore& GetProfileStore();
	/** The active profile (user setting, else the project default), or a default profile when it cannot be loaded. */
	FProsettaSyncProfile LoadActiveProfile() const;
	/** The watch options the active profile asks for. */
	FProsettaWatchOptions WatchOptionsFromProfile(const FProsettaSyncProfile& Profile) const;
	/** Re-applies profile-level environment (PROSETTA_SYNC_BIN) after the active profile changed. */
	void ApplyProfileEnvironment(const FProsettaSyncProfile& Profile);

	/** Installs the importer the dashboard's story rebuild runs for voice and portraits; unset rebuilds the story only. */
	void SetPresentationImporter(FProsettaPresentationImporter Importer) { PresentationImporter = MoveTemp(Importer); }
	const FProsettaPresentationImporter& GetPresentationImporter() const { return PresentationImporter; }

private:
	void HandlePostEngineInit();

	TUniquePtr<FProsettaProjectWatch> ProjectWatch;
	TUniquePtr<FProsettaProfileStore> ProfileStore;
	FProsettaPresentationImporter PresentationImporter;
	/** PROSETTA_SYNC_BIN as the editor was launched with it. */
	FString InheritedSyncBin;
	FDelegateHandle PostEngineInitHandle;
};
