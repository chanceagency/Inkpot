#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProsettaReticuleWidget.generated.h"

/** Optional, hit-test-transparent radial indicator. Progress sweeps clockwise from twelve o'clock. */
UCLASS(BlueprintType, Blueprintable)
class INKPOT_API UProsettaReticuleWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UProsettaReticuleWidget(const FObjectInitializer& Initializer);
	/** Diameter in Slate units (viewport DPI scaling applies). Also the widget's minimum desired size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Reticule", meta=(ClampMin="4", ClampMax="512"))
	float Diameter = 24.f;
	/** Ring width; set to half the diameter for a filled circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Reticule", meta=(ClampMin="1"))
	float Thickness = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Reticule")
	FLinearColor BaseColor = FLinearColor(0.12f, 0.12f, 0.12f, 0.65f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Reticule")
	FLinearColor ProgressColor = FLinearColor(0.2f, 0.6f, 1.f, 0.8f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Reticule", meta=(ClampMin="0", ClampMax="1"))
	float Progress = 0.f;

	virtual void SynchronizeProperties() override;
protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
private:
	/** Keeps auto-sized slots at the painted diameter; the widget has no widget tree to size it otherwise. */
	void SyncDesiredSize();
};
