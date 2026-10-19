#include "SProsettaVolumesPanel.h"
#include "ProsettaWidgetHelpers.h"
#include "SProsettaStatusDot.h"
#include "Dialog/SCustomDialog.h"
#include "Editor.h"
#include "Inkpot/Prosetta/ProsettaSyncInspector.h"
#include "SSimpleButton.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "ProsettaVolumesPanel"

namespace
{
	const FName ColDefault("Default");
	const FName ColName("Name");
	const FName ColStatus("Status");
	const FName ColErrors("Errors");
	const FName ColWarnings("Warnings");
	const FName ColTags("Tags");
	const FName ColCompiled("Compiled");
	const FName ColBadges("Badges");
	const FName ColUse("Use");

	class SVolumeRow : public SMultiColumnTableRow<TSharedPtr<FProsettaVolumeRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SVolumeRow) {}
			SLATE_EVENT(FSimpleDelegate, OnUse)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& Owner, TSharedPtr<FProsettaVolumeRow> InRow)
		{
			Row = InRow;
			OnUse = InArgs._OnUse;
			SMultiColumnTableRow<TSharedPtr<FProsettaVolumeRow>>::Construct(FSuperRowType::FArguments().Padding(FMargin(0, 2)), Owner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			const FProsettaVolumeSummary& V = Row->Volume;
			if (Column == ColDefault)
			{
				return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Star")).Visibility(V.bIsDefault ? EVisibility::Visible : EVisibility::Hidden).ToolTipText(LOCTEXT("DefaultTip", "Default volume")) ];
			}
			if (Column == ColName)
			{
				return SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight() [ SNew(STextBlock).Text(FText::FromString(V.Name.IsEmpty() ? V.Slug : V.Name)).Font(FAppStyle::GetFontStyle("BoldFont")) ]
					+ SVerticalBox::Slot().AutoHeight() [ SNew(STextBlock).Text(FText::FromString(V.Slug)).ColorAndOpacity(FStyleColors::AccentGray) ];
			}
			if (Column == ColStatus)
			{
				const FSlateColor Color = V.ErrorCount > 0 ? FSlateColor(FStyleColors::Error)
					: V.WarningCount > 0 ? FSlateColor(FStyleColors::Warning)
					: (V.Status == TEXT("compiled") || V.Status == TEXT("ok") || V.Status == TEXT("ready")) ? FSlateColor(FStyleColors::Success)
					: FSlateColor(FStyleColors::AccentGray);
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[ SNew(SImage).Image(FAppStyle::GetBrush("Icons.FilledCircle")).DesiredSizeOverride(FVector2D(10, 10)).ColorAndOpacity(Color) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center) [ SNew(STextBlock).Text(FText::FromString(V.Status)) ];
			}
			if (Column == ColErrors) { return SNew(STextBlock).Text(FText::AsNumber(V.ErrorCount)).ColorAndOpacity(V.ErrorCount > 0 ? FSlateColor(FStyleColors::Error) : FSlateColor::UseForeground()); }
			if (Column == ColWarnings) { return SNew(STextBlock).Text(FText::AsNumber(V.WarningCount)).ColorAndOpacity(V.WarningCount > 0 ? FSlateColor(FStyleColors::Warning) : FSlateColor::UseForeground()); }
			if (Column == ColTags) { return SNew(STextBlock).Text(FText::FromString(FString::Join(V.Tags, TEXT(", ")))).AutoWrapText(true); }
			if (Column == ColCompiled)
			{
				return SNew(STextBlock).Text(FText::FromString(V.bHasLastCompiledAt ? UProsettaSyncInspector::DescribeAge(V.LastCompiledAt, FDateTime::UtcNow()) : TEXT("never")))
					.ToolTipText(FText::FromString(V.bHasLastCompiledAt ? V.LastCompiledAt.ToIso8601() : FString()));
			}
			if (Column == ColBadges) { return SNew(STextBlock).Text(FText::FromString(Row->Badges())).ColorAndOpacity(FStyleColors::AccentGray); }
			if (Column == ColUse)
			{
				return SNew(SButton).Tag(*FString::Printf(TEXT("Prosetta.Use.%s"), *V.Slug)).Text(LOCTEXT("Use", "Use…")).ToolTipText(LOCTEXT("UseTip", "Switch the sync scope to this volume"))
					.OnClicked_Lambda([this]() { OnUse.ExecuteIfBound(); return FReply::Handled(); });
			}
			return SNullWidget::NullWidget;
		}

	private:
		TSharedPtr<FProsettaVolumeRow> Row;
		FSimpleDelegate OnUse;
	};
}

FString FProsettaVolumeRow::Badges() const
{
	TArray<FString> Parts;
	if (bMirrored) { Parts.Add(MirroredTag.IsEmpty() ? TEXT("mirrored") : FString::Printf(TEXT("mirrored@%s"), *MirroredTag)); }
	if (bCanonical) { Parts.Add(TEXT("canonical")); }
	if (bInProfile) { Parts.Add(TEXT("profile")); }
	return FString::Join(Parts, TEXT(" · "));
}

// ---------------------------------------------------------------- switch dialog

bool FProsettaVolumeSwitchDialog::SBody::CanRepoint()
{
	return GEditor != nullptr && GEditor->GetEditorWorldContext(false).World() != nullptr;
}

FText FProsettaVolumeSwitchDialog::SBody::Explanation(const FProsettaVolumeSummary& Volume, bool bPIE)
{
	FString Text = FString::Printf(TEXT("This runs `prosetta.py config set volumes %s[@tag]` for the active profile and starts a sync.\n\n"
		"• The cache key changes, so the whole volume is pulled again.\n"
		"• The mirror under Saved/Prosetta is re-synced; the story asset is rebuilt on accept.\n"
		"• A running watch is restarted on the new scope.\n"
		"• Nothing is written to Prosetta."), *Volume.Slug);
	if (bPIE) { Text += TEXT("\n\nPlay is running: the swap happens after Play stops."); }
	return FText::FromString(Text);
}

void FProsettaVolumeSwitchDialog::SBody::Construct(const FArguments& InArgs, const FProsettaVolumeSummary& Volume, bool bPIE)
{
	TagOptions.Add(MakeShared<FString>(TEXT("latest")));
	for (const FString& Tag : Volume.Tags) { TagOptions.Add(MakeShared<FString>(Tag)); }
	SelectedTag = TagOptions[0];
	bCanRepoint = CanRepoint();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[ SNew(STextBlock).Tag(TEXT("Prosetta.SwitchExplanation")).AutoWrapText(true).Text(Explanation(Volume, bPIE)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0) [ SNew(STextBlock).Text(LOCTEXT("Tag", "Release tag")) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SComboBox<TSharedPtr<FString>>).Tag(TEXT("Prosetta.SwitchTag")).OptionsSource(&TagOptions).InitiallySelectedItem(SelectedTag)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) { if (Item.IsValid()) { SelectedTag = Item; } })
				[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedTag.IsValid() ? *SelectedTag : TEXT("latest")); }) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SCheckBox).Tag(TEXT("Prosetta.SwitchRepoint")).IsEnabled(bCanRepoint)
			.IsChecked_Lambda([this]() { return bRepointChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bRepointChecked = State == ECheckBoxState::Checked; })
			.ToolTipText(bCanRepoint ? LOCTEXT("RepointTip", "Sets the level's Prosetta story source host (undoable)") : LOCTEXT("RepointNoWorld", "No level is open"))
			[ SNew(STextBlock).Text(LOCTEXT("Repoint", "Also re-point the open level at this volume's story")) ]
		]
	];
}

FString FProsettaVolumeSwitchDialog::SBody::GetSelectedTag() const
{
	return SelectedTag.IsValid() && *SelectedTag != TEXT("latest") ? *SelectedTag : FString();
}

bool FProsettaVolumeSwitchDialog::SBody::GetRepoint() const { return bRepointChecked && bCanRepoint; }

void FProsettaVolumeSwitchDialog::SBody::SetSelectedTag(const FString& Tag)
{
	const FString Wanted = Tag.IsEmpty() ? TEXT("latest") : Tag;
	for (const TSharedPtr<FString>& Option : TagOptions) { if (*Option == Wanted) { SelectedTag = Option; return; } }
	TagOptions.Add(MakeShared<FString>(Wanted));
	SelectedTag = TagOptions.Last();
}

void FProsettaVolumeSwitchDialog::SBody::SetRepoint(bool bRepoint) { bRepointChecked = bRepoint; }

bool FProsettaVolumeSwitchDialog::Perform(TSharedRef<IProsettaDashboardModel> Model, const FString& Slug, const FString& Tag, bool bRepoint, bool bPIE)
{
	if (bPIE && GEditor)
	{
		TWeakPtr<IProsettaDashboardModel> WeakModel = Model;
		TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
		// ShutdownPIE, not EndPIE: EndPIE is broadcast while GEditor->PlayWorld is still set, so SwitchVolume would refuse.
		*Handle = FEditorDelegates::ShutdownPIE.AddLambda([WeakModel, Slug, Tag, bRepoint, Handle](bool)
		{
			// Removing this binding destroys the lambda and its captures, so copy what the switch needs first.
			const TSharedPtr<IProsettaDashboardModel> Pinned = WeakModel.Pin();
			const FString SwitchSlug = Slug;
			const FString SwitchTag = Tag;
			const bool bSwitchRepoint = bRepoint;
			FEditorDelegates::ShutdownPIE.Remove(*Handle);
			if (Pinned)
			{
				FString Error;
				if (!Pinned->SwitchVolume(SwitchSlug, SwitchTag, bSwitchRepoint, Error)) { ProsettaWidgets::Notify(FText::FromString(Error), true); }
			}
		});
		GEditor->RequestEndPlayMap();
		return true;
	}
	FString Error;
	if (!Model->SwitchVolume(Slug, Tag, bRepoint, Error))
	{
		ProsettaWidgets::Notify(FText::FromString(Error), true);
		return false;
	}
	return true;
}

bool FProsettaVolumeSwitchDialog::Show(TSharedRef<IProsettaDashboardModel> Model, const FProsettaVolumeSummary& Volume)
{
	if (!ProsettaWidgets::CanShowModal()) { return false; }
	const bool bPIE = Model->GetState().bPIE;
	TSharedRef<SBody> Body = SNew(SBody, Volume, bPIE);
	TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(FText::Format(LOCTEXT("SwitchTitle", "Switch to volume {0}"), FText::FromString(Volume.Name.IsEmpty() ? Volume.Slug : Volume.Name)))
		.UseScrollBox(false)
		.Content() [ SNew(SBox).MinDesiredWidth(480.0f) [ Body ] ]
		.Buttons({
			SCustomDialog::FButton(bPIE ? LOCTEXT("StopAndSwap", "Stop Play and swap") : LOCTEXT("Switch", "Switch volume")).SetPrimary(true),
			SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel")) });
	if (Dialog->ShowModal() == 0)
	{
		Perform(Model, Volume.Slug, Body->GetSelectedTag(), Body->GetRepoint(), bPIE);
	}
	return true;
}

// ---------------------------------------------------------------- panel

TArray<TSharedPtr<FProsettaVolumeRow>> SProsettaVolumesPanel::BuildRows(const FProsettaDashboardState& State)
{
	TArray<TSharedPtr<FProsettaVolumeRow>> Rows;
	TArray<FProsettaVolumeSummary> Volumes;
	if (State.bHasVolumes) { Volumes = State.Volumes.Volumes; }
	else if (State.bHasManifest)
	{
		for (const FProsettaScopeVolumeEntry& Entry : State.Manifest.ScopeDetail.Volumes)
		{
			FProsettaVolumeSummary Stub;
			Stub.Slug = Entry.Slug;
			Stub.Status = TEXT("mirrored");
			if (!Entry.Tag.IsEmpty()) { Stub.Tags.Add(Entry.Tag); }
			Volumes.Add(Stub);
		}
	}
	const bool bSingleScope = State.bHasManifest && State.Manifest.ScopeDetail.Volumes.Num() == 1;
	for (const FProsettaVolumeSummary& Volume : Volumes)
	{
		TSharedPtr<FProsettaVolumeRow> Row = MakeShared<FProsettaVolumeRow>();
		Row->Volume = Volume;
		if (State.bHasManifest)
		{
			if (const FProsettaScopeVolumeEntry* Entry = State.Manifest.ScopeDetail.FindVolume(Volume.Slug))
			{
				Row->bMirrored = true;
				Row->MirroredTag = Entry->Tag;
				Row->bCanonical = bSingleScope;
			}
		}
		Row->bInProfile = State.Profile.Volumes.ContainsByPredicate([&Volume](const FProsettaVolumeScope& S) { return S.Slug == Volume.Slug; });
		Rows.Add(Row);
	}
	Rows.Sort([](const TSharedPtr<FProsettaVolumeRow>& A, const TSharedPtr<FProsettaVolumeRow>& B)
	{
		if (A->Volume.bIsDefault != B->Volume.bIsDefault) { return A->Volume.bIsDefault; }
		return A->Volume.Slug < B->Volume.Slug;
	});
	return Rows;
}

bool SProsettaVolumesPanel::MatchesFilter(const FProsettaVolumeRow& Row, const FString& InFilter)
{
	if (InFilter.IsEmpty()) { return true; }
	if (Row.Volume.Slug.Contains(InFilter) || Row.Volume.Name.Contains(InFilter)) { return true; }
	for (const FString& Tag : Row.Volume.Tags) { if (Tag.Contains(InFilter)) { return true; } }
	return false;
}

void SProsettaVolumesPanel::Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel)
{
	Model = InModel;
	const auto State = [this]() -> const FProsettaDashboardState& { return Model->GetState(); };

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[ SNew(STextBlock).Text(LOCTEXT("Volumes", "Volumes")).Font(FAppStyle::GetFontStyle("BoldFont")) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SAssignNew(SearchBox, SSearchBox).Tag(TEXT("Prosetta.VolumeSearch")).HintText(LOCTEXT("Search", "Filter by slug, name or tag"))
				.OnTextChanged_Lambda([this](const FText& Text) { SetFilter(Text.ToString()); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.VolumesRefresh")).Icon(FAppStyle::GetBrush("Icons.Refresh")).ToolTipText(LOCTEXT("RefreshTip", "List the project's volumes again"))
				.IsEnabled_Lambda([State]() { return !State().IsBusy() && State().Credentials.HasCredentials(); })
				.OnClicked_Lambda([this]() { Model->RefreshVolumes(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
		[
			SNew(STextBlock).Tag(TEXT("Prosetta.VolumesEmpty")).ColorAndOpacity(FStyleColors::AccentGray).AutoWrapText(true)
			.Visibility_Lambda([this]() { return AllRows.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text_Lambda([State]()
			{
				if (!State().Credentials.HasCredentials()) { return LOCTEXT("EmptyNoCreds", "Connect to list the project's volumes."); }
				if (State().IsBusy()) { return LOCTEXT("EmptyBusy", "Listing volumes…"); }
				return LOCTEXT("EmptyNone", "No volumes listed. The project is single-volume or the listing has not run; use Refresh.");
			})
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(ListView, SListView<TSharedPtr<FProsettaVolumeRow>>).Tag(TEXT("Prosetta.VolumesList"))
			.ListItemsSource(&FilteredRows)
			.OnGenerateRow(this, &SProsettaVolumesPanel::MakeRow)
			.SelectionMode(ESelectionMode::None)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(ColDefault).DefaultLabel(FText::FromString(TEXT("★"))).FixedWidth(24.0f).HAlignHeader(HAlign_Center)
				+ SHeaderRow::Column(ColName).DefaultLabel(LOCTEXT("ColName", "Volume")).FillWidth(0.28f)
				+ SHeaderRow::Column(ColStatus).DefaultLabel(LOCTEXT("ColStatus", "Status")).FillWidth(0.14f)
				+ SHeaderRow::Column(ColErrors).DefaultLabel(LOCTEXT("ColErrors", "Err")).FixedWidth(36.0f)
				+ SHeaderRow::Column(ColWarnings).DefaultLabel(LOCTEXT("ColWarnings", "Warn")).FixedWidth(40.0f)
				+ SHeaderRow::Column(ColTags).DefaultLabel(LOCTEXT("ColTags", "Tags")).FillWidth(0.16f)
				+ SHeaderRow::Column(ColCompiled).DefaultLabel(LOCTEXT("ColCompiled", "Compiled")).FillWidth(0.14f)
				+ SHeaderRow::Column(ColBadges).DefaultLabel(LOCTEXT("ColBadges", "In use")).FillWidth(0.2f)
				+ SHeaderRow::Column(ColUse).DefaultLabel(FText::GetEmpty()).FixedWidth(60.0f))
		]
	];

	Rebuild();
	StateHandle = Model->OnStateChanged().AddSP(this, &SProsettaVolumesPanel::HandleStateChanged);
}

SProsettaVolumesPanel::~SProsettaVolumesPanel()
{
	if (Model.IsValid()) { Model->OnStateChanged().Remove(StateHandle); }
}

void SProsettaVolumesPanel::SetFilter(const FString& Text)
{
	Filter = Text.TrimStartAndEnd();
	FilteredRows.Reset();
	for (const TSharedPtr<FProsettaVolumeRow>& Row : AllRows) { if (MatchesFilter(*Row, Filter)) { FilteredRows.Add(Row); } }
	if (ListView.IsValid()) { ListView->RequestListRefresh(); }
}

void SProsettaVolumesPanel::Rebuild()
{
	AllRows = BuildRows(Model->GetState());
	SetFilter(Filter);
}

void SProsettaVolumesPanel::HandleStateChanged() { Rebuild(); }

TSharedRef<ITableRow> SProsettaVolumesPanel::MakeRow(TSharedPtr<FProsettaVolumeRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	const FProsettaVolumeSummary Volume = Row->Volume;
	TWeakPtr<SProsettaVolumesPanel> WeakSelf = SharedThis(this);
	return SNew(SVolumeRow, Owner, Row).OnUse(FSimpleDelegate::CreateLambda([WeakSelf, Volume]()
	{
		if (TSharedPtr<SProsettaVolumesPanel> Self = WeakSelf.Pin()) { Self->UseVolume(Volume); }
	}));
}

void SProsettaVolumesPanel::UseVolume(const FProsettaVolumeSummary& Volume)
{
	if (!FProsettaVolumeSwitchDialog::Show(Model.ToSharedRef(), Volume))
	{
		// Headless: switch directly, live tag, no re-point.
		FProsettaVolumeSwitchDialog::Perform(Model.ToSharedRef(), Volume.Slug, FString(), false, Model->GetState().bPIE);
	}
}

#undef LOCTEXT_NAMESPACE
