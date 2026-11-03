#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Prosetta/UI/ProsettaReticuleWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/SNullWidget.h"
#include "Types/PaintArgs.h"
#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaReticulePaintTest, "Inkpot.Prosetta.Reticule.Paint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaReticulePaintTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ProsettaReticuleTest"));
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	TSharedPtr<SVirtualWindow> Window;
	ON_SCOPE_EXIT
	{
		if (Window) { Window->SetContent(SNullWidget::NullWidget); }
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	auto* Widget = CreateWidget<UProsettaReticuleWidget>(World, UProsettaReticuleWidget::StaticClass());
	if (!TestNotNull(TEXT("radial widget"), Widget)) { return false; }
	Widget->Diameter = 40.f;
	Widget->Thickness = 4.f;
	const TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	Window = SNew(SVirtualWindow);
	Window->SetContent(SlateWidget);
	Window->Resize(FVector2D(640, 360));
	Window->SlatePrepass(1.f);
	TestTrue(TEXT("auto slots size to the diameter"), FVector2D(SlateWidget->GetDesiredSize()).Equals(FVector2D(40, 40), 0.01));
	auto Paint = [&Window](FSlateWindowElementList& Elements, float Scale)
	{
		auto& Grid = Window->GetHittestGrid();
		Grid.Clear(); Grid.SetHittestArea(FVector2D::ZeroVector, FVector2D(640, 360));
		Window->Paint(FPaintArgs(nullptr, Grid, FVector2D::ZeroVector, FPlatformTime::Seconds(), 0.f),
			FGeometry::MakeRoot(FVector2D(640, 360) / Scale, FSlateLayoutTransform(Scale)), FSlateRect(0, 0, 640, 360),
			Elements, 0, FWidgetStyle(), true);
	};
	auto ArcPoint = [](float Fraction, float Radius, FVector2f Center = FVector2f(320, 180))
	{
		const float Angle = -UE_HALF_PI + UE_TWO_PI * Fraction;
		return Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
	};
	for (float Progress : {0.f, 0.25f, 0.5f, 1.f})
	{
		Widget->Progress = Progress;
		FSlateWindowElementList Elements(Window);
		Paint(Elements, 1.f);
		const auto& Lines = Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Line>();
		const bool bSplit = Progress > 0 && Progress < 1;
		TestEqual(TEXT("base and progress arcs"), Lines.Num(), bSplit ? 2 : 1);
		if (!Lines.IsEmpty())
		{
			TestEqual(TEXT("editable thickness"), Lines[0].GetThickness(), 4.f);
			TestTrue(TEXT("viewport center"), Lines[0].GetPoints()[0].Equals(ArcPoint(Progress < 1.f ? Progress : 0.f, 18), 0.01f));
			TestTrue(TEXT("first arc color"), Lines[0].GetTint().Equals(Progress < 1.f ? Widget->BaseColor : Widget->ProgressColor, 1e-4f));
			TestFalse(TEXT("enabled draw effect"), EnumHasAnyFlags(Lines[0].GetDrawEffects(), ESlateDrawEffect::DisabledEffect));
		}
		if (Progress > 0 && !Lines.IsEmpty())
		{
			TestTrue(TEXT("clockwise from top"), Lines.Last().GetPoints().Last().Equals(ArcPoint(Progress, 18), 0.01f));
			TestTrue(TEXT("progress color"), Lines.Last().GetTint().Equals(Widget->ProgressColor, 1e-4f));
		}
	}

	// Non-finite progress falls back to an empty sweep: a single base ring.
	Widget->Progress = std::numeric_limits<float>::quiet_NaN();
	{
		FSlateWindowElementList Elements(Window);
		Paint(Elements, 1.f);
		const auto& Lines = Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Line>();
		if (TestEqual(TEXT("NaN progress draws only the base ring"), Lines.Num(), 1))
		{
			TestTrue(TEXT("NaN progress base color"), Lines[0].GetTint().Equals(Widget->BaseColor, 1e-4f));
		}
	}

	// Widget color, disabled state and an oversized ring width.
	Widget->Progress = 0.f;
	Widget->Thickness = 100.f;
	const FLinearColor Fade(1.f, 0.5f, 1.f, 0.5f);
	Widget->SetColorAndOpacity(Fade);
	Widget->SetIsEnabled(false);
	{
		FSlateWindowElementList Elements(Window);
		Paint(Elements, 1.f);
		const auto& Lines = Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Line>();
		if (TestEqual(TEXT("styled base ring"), Lines.Num(), 1))
		{
			TestTrue(TEXT("color and opacity tints the ring"), Lines[0].GetTint().Equals(Widget->BaseColor * Fade, 1e-4f));
			TestTrue(TEXT("disabled draw effect"), EnumHasAnyFlags(Lines[0].GetDrawEffects(), ESlateDrawEffect::DisabledEffect));
			TestEqual(TEXT("thickness clamps to half the diameter"), Lines[0].GetThickness(), 20.f);
			TestTrue(TEXT("clamped ring radius"), Lines[0].GetPoints()[0].Equals(ArcPoint(0.f, 10), 0.01f));
		}
	}
	Widget->SetColorAndOpacity(FLinearColor::White);
	Widget->SetIsEnabled(true);
	Widget->Thickness = 4.f;

	// Points stay in local units under DPI scaling, so the pixel thickness must scale with them.
	FSlateWindowElementList Scaled(Window);
	Paint(Scaled, 2.f);
	const auto& ScaledLines = Scaled.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Line>();
	if (TestEqual(TEXT("scaled base arc"), ScaledLines.Num(), 1))
	{
		TestEqual(TEXT("DPI-scaled thickness"), ScaledLines[0].GetThickness(), 8.f);
		TestTrue(TEXT("local-unit radius"), ScaledLines[0].GetPoints()[0].Equals(FVector2f(160, 72), 0.01f));
	}

	// A runtime diameter change reaches the desired size on the next tick.
	Widget->Diameter = 60.f;
	{
		FSlateWindowElementList Elements(Window);
		Paint(Elements, 1.f);
	}
	Window->SlatePrepass(1.f);
	TestTrue(TEXT("runtime diameter resizes auto slots"), FVector2D(SlateWidget->GetDesiredSize()).Equals(FVector2D(60, 60), 0.01));
	return true;
}

#endif
