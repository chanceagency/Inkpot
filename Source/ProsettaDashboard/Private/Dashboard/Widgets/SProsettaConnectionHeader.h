#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IProsettaDashboardModel;

/** Status dot, headline, scope line, data age, watch / reconnect buttons and the Connection menu. */
class PROSETTADASHBOARD_API SProsettaConnectionHeader : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaConnectionHeader) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel);

	/** "project · scope · channel · sha12" from the manifest and the profile. */
	FText ScopeLine() const;
	FText AgeText() const;
	/** Amber once the mirror is older than 30 minutes. */
	FSlateColor AgeColor() const;
	TSharedRef<SWidget> MakeConnectionMenu();

private:
	TSharedPtr<IProsettaDashboardModel> Model;
};
