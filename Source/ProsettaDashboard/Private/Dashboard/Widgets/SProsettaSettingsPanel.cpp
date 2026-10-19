#include "SProsettaSettingsPanel.h"
#include "ProsettaWidgetHelpers.h"
#include "Dialog/SCustomDialog.h"
#include "IStructureDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SNegativeActionButton.h"
#include "SPrimaryButton.h"
#include "SSimpleButton.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ProsettaSettingsPanel"

void SProsettaSettingsPanel::Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel)
{
	Model = InModel;
	const auto State = [this]() -> const FProsettaDashboardState& { return Model->GetState(); };

	StructData = MakeShared<FStructOnScope>(FProsettaSyncProfile::StaticStruct());
	*EditedProfile() = State().Profile;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.bShowScrollBar = false;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.ColumnWidth = 0.45f;
	FStructureDetailsViewArgs StructArgs;
	DetailsView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, StructData);
	DetailsView->GetDetailsView()->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& PropertyAndParent)
	{
		const FName Name = PropertyAndParent.Property.GetFName();
		if (Name == GET_MEMBER_NAME_CHECKED(FProsettaSyncProfile, Mode) || Name == GET_MEMBER_NAME_CHECKED(FProsettaSyncProfile, Volumes)) { return false; }
		for (const FProperty* Parent : PropertyAndParent.ParentProperties)
		{
			if (Parent && Parent->GetFName() == GET_MEMBER_NAME_CHECKED(FProsettaSyncProfile, Volumes)) { return false; }
		}
		return true;
	}));
	DetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SProsettaSettingsPanel::HandleFinishedChangingProperties);

	RebuildProfileOptions();

	ChildSlot
	[
		SNew(SVerticalBox)
		// Profile row
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[ SNew(STextBlock).Text(LOCTEXT("Profile", "Profile")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SAssignNew(ProfileCombo, SComboBox<TSharedPtr<FString>>).Tag(TEXT("Prosetta.ProfileCombo"))
				.OptionsSource(&ProfileOptions)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString())); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
				{
					if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct) { return; }
					if (*Item == Model->GetState().Profile.Name) { return; }
					FString Error;
					if (!Model->LoadProfile(*Item, Error)) { ProsettaWidgets::Notify(FText::FromString(Error), true); }
				})
				[ SNew(STextBlock).Tag(TEXT("Prosetta.ProfileLabel")).Text(this, &SProsettaSettingsPanel::ProfileLabel) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.ProfileSave")).Icon(FAppStyle::GetBrush("Icons.Save")).ToolTipText(LOCTEXT("SaveTip", "Save the profile to Saved/Config/ProsettaSyncProfiles"))
				.IsEnabled_Lambda([State]() { return State().bProfileDirty; })
				.OnClicked_Lambda([this]() { FString Error; if (!Model->SaveProfile(Error)) { ProsettaWidgets::Notify(FText::FromString(Error), true); } return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.ProfileSaveAs")).Text(LOCTEXT("SaveAs", "Save as…"))
				.OnClicked_Lambda([this]() { PromptSaveAs(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.ProfileRevert")).Icon(FAppStyle::GetBrush("Icons.CircleArrowLeft")).ToolTipText(LOCTEXT("RevertTip", "Discard unsaved edits"))
				.IsEnabled_Lambda([State]() { return State().bProfileDirty; })
				.OnClicked_Lambda([this]() { Model->RevertProfile(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SNegativeActionButton).Tag(TEXT("Prosetta.ProfileDelete")).Icon(FAppStyle::GetBrush("Icons.Delete")).ToolTipText(LOCTEXT("DeleteTip", "Delete this profile"))
				.IsEnabled_Lambda([State]() { return State().ProfileNames.Num() > 1; })
				.OnClicked_Lambda([this]() { ConfirmDelete(); return FReply::Handled(); })
			]
		]
		// Mode
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
		[
			SNew(SSegmentedControl<EProsettaSyncMode>).Tag(TEXT("Prosetta.Mode"))
			.Value_Lambda([State]() { return State().Profile.Mode; })
			.OnValueChanged_Lambda([this](EProsettaSyncMode Mode) { SetMode(Mode); })
			+ SSegmentedControl<EProsettaSyncMode>::Slot(EProsettaSyncMode::Watch).Text(LOCTEXT("Watch", "Watch")).ToolTip(LOCTEXT("WatchTip", "Stream generations as they complete (not for a fixed release tag)"))
			+ SSegmentedControl<EProsettaSyncMode>::Slot(EProsettaSyncMode::SyncOnce).Text(LOCTEXT("SyncOnce", "Sync once")).ToolTip(LOCTEXT("SyncTip", "Pull the current content into the mirror"))
			+ SSegmentedControl<EProsettaSyncMode>::Slot(EProsettaSyncMode::Verify).Text(LOCTEXT("Verify", "Verify")).ToolTip(LOCTEXT("VerifyTip", "Check the mirror against its manifest"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Tag(TEXT("Prosetta.ModeHint")).AutoWrapText(true).ColorAndOpacity(FStyleColors::Warning)
			.Visibility_Lambda([this]() { return ModeHintText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			.Text_Lambda([this]() { return ModeHintText; })
		]
		// Scope
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SAssignNew(ScopeArea, SExpandableArea).Tag(TEXT("Prosetta.ScopeArea")).AreaTitle(LOCTEXT("Scope", "Scope")).InitiallyCollapsed(false)
			.Visibility_Lambda([State]() { return State().Profile.Mode == EProsettaSyncMode::Verify ? EVisibility::Collapsed : EVisibility::Visible; })
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 4)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([State]() { return State().Profile.Mode == EProsettaSyncMode::Watch ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0) [ SNew(STextBlock).Text(LOCTEXT("Channel", "Channel")) ]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SSegmentedControl<EProsettaWatchChannel>).Tag(TEXT("Prosetta.Channel"))
						.Value_Lambda([State]() { return State().Profile.Channel; })
						.OnValueChanged_Lambda([this](EProsettaWatchChannel Channel) { SetChannel(Channel); })
						+ SSegmentedControl<EProsettaWatchChannel>::Slot(EProsettaWatchChannel::Live).Text(LOCTEXT("Live", "live")).ToolTip(LOCTEXT("LiveTip", "Every accepted generation"))
						+ SSegmentedControl<EProsettaWatchChannel>::Slot(EProsettaWatchChannel::LatestRelease).Text(LOCTEXT("LatestRelease", "latest-release")).ToolTip(LOCTEXT("LatestTip", "Only when a release is published (one volume)"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
				[
					SNew(STextBlock).ColorAndOpacity(FStyleColors::AccentGray).AutoWrapText(true)
					.Text_Lambda([State]()
					{
						if (State().bHasVolumes) { return LOCTEXT("VolumesHint", "Tick the volumes to sync; a tag pins a release, blank follows live. Nothing ticked syncs the whole project."); }
						return LOCTEXT("VolumesOffline", "Volumes come from the project once connected; the profile's scope is shown meanwhile.");
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
				[ SAssignNew(VolumeRowsBox, SVerticalBox) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 4).HAlign(HAlign_Left)
				[
					SNew(SSimpleButton).Tag(TEXT("Prosetta.RefreshVolumes")).Text(LOCTEXT("RefreshVolumes", "Refresh volumes")).Icon(FAppStyle::GetBrush("Icons.Refresh"))
					.IsEnabled_Lambda([State]() { return !State().IsBusy() && State().Credentials.HasCredentials(); })
					.OnClicked_Lambda([this]() { Model->RefreshVolumes(); return FReply::Handled(); })
				]
			]
		]
		// Details
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Tag(TEXT("Prosetta.ProfileDetails")) [ DetailsView->GetWidget().ToSharedRef() ]
		]
		// Footer
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(STextBlock).Tag(TEXT("Prosetta.ProfileProblems")).AutoWrapText(true).ColorAndOpacity(FStyleColors::Error)
			.Visibility_Lambda([State]() { return State().ProfileProblems.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			.Text_Lambda([State]()
			{
				TArray<FString> Lines;
				for (const FText& Problem : State().ProfileProblems) { Lines.Add(Problem.ToString()); }
				return FText::FromString(FString::Join(Lines, TEXT("\n")));
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SPrimaryButton).Tag(TEXT("Prosetta.Run")).Text(this, &SProsettaSettingsPanel::RunLabel)
				.IsEnabled(this, &SProsettaSettingsPanel::CanRun)
				.OnClicked_Lambda([this]() { Model->RunProfile(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.Reimport")).Text(LOCTEXT("Reimport", "Reimport last generation")).Icon(FAppStyle::GetBrush("Icons.Import"))
				.ToolTipText(LOCTEXT("ReimportTip", "Rebuild the story asset (and presentation) from the mirror as it is now"))
				.IsEnabled_Lambda([State]() { return !State().IsBusy() && State().bHasManifest && !State().bPIE; })
				.OnClicked_Lambda([this]() { Model->ReimportLastGeneration(); return FReply::Handled(); })
			]
		]
	];

	RebuildVolumeRows();
	StateHandle = Model->OnStateChanged().AddSP(this, &SProsettaSettingsPanel::HandleStateChanged);
}

SProsettaSettingsPanel::~SProsettaSettingsPanel()
{
	if (Model.IsValid()) { Model->OnStateChanged().Remove(StateHandle); }
}

FProsettaSyncProfile* SProsettaSettingsPanel::EditedProfile() const
{
	return StructData.IsValid() ? reinterpret_cast<FProsettaSyncProfile*>(StructData->GetStructMemory()) : nullptr;
}

const FProsettaSyncProfile* SProsettaSettingsPanel::GetEditedProfile() const { return EditedProfile(); }

FText SProsettaSettingsPanel::ProfileLabel() const
{
	const FProsettaDashboardState& State = Model->GetState();
	return FText::FromString(State.bProfileDirty ? State.Profile.Name + TEXT(" •") : State.Profile.Name);
}

FText SProsettaSettingsPanel::RunLabel() const
{
	switch (Model->GetState().Profile.Mode)
	{
	case EProsettaSyncMode::Watch: return Model->GetState().bWatchRunning ? LOCTEXT("RestartWatch", "Restart watch") : LOCTEXT("RunWatch", "Start watch");
	case EProsettaSyncMode::SyncOnce: return LOCTEXT("RunSync", "Sync now");
	case EProsettaSyncMode::Verify:
	default: return LOCTEXT("RunVerify", "Verify");
	}
}

bool SProsettaSettingsPanel::CanRun() const
{
	const FProsettaDashboardState& State = Model->GetState();
	return !State.IsBusy() && State.ProfileProblems.IsEmpty() && State.Credentials.HasCredentials() && State.Connection != EProsettaDashboardConnection::NoCli;
}

void SProsettaSettingsPanel::SetMode(EProsettaSyncMode Mode)
{
	FProsettaSyncProfile Edited = Model->GetState().Profile;
	if (Mode == EProsettaSyncMode::Watch && Edited.IsFixedTag())
	{
		ModeHintText = LOCTEXT("FixedTagHint", "Watch follows live content; a scope pinned to a release tag cannot be watched. Clear the tag or use Sync once.");
		return;
	}
	ModeHintText = FText::GetEmpty();
	if (Edited.Mode == Mode) { return; }
	Edited.Mode = Mode;
	Model->SetProfile(Edited);
}

void SProsettaSettingsPanel::SetChannel(EProsettaWatchChannel Channel)
{
	FProsettaSyncProfile Edited = Model->GetState().Profile;
	if (Edited.Channel == Channel) { return; }
	Edited.Channel = Channel;
	if (Channel == EProsettaWatchChannel::LatestRelease && Edited.Volumes.Num() > 1) { Edited.Volumes.SetNum(1); }
	Model->SetProfile(Edited);
}

void SProsettaSettingsPanel::SetVolumeChecked(const FString& Slug, bool bChecked)
{
	FProsettaSyncProfile Edited = Model->GetState().Profile;
	const int32 Existing = Edited.Volumes.IndexOfByPredicate([&Slug](const FProsettaVolumeScope& Scope) { return Scope.Slug == Slug; });
	if (bChecked && Existing == INDEX_NONE)
	{
		if (Edited.Channel == EProsettaWatchChannel::LatestRelease && Edited.Mode == EProsettaSyncMode::Watch) { Edited.Volumes.Reset(); }
		Edited.Volumes.Add(FProsettaVolumeScope(Slug));
	}
	else if (!bChecked && Existing != INDEX_NONE) { Edited.Volumes.RemoveAt(Existing); }
	else { return; }
	Model->SetProfile(Edited);
}

void SProsettaSettingsPanel::SetVolumeTag(const FString& Slug, const FString& Tag)
{
	FProsettaSyncProfile Edited = Model->GetState().Profile;
	FProsettaVolumeScope* Scope = Edited.Volumes.FindByPredicate([&Slug](const FProsettaVolumeScope& S) { return S.Slug == Slug; });
	if (!Scope) { return; }
	const FString Clean = Tag.TrimStartAndEnd() == TEXT("latest") ? FString() : Tag.TrimStartAndEnd();
	if (Scope->Tag == Clean) { return; }
	Scope->Tag = Clean;
	Model->SetProfile(Edited);
}

bool SProsettaSettingsPanel::SaveProfileAs(const FString& NewName, FString& OutError)
{
	return Model->SaveProfileAs(NewName.TrimStartAndEnd(), OutError);
}

TMap<FString, bool> SProsettaSettingsPanel::GetVolumeRows() const
{
	TMap<FString, bool> Rows;
	const FProsettaSyncProfile& Profile = Model->GetState().Profile;
	for (const FString& Slug : DrawnVolumeSlugs)
	{
		Rows.Add(Slug, Profile.Volumes.ContainsByPredicate([&Slug](const FProsettaVolumeScope& S) { return S.Slug == Slug; }));
	}
	return Rows;
}

void SProsettaSettingsPanel::RebuildProfileOptions()
{
	ProfileOptions.Reset();
	for (const FString& Name : Model->GetState().ProfileNames) { ProfileOptions.Add(MakeShared<FString>(Name)); }
	if (ProfileOptions.IsEmpty()) { ProfileOptions.Add(MakeShared<FString>(Model->GetState().Profile.Name)); }
	if (ProfileCombo.IsValid()) { ProfileCombo->RefreshOptions(); }
}

void SProsettaSettingsPanel::RebuildVolumeRows()
{
	if (!VolumeRowsBox.IsValid()) { return; }
	const FProsettaDashboardState& State = Model->GetState();
	struct FRowSpec { FString Slug; FString Name; TArray<FString> Tags; };
	TArray<FRowSpec> Specs;
	if (State.bHasVolumes)
	{
		for (const FProsettaVolumeSummary& Volume : State.Volumes.Volumes) { Specs.Add({ Volume.Slug, Volume.Name, Volume.Tags }); }
	}
	for (const FProsettaVolumeScope& Scope : State.Profile.Volumes)
	{
		if (!Specs.ContainsByPredicate([&Scope](const FRowSpec& S) { return S.Slug == Scope.Slug; })) { Specs.Add({ Scope.Slug, FString(), {} }); }
	}
	FString Signature = State.Profile.ScopeString();
	for (const FRowSpec& Spec : Specs) { Signature += TEXT("|") + Spec.Slug + TEXT("/") + Spec.Name + TEXT("/") + FString::Join(Spec.Tags, TEXT(",")); }
	if (Signature == VolumeRowsSignature && VolumeRowsBox->NumSlots() > 0) { return; }
	VolumeRowsSignature = Signature;
	VolumeRowsBox->ClearChildren();
	DrawnVolumeSlugs.Reset();
	for (const FRowSpec& Spec : Specs)
	{
		DrawnVolumeSlugs.Add(Spec.Slug);
		const FString Slug = Spec.Slug;
		const FProsettaVolumeScope* InProfile = State.Profile.Volumes.FindByPredicate([&Slug](const FProsettaVolumeScope& S) { return S.Slug == Slug; });
		const FString Label = Spec.Name.IsEmpty() || Spec.Name == Spec.Slug ? Spec.Slug : FString::Printf(TEXT("%s (%s)"), *Spec.Name, *Spec.Slug);
		VolumeRowsBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox).Tag(*FString::Printf(TEXT("Prosetta.Scope.%s"), *Slug))
				.IsChecked(InProfile ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this, Slug](ECheckBoxState NewState) { SetVolumeChecked(Slug, NewState == ECheckBoxState::Checked); })
				[ SNew(STextBlock).Text(FText::FromString(Label)) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(110.0f)
				[
					SNew(SEditableTextBox).Tag(*FString::Printf(TEXT("Prosetta.ScopeTag.%s"), *Slug))
					.IsEnabled(InProfile != nullptr)
					.HintText(LOCTEXT("TagHint", "latest"))
					.ToolTipText(FText::FromString(Spec.Tags.IsEmpty() ? TEXT("Release tag (blank follows live)") : TEXT("Tags: ") + FString::Join(Spec.Tags, TEXT(", "))))
					.Text(FText::FromString(InProfile ? InProfile->Tag : FString()))
					.OnTextCommitted_Lambda([this, Slug](const FText& Text, ETextCommit::Type) { SetVolumeTag(Slug, Text.ToString()); })
				]
			]
		];
	}
	if (Specs.IsEmpty())
	{
		VolumeRowsBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Tag(TEXT("Prosetta.ScopeEmpty")).ColorAndOpacity(FStyleColors::AccentGray).Text(LOCTEXT("NoVolumes", "Whole project (no volumes listed yet)"))
		];
	}
}

void SProsettaSettingsPanel::SyncDetailsFromState()
{
	FProsettaSyncProfile* Edited = EditedProfile();
	if (!Edited || bApplyingEdit) { return; }
	const FProsettaSyncProfile& Current = Model->GetState().Profile;
	if (*Edited != Current)
	{
		*Edited = Current;
		if (DetailsView.IsValid()) { DetailsView->GetDetailsView()->ForceRefresh(); }
	}
}

void SProsettaSettingsPanel::HandleFinishedChangingProperties(const FPropertyChangedEvent& Event)
{
	FProsettaSyncProfile* Edited = EditedProfile();
	if (!Edited || *Edited == Model->GetState().Profile) { return; }
	bApplyingEdit = true;
	Model->SetProfile(*Edited);
	bApplyingEdit = false;
	SyncDetailsFromState();
}

void SProsettaSettingsPanel::HandleStateChanged()
{
	const FProsettaDashboardState& State = Model->GetState();
	if (!ModeHintText.IsEmpty() && !State.Profile.IsFixedTag()) { ModeHintText = FText::GetEmpty(); }
	TArray<FString> Names;
	for (const TSharedPtr<FString>& Option : ProfileOptions) { Names.Add(*Option); }
	if (Names != State.ProfileNames) { RebuildProfileOptions(); }
	RebuildVolumeRows();
	SyncDetailsFromState();
}

void SProsettaSettingsPanel::PromptSaveAs()
{
	if (!ProsettaWidgets::CanShowModal()) { return; }
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(LOCTEXT("SaveAsTitle", "Save profile as"))
		.Content()
		[
			SNew(SBox).MinDesiredWidth(360.0f)
			[
				SAssignNew(NameBox, SEditableTextBox).HintText(LOCTEXT("NameHint", "Profile name")).Text(FText::FromString(Model->GetState().Profile.Name))
			]
		]
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("SaveAsButton", "Save")).SetPrimary(true)
				.SetIsEnabled(TAttribute<bool>::CreateLambda([&NameBox]() { return NameBox.IsValid() && !NameBox->GetText().ToString().TrimStartAndEnd().IsEmpty(); })),
			SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel")) });
	if (Dialog->ShowModal() == 0 && NameBox.IsValid())
	{
		FString Error;
		if (!SaveProfileAs(NameBox->GetText().ToString(), Error)) { ProsettaWidgets::Notify(FText::FromString(Error), true); }
	}
}

void SProsettaSettingsPanel::ConfirmDelete()
{
	const FString Name = Model->GetState().Profile.Name;
	if (ProsettaWidgets::CanShowModal())
	{
		TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
			.Title(LOCTEXT("DeleteTitle", "Delete profile"))
			.Content() [ SNew(STextBlock).Text(FText::Format(LOCTEXT("DeleteBody", "Delete the profile '{0}'? The sync cache and mirror are not touched."), FText::FromString(Name))) ]
			.Buttons({ SCustomDialog::FButton(LOCTEXT("DeleteButton", "Delete")).SetPrimary(true), SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel")) });
		if (Dialog->ShowModal() != 0) { return; }
	}
	FString Error;
	if (!Model->DeleteProfile(Name, Error)) { ProsettaWidgets::Notify(FText::FromString(Error), true); }
}

#undef LOCTEXT_NAMESPACE
