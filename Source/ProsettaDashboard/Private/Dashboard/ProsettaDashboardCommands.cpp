#include "ProsettaDashboardCommands.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ProsettaDashboard"

FProsettaDashboardCommands::FProsettaDashboardCommands()
	: TCommands<FProsettaDashboardCommands>(TEXT("ProsettaDashboard"), LOCTEXT("ContextDesc", "Prosetta Dashboard"), NAME_None, FAppStyle::GetAppStyleSetName())
{
}

void FProsettaDashboardCommands::RegisterCommands()
{
	UI_COMMAND(OpenDashboard, "Prosetta Dashboard", "Open the Prosetta Dashboard tab.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::P));
	UI_COMMAND(ToggleWatch, "Toggle Prosetta Watch", "Start or stop the editor-owned Prosetta watch.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SyncNow, "Prosetta Sync Now", "Run one Prosetta sync with the active profile.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SaveConfig, "Save Prosetta Profile", "Save the dashboard's edited sync profile.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
