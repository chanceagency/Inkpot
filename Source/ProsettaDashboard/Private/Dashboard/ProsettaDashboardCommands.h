#pragma once
#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/** Level-editor-wide shortcuts for the Prosetta Dashboard (Ctrl+Alt+P opens it). */
class PROSETTADASHBOARD_API FProsettaDashboardCommands : public TCommands<FProsettaDashboardCommands>
{
public:
	FProsettaDashboardCommands();
	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenDashboard;
	TSharedPtr<FUICommandInfo> ToggleWatch;
	TSharedPtr<FUICommandInfo> SyncNow;
	TSharedPtr<FUICommandInfo> SaveConfig;
};
