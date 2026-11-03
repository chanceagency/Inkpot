#include "Prosetta/UI/ProsettaReticuleWidget.h"
#include "Rendering/DrawElements.h"
#include "Slate/SObjectWidget.h"

namespace
{
	float ReticuleSize(float Diameter)
	{
		return FMath::Clamp(FMath::IsFinite(Diameter) ? Diameter : 24.f, 4.f, 512.f);
	}
}

UProsettaReticuleWidget::UProsettaReticuleWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
	// Not volatile: the setters and NativeTick invalidate paint only when a painted field changes, so the
	// ring is not rebuilt every frame and stays cached inside invalidation and retainer panels.
}

void UProsettaReticuleWidget::SetDiameter(float InDiameter)
{
	Diameter = InDiameter;
	InvalidateIfChanged();
}

void UProsettaReticuleWidget::SetThickness(float InThickness)
{
	Thickness = InThickness;
	InvalidateIfChanged();
}

void UProsettaReticuleWidget::SetBaseColor(FLinearColor InColor)
{
	BaseColor = InColor;
	InvalidateIfChanged();
}

void UProsettaReticuleWidget::SetProgressColor(FLinearColor InColor)
{
	ProgressColor = InColor;
	InvalidateIfChanged();
}

void UProsettaReticuleWidget::SetProgress(float InProgress)
{
	Progress = InProgress;
	InvalidateIfChanged();
}

void UProsettaReticuleWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	// The Slate widget paints from the current fields after a rebuild, so they become the observed state.
	Observed = FObservedState{Diameter, Thickness, Progress, BaseColor, ProgressColor};
	SyncDesiredSize();
}

void UProsettaReticuleWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	// The fields are public and may be written directly, so pick up those writes here.
	InvalidateIfChanged();
}

void UProsettaReticuleWidget::InvalidateIfChanged()
{
	const FObservedState Current{Diameter, Thickness, Progress, BaseColor, ProgressColor};
	static_assert(sizeof(FObservedState) == sizeof(float) * 11, "FObservedState must be unpadded for Memcmp");
	if (FMemory::Memcmp(&Current, &Observed, sizeof(FObservedState)) == 0) { return; }
	const bool bResized = FMemory::Memcmp(&Current.Diameter, &Observed.Diameter, sizeof(float)) != 0;
	Observed = Current;
	if (bResized)
	{
		// SetMinimumDesiredSize invalidates layout only when the clamped size actually changes.
		SyncDesiredSize();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UProsettaReticuleWidget::SyncDesiredSize()
{
	SetMinimumDesiredSize(FVector2D(ReticuleSize(Diameter)));
}

int32 UProsettaReticuleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	Layer = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled);
	const float Size = ReticuleSize(Diameter);
	const float Width = FMath::Clamp(FMath::IsFinite(Thickness) ? Thickness : 3.f, 1.f, Size * 0.5f);
	const FVector2f Center(Geometry.GetLocalSize() * 0.5f);
	const float Radius = (Size - Width) * 0.5f;
	// Line thickness is in pixels; scale it so the ring keeps its local-unit proportions under DPI scaling.
	const float LineThickness = Width * Geometry.Scale;
	const ESlateDrawEffect Effect = bParentEnabled && GetIsEnabled() ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	// SObjectWidget does not compound its own color into the style it hands NativePaint. Read it from the
	// Slate widget so a ColorAndOpacity binding applies, falling back to the property before it exists.
	const TSharedPtr<SObjectWidget> SlateWidget = MyGCWidget.Pin();
	const FLinearColor Tint = (SlateWidget ? SlateWidget->GetColorAndOpacity() : GetColorAndOpacity()) * Style.GetColorAndOpacityTint();
	auto Arc = [&](float Start, float Fraction, const FLinearColor& Color, int32 ArcLayer)
	{
		if (Fraction <= 0.f) { return; }
		const int32 Segments = FMath::Max(1, FMath::CeilToInt(128.f * Fraction));
		// Rotate a unit vector by a fixed step instead of evaluating sin/cos for every point.
		FVector2f Direction;
		FMath::SinCos(&Direction.Y, &Direction.X, -UE_HALF_PI + UE_TWO_PI * Start);
		float StepSin, StepCos;
		FMath::SinCos(&StepSin, &StepCos, UE_TWO_PI * Fraction / Segments);
		TArray<FVector2f> Points;
		Points.SetNumUninitialized(Segments + 1);
		for (FVector2f& Point : Points)
		{
			Point = Center + Direction * Radius;
			Direction = FVector2f(Direction.X * StepCos - Direction.Y * StepSin, Direction.X * StepSin + Direction.Y * StepCos);
		}
		FSlateDrawElement::MakeLines(Elements, ArcLayer, Geometry.ToPaintGeometry(), MoveTemp(Points),
			Effect, Color * Tint, true, LineThickness);
	};
	const float Fraction = FMath::Clamp(FMath::IsFinite(Progress) ? Progress : 0.f, 0.f, 1.f);
	// Complementary arcs keep the authored alpha stable across milestone transitions.
	Arc(Fraction, 1.f - Fraction, BaseColor, ++Layer);
	Arc(0.f, Fraction, ProgressColor, ++Layer);
	return Layer;
}
