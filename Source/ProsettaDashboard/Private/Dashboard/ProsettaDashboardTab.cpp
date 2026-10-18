#include "ProsettaDashboardTab.h"
#include "ProsettaDashboardCommands.h"
#include "ProsettaDashboardSubsystem.h"
#include "ProsettaDashboardModel.h"
#include "Widgets/SProsettaDashboard.h"

#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ProsettaDashboard"

const FName FProsettaDashboardTab::TabId(TEXT("ProsettaDashboard"));
TSharedPtr<FUICommandList> FProsettaDashboardTab::Commands;
bool FProsettaDashboardTab::bRegistered = false;
bool FProsettaDashboardTab::bBoundToLevelEditor = false;

namespace
{
	TSharedPtr<FProsettaDashboardModel> DashboardModel()
	{
		UProsettaDashboardSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UProsettaDashboardSubsystem>() : nullptr;
		return Subsystem ? TSharedPtr<FProsettaDashboardModel>(Subsystem->GetModel()) : nullptr;
	}
}

void FProsettaDashboardTab::Register()
{
	if (bRegistered) { return; }
	bRegistered = true;
	FProsettaDashboardCommands::Register();
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateStatic(&FProsettaDashboardTab::Spawn))
		.SetDisplayName(LOCTEXT("TabTitle", "Prosetta Dashboard"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Connection, sync profile, volumes and recent Prosetta changes for this project."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"));
	BindCommands();
	AddToolbarButton();
}

void FProsettaDashboardTab::Unregister()
{
	if (!bRegistered) { return; }
	bRegistered = false;
	if (FSlateApplication::IsInitialized()) { FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId); }
	if (UToolMenus* Menus = UToolMenus::TryGet()) { Menus->UnregisterOwner(TEXT("ProsettaDashboard")); }
	// Appended command lists hold a weak reference; dropping ours is enough.
	Commands.Reset();
	bBoundToLevelEditor = false;
	FProsettaDashboardCommands::Unregister();
}

void FProsettaDashboardTab::Open()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TabId);
}

bool FProsettaDashboardTab::IsOpen()
{
	return FGlobalTabmanager::Get()->FindExistingLiveTab(TabId).IsValid();
}

void FProsettaDashboardTab::BindCommands()
{
	const FProsettaDashboardCommands& Cmd = FProsettaDashboardCommands::Get();
	Commands = MakeShared<FUICommandList>();
	Commands->MapAction(Cmd.OpenDashboard, FExecuteAction::CreateStatic(&FProsettaDashboardTab::Open));
	Commands->MapAction(Cmd.ToggleWatch, FExecuteAction::CreateLambda([]
	{
		if (TSharedPtr<FProsettaDashboardModel> Model = DashboardModel())
		{
			if (Model->GetState().bWatchRunning) { Model->StopWatch(); } else { Model->StartWatch(); }
		}
	}));
	Commands->MapAction(Cmd.SyncNow, FExecuteAction::CreateLambda([] { if (TSharedPtr<FProsettaDashboardModel> Model = DashboardModel()) { Model->SyncOnce(); } }));
	Commands->MapAction(Cmd.SaveConfig, FExecuteAction::CreateLambda([]
	{
		if (TSharedPtr<FProsettaDashboardModel> Model = DashboardModel()) { FString Error; Model->SaveProfile(Error); }
	}));
	BindToLevelEditor();
}

void FProsettaDashboardTab::BindToLevelEditor()
{
	// A Default-phase module starts before the editor loads LevelEditor; the module calls this again after engine init.
	if (bBoundToLevelEditor || !Commands.IsValid() || !FModuleManager::Get().IsModuleLoaded("LevelEditor")) { return; }
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.GetGlobalLevelEditorActions()->Append(Commands.ToSharedRef());
	bBoundToLevelEditor = true;
}

void FProsettaDashboardTab::AddToolbarButton()
{
	UToolMenus* Menus = UToolMenus::Get();
	if (!Menus) { return; }
	FToolMenuOwnerScoped OwnerScope(TEXT("ProsettaDashboard"));
	UToolMenu* Toolbar = Menus->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& Section = Toolbar->FindOrAddSection("Prosetta");
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"OpenProsettaDashboard",
		FToolUIActionChoice(FExecuteAction::CreateStatic(&FProsettaDashboardTab::Open)),
		LOCTEXT("ToolbarLabel", "Prosetta"),
		LOCTEXT("ToolbarTooltip", "Open the Prosetta Dashboard (Ctrl+Alt+P)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import")));
	UToolMenu* ToolsMenu = Menus->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection("Prosetta", LOCTEXT("ToolsSection", "Prosetta"));
	ToolsSection.AddMenuEntryWithCommandList(FProsettaDashboardCommands::Get().OpenDashboard, Commands);
}

TSharedRef<SDockTab> FProsettaDashboardTab::Spawn(const FSpawnTabArgs&)
{
	TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab).Label(LOCTEXT("TabTitle", "Prosetta Dashboard"));
	TSharedPtr<FProsettaDashboardModel> Model = DashboardModel();
	if (Model.IsValid())
	{
		Tab->SetContent(SNew(SProsettaDashboard, Model.ToSharedRef()));
		Model->Connect();
	}
	else
	{
		Tab->SetContent(SNew(STextBlock).Text(LOCTEXT("NoEditor", "The Prosetta Dashboard needs the level editor.")));
	}
	return Tab;
}

#undef LOCTEXT_NAMESPACE
