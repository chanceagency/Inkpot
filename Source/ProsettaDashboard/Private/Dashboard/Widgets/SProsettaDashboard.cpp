#include "SProsettaDashboard.h"
#include "ProsettaWidgetHelpers.h"
#include "SProsettaChangesLog.h"
#include "SProsettaConnectionHeader.h"
#include "SProsettaCredentialsForm.h"
#include "SProsettaSettingsPanel.h"
#include "SProsettaStoryDebugPanel.h"
#include "SProsettaVolumesPanel.h"
#include "SPrimaryButton.h"
#include "SSimpleButton.h"
#include "SWarningOrErrorBox.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ProsettaDashboard"

void SProsettaDashboard::Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel)
{
	Model = InModel;
	ChildSlot
	[
		SAssignNew(Switcher, SWidgetSwitcher).Tag(TEXT("Prosetta.Switcher"))
		.WidgetIndex(this, &SProsettaDashboard::ActivePage)
		+ SWidgetSwitcher::Slot() [ MakeFirstRunPage() ]
		+ SWidgetSwitcher::Slot() [ MakeDashboardPage() ]
	];
	StateHandle = Model->OnStateChanged().AddSP(this, &SProsettaDashboard::HandleStateChanged);
}

SProsettaDashboard::~SProsettaDashboard()
{
	if (Model.IsValid()) { Model->OnStateChanged().Remove(StateHandle); }
}

int32 SProsettaDashboard::ActivePage() const { return Model->GetState().NeedsFirstRun() ? 0 : 1; }

bool SProsettaDashboard::HasCli() const { return Model->GetState().Credentials.bCliAvailable && Model->GetState().Connection != EProsettaDashboardConnection::NoCli; }
bool SProsettaDashboard::HasCredentials() const { return Model->GetState().Credentials.HasCredentials(); }
bool SProsettaDashboard::HasScope() const
{
	const FProsettaDashboardState& State = Model->GetState();
	return State.bHasManifest || !State.Profile.Volumes.IsEmpty() || State.bHasVolumes;
}

FText SProsettaDashboard::CliItemText() const
{
	const FProsettaDashboardState& State = Model->GetState();
	if (HasCli())
	{
		return FText::Format(LOCTEXT("CliFound", "prosetta-sync CLI found{0}"), State.CliVersion.IsEmpty() ? FText::GetEmpty() : FText::FromString(TEXT(" (") + State.CliVersion + TEXT(")")));
	}
	if (!State.CliSetupProblem.IsEmpty()) { return FText::FromString(State.CliSetupProblem); }
	return LOCTEXT("CliMissing", "prosetta-sync CLI not found: install it (pipx install prosetta-sync) or set the CLI override in the profile");
}

FText SProsettaDashboard::CredentialsItemText() const
{
	const FProsettaCredentialsSummary& Creds = Model->GetState().Credentials;
	if (Creds.HasCredentials()) { return FText::FromString(TEXT("Credentials: ") + Creds.Describe()); }
	return LOCTEXT("CredsMissing", "No credentials: Connect… writes .prosetta-sync/config.json (ignored; the token never enters the project)");
}

FText SProsettaDashboard::ScopeItemText() const
{
	const FProsettaDashboardState& State = Model->GetState();
	if (State.bHasManifest) { return FText::FromString(TEXT("Mirror synced: ") + State.Manifest.ScopeDetail.Describe()); }
	if (!State.Profile.Volumes.IsEmpty()) { return FText::FromString(TEXT("Scope: ") + State.Profile.ScopeString()); }
	if (State.bHasVolumes) { return LOCTEXT("ScopeFromVolumes", "Volumes listed; pick one or sync the whole project"); }
	return LOCTEXT("ScopeMissing", "No mirror yet: connect, then Sync once or Start watch");
}

FText SProsettaDashboard::ErrorBannerText() const
{
	const FProsettaDashboardState& State = Model->GetState();
	if (State.LastError.IsEmpty() || State.LastError == State.StatusText) { return FText::FromString(State.StatusText); }
	return FText::FromString(State.StatusText + TEXT("\n") + State.LastError);
}

TSharedRef<SWidget> SProsettaDashboard::MakeChecklistRow(const FName& Tag, TAttribute<bool> bDone, TAttribute<FText> Text)
{
	return SNew(SHorizontalBox).Tag(Tag)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 8, 0)
		[
			SNew(SImage).DesiredSizeOverride(FVector2D(16, 16))
			.Image_Lambda([bDone]() { return FAppStyle::GetBrush(bDone.Get() ? "Icons.Check" : "Icons.X"); })
			.ColorAndOpacity_Lambda([bDone]() { return bDone.Get() ? FSlateColor(FStyleColors::Success) : FSlateColor(FStyleColors::Error); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[ SNew(STextBlock).Text(Text).AutoWrapText(true) ];
}

void SProsettaDashboard::ConnectFromFirstRun()
{
	const FProsettaDashboardState& State = Model->GetState();
	if (!State.Credentials.HasCredentials() && HasCli())
	{
		if (FProsettaCredentialsDialog::Show(Model.ToSharedRef())) { return; }
	}
	Model->Connect();
}

TSharedRef<SWidget> SProsettaDashboard::MakeFirstRunPage()
{
	return SNew(SBox).Tag(TEXT("Prosetta.FirstRun")).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24)
	[
		SNew(SBox).MaxDesiredWidth(560.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[ SNew(STextBlock).Text(LOCTEXT("FirstRunTitle", "Set up Prosetta sync")).Font(FAppStyle::GetFontStyle("HeadingExtraSmall")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
			[ SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FStyleColors::AccentGray).Text(LOCTEXT("FirstRunIntro", "The dashboard streams Prosetta generations into Saved/Prosetta and rebuilds the story asset. Three things are needed first:")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4) [ MakeChecklistRow("Prosetta.Check.Cli", TAttribute<bool>(this, &SProsettaDashboard::HasCli), TAttribute<FText>(this, &SProsettaDashboard::CliItemText)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4) [ MakeChecklistRow("Prosetta.Check.Credentials", TAttribute<bool>(this, &SProsettaDashboard::HasCredentials), TAttribute<FText>(this, &SProsettaDashboard::CredentialsItemText)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4) [ MakeChecklistRow("Prosetta.Check.Scope", TAttribute<bool>(this, &SProsettaDashboard::HasScope), TAttribute<FText>(this, &SProsettaDashboard::ScopeItemText)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SNew(STextBlock).Tag(TEXT("Prosetta.FirstRunStatus")).AutoWrapText(true).ColorAndOpacity(FStyleColors::Warning)
				.Visibility_Lambda([this]() { return Model->GetState().LastError.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { return FText::FromString(Model->GetState().LastError); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SPrimaryButton).Tag(TEXT("Prosetta.FirstRunConnect")).Text(LOCTEXT("Connect", "Connect…"))
					.IsEnabled_Lambda([this]() { return !Model->GetState().IsBusy(); })
					.OnClicked_Lambda([this]() { ConnectFromFirstRun(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					SNew(SSimpleButton).Tag(TEXT("Prosetta.FirstRunRetry")).Text(LOCTEXT("Retry", "Check again")).Icon(FAppStyle::GetBrush("Icons.Refresh"))
					.IsEnabled_Lambda([this]() { return !Model->GetState().IsBusy(); })
					.OnClicked_Lambda([this]() { Model->Connect(); return FReply::Handled(); })
				]
			]
		]
	];
}

TSharedRef<SWidget> SProsettaDashboard::MakeDashboardPage()
{
	ChangesLog = SNew(SProsettaChangesLog, Model.ToSharedRef());
	return SNew(SVerticalBox).Tag(TEXT("Prosetta.Main"))
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
		[ SAssignNew(Header, SProsettaConnectionHeader, Model.ToSharedRef()) ]
		// PIE banner
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SWarningOrErrorBox).Tag(TEXT("Prosetta.Banner.PIE")).MessageStyle(EMessageStyle::Warning).AutoWrapText(true)
			.Visibility_Lambda([this]() { const FProsettaDashboardState& S = Model->GetState(); return S.bPIE && (S.PendingCount > 0 || S.bReloadPending) ? EVisibility::Visible : EVisibility::Collapsed; })
			.Message_Lambda([this]()
			{
				const FProsettaDashboardState& S = Model->GetState();
				return FText::Format(LOCTEXT("PIEBanner", "Play is running: {0} {0}|plural(one=generation,other=generations) pending; they are applied when Play stops."), FText::AsNumber(FMath::Max(S.PendingCount, S.bReloadPending ? 1 : 0)));
			})
		]
		// Error banner
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SWarningOrErrorBox).Tag(TEXT("Prosetta.Banner.Error")).MessageStyle(EMessageStyle::Error).AutoWrapText(true)
			.Visibility_Lambda([this]()
			{
				const EProsettaDashboardConnection C = Model->GetState().Connection;
				return C == EProsettaDashboardConnection::Error || C == EProsettaDashboardConnection::Blocked || C == EProsettaDashboardConnection::AuthFailed ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.Message(this, &SProsettaDashboard::ErrorBannerText)
			[
				SNew(SHyperlink).Tag(TEXT("Prosetta.OpenLog")).Text(LOCTEXT("OpenLog", "Show log"))
				.OnNavigate_Lambda([this]() { if (ChangesLog.IsValid()) { ChangesLog->Expand(); } })
			]
		]
		// External change banner
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SWarningOrErrorBox).Tag(TEXT("Prosetta.Banner.External")).MessageStyle(EMessageStyle::Warning).AutoWrapText(true)
			.Visibility_Lambda([this]() { return Model->GetState().bExternalChangePending ? EVisibility::Visible : EVisibility::Collapsed; })
			.Message(LOCTEXT("ExternalBanner", "The mirror changed outside the editor (terminal sync or watch). Apply it to rebuild the story asset."))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SPrimaryButton).Tag(TEXT("Prosetta.ApplyExternal")).Text(LOCTEXT("Apply", "Apply"))
					.IsEnabled_Lambda([this]() { return !Model->GetState().bPIE && !Model->GetState().IsBusy(); })
					.OnClicked_Lambda([this]() { Model->ApplyPending(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
				[
					SNew(SSimpleButton).Tag(TEXT("Prosetta.DismissExternal")).Text(LOCTEXT("Dismiss", "Dismiss"))
					.OnClicked_Lambda([this]() { Model->DismissExternalChange(); return FReply::Handled(); })
				]
			]
		]
		// Body
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 4)
		[
			SNew(SSplitter).Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.42f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("Brushes.Panel")).Padding(8)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot() [ SAssignNew(SettingsPanel, SProsettaSettingsPanel, Model.ToSharedRef()) ]
				]
			]
			+ SSplitter::Slot().Value(0.58f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 0, 0, 4)
				[
					SNew(SBorder).BorderImage(FAppStyle::GetBrush("Brushes.Panel")).Padding(8)
					[ SAssignNew(VolumesPanel, SProsettaVolumesPanel, Model.ToSharedRef()) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 0, 0)
				[ SAssignNew(StoryDebugPanel, SProsettaStoryDebugPanel, Model.ToSharedRef()) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
		[ ChangesLog.ToSharedRef() ];
}

void SProsettaDashboard::HandleStateChanged() {}

#undef LOCTEXT_NAMESPACE
