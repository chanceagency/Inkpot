#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IProsettaDashboardModel;

/** Running-story count plus the Blotter / Outliner shortcuts. */
class PROSETTADASHBOARD_API SProsettaStoryDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaStoryDebugPanel) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel);
	FText StoryText() const;

private:
	TSharedPtr<IProsettaDashboardModel> Model;
};
