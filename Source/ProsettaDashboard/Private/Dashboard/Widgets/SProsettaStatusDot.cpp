#include "SProsettaStatusDot.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"

void SProsettaStatusDot::Construct(const FArguments& InArgs)
{
	Connection = InArgs._Connection;
	ChildSlot
	[
		SNew(SImage)
		.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
		.DesiredSizeOverride(FVector2D(12.0, 12.0))
		.ToolTipText(InArgs._ToolTipText)
		.ColorAndOpacity_Lambda([this]() { return ColorFor(Connection.Get()); })
	];
}

FSlateColor SProsettaStatusDot::ColorFor(EProsettaDashboardConnection InConnection)
{
	switch (InConnection)
	{
	case EProsettaDashboardConnection::Connected: return FStyleColors::Success;
	case EProsettaDashboardConnection::Degraded:
	case EProsettaDashboardConnection::Blocked:
	case EProsettaDashboardConnection::Connecting: return FStyleColors::Warning;
	case EProsettaDashboardConnection::Error:
	case EProsettaDashboardConnection::AuthFailed:
	case EProsettaDashboardConnection::NoCli:
	case EProsettaDashboardConnection::NoCredentials: return FStyleColors::Error;
	case EProsettaDashboardConnection::Disconnected:
	default: return FStyleColors::AccentGray;
	}
}
