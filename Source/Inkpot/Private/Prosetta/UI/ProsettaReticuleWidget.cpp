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
	// Paint reads plain UPROPERTY fields, so repaint every frame rather than relying on invalidation.
	bIsVolatile = true;
}

void UProsettaReticuleWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	SyncDesiredSize();
}

void UProsettaReticuleWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	// Diameter is a plain field, so pick up runtime changes here; SetMinimumDesiredSize only invalidates on change.
	SyncDesiredSize();
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
		TArray<FVector2f> Points;
		Points.Reserve(Segments + 1);
		for (int32 I = 0; I <= Segments; ++I)
		{
			const float Angle = -UE_HALF_PI + UE_TWO_PI * (Start + Fraction * I / Segments);
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
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
