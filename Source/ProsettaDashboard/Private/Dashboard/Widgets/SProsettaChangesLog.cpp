#include "SProsettaChangesLog.h"
#include "ProsettaWidgetHelpers.h"
#include "ProsettaDashboardSettings.h"
#include "Modules/ModuleManager.h"
#include "OutputLogCreationParams.h"
#include "OutputLogModule.h"
#include "SSimpleButton.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ProsettaChangesLog"

const FSlateBrush* SProsettaChangesLog::IconFor(EProsettaChangeKind Kind)
{
	switch (Kind)
	{
	case EProsettaChangeKind::Generation: return FAppStyle::GetBrush("Icons.Import");
	case EProsettaChangeKind::Sync: return FAppStyle::GetBrush("Icons.Refresh");
	case EProsettaChangeKind::Verify: return FAppStyle::GetBrush("Icons.Check");
	case EProsettaChangeKind::Error: return FAppStyle::GetBrush("Icons.Error");
	case EProsettaChangeKind::Blocked: return FAppStyle::GetBrush("Icons.Lock");
	case EProsettaChangeKind::ExternalChange: return FAppStyle::GetBrush("Icons.Warning");
	case EProsettaChangeKind::Info:
	default: return FAppStyle::GetBrush("Icons.Info");
	}
}

void SProsettaChangesLog::Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel)
{
	Model = InModel;
	UProsettaDashboardUserSettings* UserSettings = UProsettaDashboardUserSettings::Get();
	const bool bInitiallyExpanded = UserSettings ? UserSettings->bLogPanelExpanded : false;

	TSharedRef<SWidget> OutputLog = SNullWidget::NullWidget;
	// SOutputLog walks the global log history without a lock while other threads append to it; automation
	// runs log from background threads constantly, so tests leave the embedded log out.
	FOutputLogModule* OutputLogModule = GIsAutomationTesting ? nullptr : FModuleManager::LoadModulePtr<FOutputLogModule>("OutputLog");
	if (OutputLogModule)
	{
		FOutputLogCreationParams Params;
		Params.bCreateDockInLayoutButton = false;
		Params.SettingsMenuCreationFlags = EOutputLogSettingsMenuFlags::SkipClearOnPie | EOutputLogSettingsMenuFlags::SkipOpenSourceButton;
		Params.LoggingLimit = 2000;
		Params.AllowAsInitialLogCategory = FAllowLogCategoryCallback::CreateLambda([](const FName Category)
		{
			const FString Name = Category.ToString();
			return Name.StartsWith(TEXT("LogProsetta")) || Name.StartsWith(TEXT("LogInkpot")) || Name == TEXT("LogInkPlusPlus");
		});
		OutputLog = OutputLogModule->MakeOutputLogWidget(Params);
	}

	ChildSlot
	[
		SAssignNew(Area, SExpandableArea).Tag(TEXT("Prosetta.ChangesArea"))
		.InitiallyCollapsed(!bInitiallyExpanded)
		.MaxHeight(320.0f)
		.OnAreaExpansionChanged_Lambda([](bool bExpanded)
		{
			if (UProsettaDashboardUserSettings* Settings = UProsettaDashboardUserSettings::Get())
			{
				Settings->bLogPanelExpanded = bExpanded;
				Settings->SaveConfig();
			}
		})
		.HeaderContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[ SNew(STextBlock).Tag(TEXT("Prosetta.ChangesTitle")).Text(this, &SProsettaChangesLog::Title).Font(FAppStyle::GetFontStyle("BoldFont")) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.ChangesClear")).Text(LOCTEXT("Clear", "Clear"))
				.OnClicked_Lambda([this]() { Model->ClearChanges(); return FReply::Handled(); })
			]
		]
		.BodyContent()
		[
			SNew(SSplitter).Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.4f)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FProsettaChangeEntry>>).Tag(TEXT("Prosetta.ChangesList"))
				.ListItemsSource(&Rows)
				.OnGenerateRow(this, &SProsettaChangesLog::MakeRow)
				.SelectionMode(ESelectionMode::Single)
			]
			+ SSplitter::Slot().Value(0.6f)
			[ SNew(SBox).Tag(TEXT("Prosetta.OutputLog")) [ OutputLog ] ]
		]
	];

	Rebuild();
	StateHandle = Model->OnStateChanged().AddSP(this, &SProsettaChangesLog::HandleStateChanged);
}

SProsettaChangesLog::~SProsettaChangesLog()
{
	if (Model.IsValid()) { Model->OnStateChanged().Remove(StateHandle); }
}

void SProsettaChangesLog::Expand()
{
	if (Area.IsValid()) { Area->SetExpanded(true); }
}

bool SProsettaChangesLog::IsExpanded() const { return Area.IsValid() && Area->IsExpanded(); }

FText SProsettaChangesLog::Title() const
{
	return FText::Format(LOCTEXT("TitleFmt", "Recent changes ({0})"), FText::AsNumber(Model->GetState().Changes.Num()));
}

void SProsettaChangesLog::Rebuild()
{
	Rows.Reset();
	const TArray<FProsettaChangeEntry>& Changes = Model->GetState().Changes;
	for (int32 Index = Changes.Num() - 1; Index >= 0; --Index) { Rows.Add(MakeShared<FProsettaChangeEntry>(Changes[Index])); }
	if (ListView.IsValid()) { ListView->RequestListRefresh(); }
}

void SProsettaChangesLog::HandleStateChanged() { Rebuild(); }

TSharedRef<ITableRow> SProsettaChangesLog::MakeRow(TSharedPtr<FProsettaChangeEntry> Entry, const TSharedRef<STableViewBase>& Owner)
{
	const bool bProblem = Entry->Kind == EProsettaChangeKind::Error || Entry->Kind == EProsettaChangeKind::Blocked;
	return SNew(STableRow<TSharedPtr<FProsettaChangeEntry>>, Owner).Padding(FMargin(2, 2))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 6, 0)
		[ SNew(SImage).Image(IconFor(Entry->Kind)).DesiredSizeOverride(FVector2D(14, 14)).ColorAndOpacity(bProblem ? FSlateColor(FStyleColors::Error) : FSlateColor::UseForeground()) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 8, 0)
		[ SNew(STextBlock).Text(FText::FromString(ProsettaWidgets::LocalClock(Entry->Time))).ColorAndOpacity(FStyleColors::AccentGray) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[ SNew(STextBlock).Text(FText::FromString(Entry->Describe())).AutoWrapText(true).ToolTipText(FText::FromString(Entry->Message)) ]
	];
}

#undef LOCTEXT_NAMESPACE
