#include "Prosetta/UI/ProsettaReticuleWidget.h"
#include "Rendering/DrawElements.h"

UProsettaReticuleWidget::UProsettaReticuleWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

int32 UProsettaReticuleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
    const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
    const FWidgetStyle& Style, bool bParentEnabled) const
{
    Layer = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled);
    const float Size = FMath::Clamp(FMath::IsFinite(Diameter) ? Diameter : 24.f, 4.f, 512.f);
    const float Width = FMath::Clamp(FMath::IsFinite(Thickness) ? Thickness : 3.f, 1.f, Size * 0.5f);
    const FVector2f Center(Geometry.GetLocalSize() * 0.5f);
    const float Radius = (Size - Width) * 0.5f;
    auto Arc = [&](float Start, float Fraction, const FLinearColor& Color, int32 ArcLayer)
    {
        if (Fraction <= 0.f) { return; }
        const int32 Segments = FMath::Max(1, FMath::CeilToInt(128.f * Fraction));
        TArray<FVector2f> Points;
        for (int32 I = 0; I <= Segments; ++I)
        {
            const float Angle = -HALF_PI + 2.f * PI * (Start + Fraction * I / Segments);
            Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
        }
        FSlateDrawElement::MakeLines(Elements, ArcLayer, Geometry.ToPaintGeometry(), Points,
            ESlateDrawEffect::None, Color * Style.GetColorAndOpacityTint(), true, Width);
    };
    const float Fraction = FMath::Clamp(FMath::IsFinite(Progress) ? Progress : 0.f, 0.f, 1.f);
    // Complementary arcs keep the authored alpha stable across milestone transitions.
    Arc(Fraction, 1.f - Fraction, BaseColor, ++Layer);
    Arc(0.f, Fraction, ProgressColor, ++Layer);
    return Layer;
}
