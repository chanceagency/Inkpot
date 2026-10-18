#include "SProsettaConnectionHeader.h"
#include "ProsettaDashboardModel.h"
#include "SProsettaCredentialsForm.h"
#include "SProsettaStatusDot.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "SNegativeActionButton.h"
#include "SPositiveActionButton.h"
#include "SSimpleButton.h"
#include "SSimpleComboButton.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ProsettaConnectionHeader"

void SProsettaConnectionHeader::Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel)
{
	Model = InModel;
	const auto State = [this]() -> const FProsettaDashboardState& { return Model->GetState(); };

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(SProsettaStatusDot).Tag(TEXT("Prosetta.StatusDot"))
				.Connection_Lambda([State]() { return State().Connection; })
				.ToolTipText_Lambda([State]() { return FText::FromString(FString::Printf(TEXT("watch: %s\ntransport: %s"), *State().WatchStatus, *State().TransportStatus)); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Tag(TEXT("Prosetta.Status"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.Text_Lambda([State]() { return FText::FromString(State().StatusText); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
			[
				SNew(SHorizontalBox).Tag(TEXT("Prosetta.Busy"))
				.Visibility_Lambda([State]() { return State().IsBusy() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center) [ SNew(SCircularThrobber).Radius(7.0f) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[
					SNew(STextBlock).ColorAndOpacity(FStyleColors::AccentGray).Text_Lambda([State]() { return FText::FromString(State().Busy); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SPositiveActionButton).Tag(TEXT("Prosetta.StartWatch")).Text(LOCTEXT("StartWatch", "Start watch")).Icon(FAppStyle::GetBrush("Icons.Play"))
				.Visibility_Lambda([State]() { return State().bWatchRunning ? EVisibility::Collapsed : EVisibility::Visible; })
				.IsEnabled_Lambda([State]() { return !State().IsBusy() && State().Credentials.HasCredentials(); })
				.OnClicked_Lambda([this]() { Model->StartWatch(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SNegativeActionButton).Tag(TEXT("Prosetta.StopWatch")).Text(LOCTEXT("StopWatch", "Stop")).Icon(FAppStyle::GetBrush("Icons.X"))
				.Visibility_Lambda([State]() { return State().bWatchRunning ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([this]() { Model->StopWatch(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.Reconnect")).Icon(FAppStyle::GetBrush("Icons.Refresh"))
				.ToolTipText(LOCTEXT("ReconnectTip", "Re-read credentials, whoami and the mirror"))
				.IsEnabled_Lambda([State]() { return !State().IsBusy(); })
				.OnClicked_Lambda([this]() { Model->Connect(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SSimpleComboButton).Tag(TEXT("Prosetta.ConnectionMenu")).Text(LOCTEXT("Connection", "Connection")).HasDownArrow(true)
				.OnGetMenuContent(this, &SProsettaConnectionHeader::MakeConnectionMenu)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(20, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Tag(TEXT("Prosetta.ScopeLine")).ColorAndOpacity(FStyleColors::AccentGray).Text(this, &SProsettaConnectionHeader::ScopeLine)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 0, 0)
			[
				SNew(STextBlock).Tag(TEXT("Prosetta.DataAge")).Text(this, &SProsettaConnectionHeader::AgeText).ColorAndOpacity(this, &SProsettaConnectionHeader::AgeColor)
			]
		]
	];
}

FText SProsettaConnectionHeader::ScopeLine() const
{
	const FProsettaDashboardState& State = Model->GetState();
	TArray<FString> Parts;
	const FString Project = !State.Manifest.ProjectName.IsEmpty() ? State.Manifest.ProjectName : State.Credentials.ProjectName;
	Parts.Add(Project.IsEmpty() ? TEXT("no project") : Project);
	Parts.Add(State.bHasManifest ? State.Manifest.ScopeDetail.Describe() : (State.Profile.ScopeString().IsEmpty() ? TEXT("live") : State.Profile.ScopeString()));
	if (State.Profile.Mode == EProsettaSyncMode::Watch) { Parts.Add(State.Profile.ChannelName()); }
	if (State.bHasManifest)
	{
		const FString Sha = State.Manifest.ContentSha();
		if (!Sha.IsEmpty()) { Parts.Add(Sha.Left(12)); }
	}
	return FText::FromString(FString::Join(Parts, TEXT(" · ")));
}

FText SProsettaConnectionHeader::AgeText() const
{
	const FProsettaDashboardState& State = Model->GetState();
	if (!State.bHasManifest) { return LOCTEXT("NoMirror", "no mirror yet"); }
	return FText::Format(LOCTEXT("AgeFmt", "synced {0}"), FText::FromString(State.DataAge));
}

FSlateColor SProsettaConnectionHeader::AgeColor() const
{
	const FProsettaDashboardState& State = Model->GetState();
	return (State.DataAgeMinutes == INDEX_NONE || State.DataAgeMinutes > 30) ? FStyleColors::Warning : FStyleColors::AccentGray;
}

TSharedRef<SWidget> SProsettaConnectionHeader::MakeConnectionMenu()
{
	const FProsettaDashboardState& State = Model->GetState();
	FMenuBuilder Menu(true, nullptr);
	Menu.BeginSection("ProsettaConnectionInfo", LOCTEXT("Current", "Current connection"));
	Menu.AddMenuEntry(FText::FromString(State.Credentials.HasCredentials() ? State.Credentials.Describe() : TEXT("No credentials found")),
		FText::FromString(State.Credentials.LocalConfigPath), FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	Menu.EndSection();
	Menu.BeginSection("ProsettaConnectionActions");
	if (State.RecentConnections.Num() > 0)
	{
		TWeakPtr<IProsettaDashboardModel> WeakModel = Model;
		Menu.AddSubMenu(LOCTEXT("Recent", "Recent connections"), LOCTEXT("RecentTip", "Switch the profile's base URL and project id; the token stays where it is"),
			FNewMenuDelegate::CreateLambda([WeakModel](FMenuBuilder& Sub)
			{
				const TSharedPtr<IProsettaDashboardModel> Pinned = WeakModel.Pin();
				if (!Pinned.IsValid()) { return; }
				const TArray<FProsettaRecentConnection>& Recent = Pinned->GetState().RecentConnections;
				for (int32 Index = 0; Index < Recent.Num(); ++Index)
				{
					Sub.AddMenuEntry(FText::FromString(Recent[Index].Describe()), FText::FromString(Recent[Index].BaseUrl), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakModel, Index]() { if (const TSharedPtr<IProsettaDashboardModel> M = WeakModel.Pin()) { M->UseRecentConnection(Index); } })));
				}
			}));
	}
	Menu.AddMenuEntry(LOCTEXT("Update", "Update credentials…"), LOCTEXT("UpdateTip", "Write a new token to the ignored local config"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(FExecuteAction::CreateLambda([this]() { FProsettaCredentialsDialog::Show(Model.ToSharedRef()); })));
	const FString ConfigDir = FPaths::GetPath(State.Credentials.LocalConfigPath);
	Menu.AddMenuEntry(LOCTEXT("OpenFolder", "Open config folder"), FText::FromString(ConfigDir), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
		FUIAction(FExecuteAction::CreateLambda([ConfigDir]() { FPlatformProcess::ExploreFolder(*ConfigDir); }),
			FCanExecuteAction::CreateLambda([ConfigDir]() { return !ConfigDir.IsEmpty() && FPaths::DirectoryExists(ConfigDir); })));
	Menu.AddMenuEntry(LOCTEXT("WhoAmI", "Refresh whoami"), FText::GetEmpty(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
		FUIAction(FExecuteAction::CreateLambda([this]() { Model->RefreshWhoAmI(); })));
	Menu.EndSection();
	return Menu.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
