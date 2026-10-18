#include "SProsettaStoryDebugPanel.h"
#include "ProsettaDashboardModel.h"
#include "SSimpleButton.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ProsettaStoryDebugPanel"

void SProsettaStoryDebugPanel::Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel)
{
	Model = InModel;
	ChildSlot
	[
		SNew(SBorder).BorderImage(FAppStyle::GetBrush("Brushes.Panel")).Padding(8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Tag(TEXT("Prosetta.StoryStatus")).Text(this, &SProsettaStoryDebugPanel::StoryText)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.OpenBlotter")).Text(LOCTEXT("Blotter", "Open Blotter"))
				.ToolTipText(LOCTEXT("BlotterTip", "Inkpot's story debug widget: variables, tags and the current path"))
				.OnClicked_Lambda([this]() { Model->OpenBlotter(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SSimpleButton).Tag(TEXT("Prosetta.OpenOutliner")).Text(LOCTEXT("Outliner", "Open Outliner"))
				.ToolTipText(LOCTEXT("OutlinerTip", "Inkpot's knot / stitch outliner"))
				.OnClicked_Lambda([this]() { Model->OpenOutliner(); return FReply::Handled(); })
			]
		]
	];
}

FText SProsettaStoryDebugPanel::StoryText() const
{
	const int32 Running = Model->GetState().RunningStories;
	if (Running <= 0) { return LOCTEXT("NoStory", "No story running (start Play)"); }
	return FText::Format(LOCTEXT("StoryFmt", "{0} {0}|plural(one=story,other=stories) running"), Running);
}

#undef LOCTEXT_NAMESPACE
