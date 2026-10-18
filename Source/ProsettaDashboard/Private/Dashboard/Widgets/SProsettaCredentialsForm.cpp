#include "SProsettaCredentialsForm.h"
#include "ProsettaDashboardModel.h"
#include "ProsettaWidgetHelpers.h"
#include "Dialog/SCustomDialog.h"
#include "Misc/Paths.h"
#include "SPrimaryButton.h"
#include "SSimpleButton.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ProsettaCredentialsForm"

void SProsettaCredentialsForm::Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel)
{
	Model = InModel;
	OnSubmitted = InArgs._OnSubmitted;
	const FProsettaDashboardState& State = Model->GetState();
	const FString InitialUrl = !State.Credentials.BaseUrl.IsEmpty() ? State.Credentials.BaseUrl : State.Profile.BaseUrl;
	const FString InitialProject = !State.Credentials.ProjectId.IsEmpty() ? State.Credentials.ProjectId : State.Profile.ProjectId;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("Intro", "Writes .prosetta-sync/config.json (ignored by git, mode 0600) through `prosetta.py init --token-stdin`. Nothing is sent to Prosetta except the whoami check that follows."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SGridPanel).FillColumn(1, 1.0f)
			+ SGridPanel::Slot(0, 0).Padding(0, 4).VAlign(VAlign_Center) [ SNew(STextBlock).Text(LOCTEXT("BaseUrl", "Base URL")) ]
			+ SGridPanel::Slot(1, 0).Padding(8, 4)
			[
				SAssignNew(BaseUrlBox, SEditableTextBox).Tag(TEXT("Prosetta.BaseUrl")).Text(FText::FromString(InitialUrl)).HintText(LOCTEXT("BaseUrlHint", "https://api.prosetta.dev"))
			]
			+ SGridPanel::Slot(0, 1).Padding(0, 4).VAlign(VAlign_Center) [ SNew(STextBlock).Text(LOCTEXT("ProjectId", "Project id")) ]
			+ SGridPanel::Slot(1, 1).Padding(8, 4)
			[
				SAssignNew(ProjectIdBox, SEditableTextBox).Tag(TEXT("Prosetta.ProjectId")).Text(FText::FromString(InitialProject))
			]
			+ SGridPanel::Slot(0, 2).Padding(0, 4).VAlign(VAlign_Center) [ SNew(STextBlock).Text(LOCTEXT("Token", "Project token")) ]
			+ SGridPanel::Slot(1, 2).Padding(8, 4)
			[
				SAssignNew(TokenBox, SEditableTextBox).Tag(TEXT("Prosetta.Token")).IsPassword(true).AllowContextMenu(false)
				.HintText(LOCTEXT("TokenHint", "pasted here only; stored in the ignored config"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.PresetLocal")).Text(LOCTEXT("LocalDev", "Local dev"))
				.ToolTipText(FText::FromString(LocalDevUrl))
				.OnClicked_Lambda([this]() { SetBaseUrl(LocalDevUrl); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.PresetRelay")).Text(LOCTEXT("StagingRelay", "Staging relay"))
				.ToolTipText(LOCTEXT("StagingRelayTip", "api.prosetta.dev through the loopback relay (prosetta.py relay)"))
				.OnClicked_Lambda([this]() { SetBaseUrl(StagingRelayUrl); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Tag(TEXT("Prosetta.PresetNote")).AutoWrapText(true).Text(this, &SProsettaCredentialsForm::PresetNote)
				.ColorAndOpacity(FStyleColors::AccentGray)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Tag(TEXT("Prosetta.CommandPreview")).AutoWrapText(true)
			.Font(FAppStyle::GetFontStyle("MonospacedText"))
			.Text_Lambda([this]() { return FText::FromString(CommandPreview()); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0).HAlign(HAlign_Right)
		[
			SNew(SPrimaryButton).Tag(TEXT("Prosetta.CredentialsSave")).Text(LOCTEXT("Save", "Save credentials"))
			.IsEnabled_Lambda([this]() { return IsValid(); })
			.OnClicked_Lambda([this]() { Submit(); return FReply::Handled(); })
		]
	];
}

bool SProsettaCredentialsForm::IsValidBaseUrl(const FString& Url)
{
	const FString Trimmed = Url.TrimStartAndEnd();
	return (Trimmed.StartsWith(TEXT("http://")) || Trimmed.StartsWith(TEXT("https://"))) && Trimmed.Len() > 8;
}

FString SProsettaCredentialsForm::RelayStatePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ProsettaSync"), TEXT("relay.json"));
}

bool SProsettaCredentialsForm::IsValid() const
{
	return IsValidBaseUrl(GetBaseUrl()) && !GetProjectId().IsEmpty() && HasToken();
}

FString SProsettaCredentialsForm::CommandPreview() const
{
	return FString::Printf(TEXT("prosetta.py init --base-url %s --project-id %s --token-stdin"),
		GetBaseUrl().IsEmpty() ? TEXT("<url>") : *GetBaseUrl(), GetProjectId().IsEmpty() ? TEXT("<id>") : *GetProjectId());
}

bool SProsettaCredentialsForm::Submit()
{
	if (!IsValid() || !Model.IsValid()) { return false; }
	FString Token = TokenBox->GetText().ToString();
	TokenBox->SetText(FText::GetEmpty());
	Model->WriteCredentials(GetBaseUrl(), GetProjectId(), MoveTemp(Token));
	OnSubmitted.ExecuteIfBound();
	return true;
}

FText SProsettaCredentialsForm::PresetNote() const
{
	if (GetBaseUrl() == StagingRelayUrl)
	{
		return FPaths::FileExists(RelayStatePath())
			? LOCTEXT("RelayUp", "Relay state found in Saved/ProsettaSync; the relay was started in a terminal.")
			: LOCTEXT("RelayDown", "No relay state yet: run prosetta.py relay in a terminal first.");
	}
	if (GetBaseUrl() == LocalDevUrl) { return LOCTEXT("LocalNote", "A Prosetta dev server on this machine; use its dev token."); }
	return FText::GetEmpty();
}

void SProsettaCredentialsForm::SetBaseUrl(const FString& Url) { BaseUrlBox->SetText(FText::FromString(Url)); }
void SProsettaCredentialsForm::SetProjectId(const FString& Id) { ProjectIdBox->SetText(FText::FromString(Id)); }
void SProsettaCredentialsForm::SetToken(const FString& Token) { TokenBox->SetText(FText::FromString(Token)); }
FString SProsettaCredentialsForm::GetBaseUrl() const { return BaseUrlBox->GetText().ToString().TrimStartAndEnd(); }
FString SProsettaCredentialsForm::GetProjectId() const { return ProjectIdBox->GetText().ToString().TrimStartAndEnd(); }
bool SProsettaCredentialsForm::HasToken() const { return !TokenBox->GetText().IsEmpty(); }

bool FProsettaCredentialsDialog::Show(TSharedRef<IProsettaDashboardModel> Model)
{
	if (!ProsettaWidgets::CanShowModal()) { return false; }
	TSharedPtr<SCustomDialog> Dialog;
	TSharedRef<SProsettaCredentialsForm> Form = SNew(SProsettaCredentialsForm, Model)
		.OnSubmitted_Lambda([&Dialog]() { if (Dialog.IsValid()) { Dialog->RequestDestroyWindow(); } });
	Dialog = SNew(SCustomDialog)
		.Title(NSLOCTEXT("ProsettaCredentialsForm", "Title", "Prosetta credentials"))
		.UseScrollBox(false)
		.Content() [ SNew(SBox).MinDesiredWidth(520.0f) [ Form ] ]
		.Buttons({ SCustomDialog::FButton(NSLOCTEXT("ProsettaCredentialsForm", "Cancel", "Cancel")) });
	Dialog->ShowModal();
	return true;
}

#undef LOCTEXT_NAMESPACE
