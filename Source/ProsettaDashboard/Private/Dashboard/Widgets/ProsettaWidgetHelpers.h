#pragma once
#include "CoreMinimal.h"

/** Small helpers shared by the dashboard panels. */
namespace ProsettaWidgets
{
	/** A short Slate toast (no-op without a Slate application). */
	PROSETTADASHBOARD_API void Notify(const FText& Message, bool bFail);
	/** Converts a UTC stamp to local wall-clock "HH:MM:SS". */
	PROSETTADASHBOARD_API FString LocalClock(const FDateTime& Utc);
	/** True when a modal window may be shown (Slate up, not unattended / commandlet). */
	PROSETTADASHBOARD_API bool CanShowModal();
}
