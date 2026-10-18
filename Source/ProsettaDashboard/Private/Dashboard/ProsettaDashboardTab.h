#pragma once
#include "CoreMinimal.h"

class FUICommandList;
class SDockTab;
class FSpawnTabArgs;

/** Registers the "Prosetta Dashboard" nomad tab (Tools menu, toolbar button, Ctrl+Alt+P). */
class PROSETTADASHBOARD_API FProsettaDashboardTab
{
public:
	static const FName TabId;
	static void Register();
	static void Unregister();
	static void Open();
	static bool IsOpen();
	/** Appends the dashboard commands (Ctrl+Alt+P) to the level editor once it is loaded; safe to call repeatedly. */
	static void BindToLevelEditor();

private:
	static TSharedRef<SDockTab> Spawn(const FSpawnTabArgs& Args);
	static void BindCommands();
	static void AddToolbarButton();
	static TSharedPtr<FUICommandList> Commands;
	static bool bRegistered;
	static bool bBoundToLevelEditor;
};
