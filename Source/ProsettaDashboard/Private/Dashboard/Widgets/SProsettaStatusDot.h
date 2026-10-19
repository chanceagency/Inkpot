#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Widgets/SCompoundWidget.h"
#include "ProsettaDashboardModel.h"

/** A coloured dot for the headline connection state (green / amber / red / grey). */
class PROSETTADASHBOARD_API SProsettaStatusDot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaStatusDot) : _Connection(EProsettaDashboardConnection::Disconnected) {}
		SLATE_ATTRIBUTE(EProsettaDashboardConnection, Connection)
		SLATE_ATTRIBUTE(FText, ToolTipText)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);

	/** Success, Warning, Error or AccentGray for a connection state. */
	static FSlateColor ColorFor(EProsettaDashboardConnection Connection);
	EProsettaDashboardConnection GetConnection() const { return Connection.Get(); }

private:
	TAttribute<EProsettaDashboardConnection> Connection;
};
