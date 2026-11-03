#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Prosetta/UI/ProsettaReticuleWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SNullWidget.h"
#include "Types/PaintArgs.h"
#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"
#include "Debugging/SlateDebugging.h"
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
	TestFalse(TEXT("reticule is not volatile"), SlateWidget->IsVolatile());
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
	// Outside a cache every frame repaints, so a direct field write shows up in the very next paint.
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
	Widget->SetDiameter(32.f);
	Window->SlatePrepass(1.f);
	TestTrue(TEXT("diameter setter resizes auto slots"), FVector2D(SlateWidget->GetDesiredSize()).Equals(FVector2D(32, 32), 0.01));

	// Inside an invalidation panel the ring is cached rather than repainted every frame, so a change must
	// invalidate it. Each simulated frame runs a prepass and a paint, as Slate does, and reads the panel's
	// cached elements, which are what the screen shows.
	auto* Cached = CreateWidget<UProsettaReticuleWidget>(World, UProsettaReticuleWidget::StaticClass());
	if (!TestNotNull(TEXT("cached radial widget"), Cached)) { return false; }
	Cached->Diameter = 40.f;
	Cached->Thickness = 4.f;
	const TSharedRef<SWidget> CachedSlate = Cached->TakeWidget();
	const TSharedRef<SInvalidationPanel> Panel = SNew(SInvalidationPanel)[CachedSlate];
	Window->SetContent(Panel);
	Window->SlatePrepass(1.f);
	TestTrue(TEXT("invalidation panel caches"), Panel->GetCanCache());
	int32 CachedPaints = 0;
#if WITH_SLATE_DEBUGGING
	const FDelegateHandle PaintCounter = FSlateDebugging::BeginWidgetPaint.AddLambda(
		[&CachedPaints, Target = &CachedSlate.Get()](const SWidget* Painted, const FPaintArgs&, const FGeometry&, const FSlateRect&, const FSlateWindowElementList&, int32)
		{
			CachedPaints += Painted == Target;
		});
	ON_SCOPE_EXIT { FSlateDebugging::BeginWidgetPaint.Remove(PaintCounter); };
#endif
	struct FPaintedArc { FLinearColor Tint; FVector2f End; };
	auto PaintFrame = [&Paint, &Window, &Panel]()
	{
		Window->SlatePrepass(1.f);
		FSlateWindowElementList Elements(Window);
		Paint(Elements, 1.f);
		TArray<FPaintedArc> Arcs;
		for (const TSharedPtr<FSlateCachedElementList>& List : Panel->GetCachedElements().GetCachedElementLists())
		{
			for (const auto& Line : List->DrawElements.Get<(uint8)EElementType::ET_Line>())
			{
				Arcs.Add({Line.GetTint(), Line.GetPoints().Last()});
			}
		}
		return Arcs;
	};
	TArray<FPaintedArc> Arcs = PaintFrame();
	TestEqual(TEXT("cached base ring"), Arcs.Num(), 1);
	// The panel builds its cache over the first frames; count repaints once it is on the fast path.
	Arcs = PaintFrame();
	CachedPaints = 0;
	Arcs = PaintFrame();
	Arcs = PaintFrame();
	TestEqual(TEXT("unchanged ring stays cached"), Arcs.Num(), 1);
#if WITH_SLATE_DEBUGGING
	TestEqual(TEXT("unchanged ring is not repainted"), CachedPaints, 0);
#endif

	// A setter invalidates immediately, so the next frame repaints the ring.
	Cached->SetProgress(0.5f);
	Arcs = PaintFrame();
	if (TestEqual(TEXT("progress setter repaints"), Arcs.Num(), 2))
	{
		TestTrue(TEXT("setter progress end"), Arcs.Last().End.Equals(ArcPoint(0.5f, 18), 0.01f));
		TestTrue(TEXT("setter progress color"), Arcs.Last().Tint.Equals(Cached->ProgressColor, 1e-4f));
	}
	const FLinearColor Green(0.f, 1.f, 0.f, 1.f);
	Cached->SetProgressColor(Green);
	Arcs = PaintFrame();
	if (TestEqual(TEXT("color setter keeps both arcs"), Arcs.Num(), 2))
	{
		TestTrue(TEXT("color setter repaints"), Arcs.Last().Tint.Equals(Green, 1e-4f));
	}

	// A direct write is noticed when the widget ticks. On the fast path that tick runs during the frame's
	// update, so the new value is cached by the following frame: one frame later than a setter.
	Cached->Progress = 0.25f;
	Arcs = PaintFrame();
	Arcs = PaintFrame();
	if (TestEqual(TEXT("direct progress write repaints"), Arcs.Num(), 2))
	{
		TestTrue(TEXT("direct write progress end"), Arcs.Last().End.Equals(ArcPoint(0.25f, 18), 0.01f));
	}
	CachedPaints = 0;
	Arcs = PaintFrame();
#if WITH_SLATE_DEBUGGING
	TestEqual(TEXT("ring settles after a direct write"), CachedPaints, 0);
#endif
	return true;
}

#endif
