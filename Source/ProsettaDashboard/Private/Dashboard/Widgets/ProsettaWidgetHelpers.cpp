#include "ProsettaWidgetHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/App.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace ProsettaWidgets
{
	void Notify(const FText& Message, bool bFail)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bFail ? 8.0f : 4.0f;
		Info.bUseSuccessFailIcons = true;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bFail ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		}
	}

	FString LocalClock(const FDateTime& Utc)
	{
		const FTimespan Offset = FDateTime::Now() - FDateTime::UtcNow();
		return (Utc + Offset).ToString(TEXT("%H:%M:%S"));
	}

	bool CanShowModal()
	{
		return FSlateApplication::IsInitialized() && !FApp::IsUnattended() && !IsRunningCommandlet() && !GIsAutomationTesting;
	}
}
