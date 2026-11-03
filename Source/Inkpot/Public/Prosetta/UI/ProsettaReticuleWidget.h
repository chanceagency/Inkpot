#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProsettaReticuleWidget.generated.h"

/**
 * Optional, hit-test-transparent radial indicator. Progress sweeps clockwise from twelve o'clock.
 *
 * The widget is not volatile: it repaints only when a value changes. Prefer the setters from C++; they
 * invalidate immediately. Direct writes to the fields still work, because NativeTick compares them with
 * the last values it saw and invalidates on a change, but inside an invalidation or retainer panel they
 * can reach the screen one frame later than a setter would.
 */
UCLASS(BlueprintType, Blueprintable)
class INKPOT_API UProsettaReticuleWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UProsettaReticuleWidget(const FObjectInitializer& Initializer);
	/** Diameter in Slate units (viewport DPI scaling applies). Also the widget's minimum desired size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetDiameter, Category="Prosetta|Reticule", meta=(ClampMin="4", ClampMax="512"))
	float Diameter = 24.f;
	/** Ring width; set to half the diameter for a filled circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetThickness, Category="Prosetta|Reticule", meta=(ClampMin="1"))
	float Thickness = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetBaseColor, Category="Prosetta|Reticule")
	FLinearColor BaseColor = FLinearColor(0.12f, 0.12f, 0.12f, 0.65f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetProgressColor, Category="Prosetta|Reticule")
	FLinearColor ProgressColor = FLinearColor(0.2f, 0.6f, 1.f, 0.8f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetProgress, Category="Prosetta|Reticule", meta=(ClampMin="0", ClampMax="1"))
	float Progress = 0.f;

	/** Sets Diameter and invalidates layout so auto-sized slots follow it. */
	UFUNCTION(BlueprintSetter)
	void SetDiameter(float InDiameter);
	UFUNCTION(BlueprintSetter)
	void SetThickness(float InThickness);
	UFUNCTION(BlueprintSetter)
	void SetBaseColor(FLinearColor InColor);
	UFUNCTION(BlueprintSetter)
	void SetProgressColor(FLinearColor InColor);
	/** Sets Progress (0-1) and invalidates paint. */
	UFUNCTION(BlueprintSetter)
	void SetProgress(float InProgress);

	virtual void SynchronizeProperties() override;
protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
	/** The painted fields as last observed, compared bitwise so a NaN field does not invalidate every frame. */
	struct FObservedState
	{
		float Diameter = 0.f;
		float Thickness = 0.f;
		float Progress = 0.f;
		FLinearColor BaseColor = FLinearColor::Transparent;
		FLinearColor ProgressColor = FLinearColor::Transparent;
	};
	/** Invalidates paint, or layout when the diameter changed, if any painted field differs from Observed. */
	void InvalidateIfChanged();
	/** Keeps auto-sized slots at the painted diameter; the widget has no widget tree to size it otherwise. */
	void SyncDesiredSize();
	FObservedState Observed;
};
