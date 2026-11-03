#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Prosetta/UI/ProsettaReticuleWidget.h"
#include "Engine/World.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/SNullWidget.h"
#include "Types/PaintArgs.h"
#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaReticulePaintTest, "Inkpot.Prosetta.Reticule.Paint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProsettaReticulePaintTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Widget = CreateWidget<UProsettaReticuleWidget>(World, UProsettaReticuleWidget::StaticClass());
    if (!TestNotNull(TEXT("radial widget"), Widget)) { World->DestroyWorld(false); return false; }
    Widget->Diameter = 40.f;
    Widget->Thickness = 4.f;
    auto Window = SNew(SVirtualWindow);
    Window->SetContent(Widget->TakeWidget());
    Window->Resize(FVector2D(640, 360));
    Window->SlatePrepass(1.f);
    for (float Progress : {0.f, 0.25f, 0.5f, 1.f})
    {
        Widget->Progress = Progress;
        auto& Grid = Window->GetHittestGrid();
        Grid.Clear(); Grid.SetHittestArea(FVector2D::ZeroVector, FVector2D(640, 360));
        FSlateWindowElementList Elements(Window);
        Window->Paint(FPaintArgs(nullptr, Grid, FVector2D::ZeroVector, FPlatformTime::Seconds(), 0.f),
            FGeometry::MakeRoot(FVector2D(640, 360), FSlateLayoutTransform()), FSlateRect(0, 0, 640, 360),
            Elements, 0, FWidgetStyle(), true);
        const auto& Lines = Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Line>();
        TestEqual(TEXT("base and progress arcs"), Lines.Num(), Progress > 0 && Progress < 1 ? 2 : 1);
        if (!Lines.IsEmpty())
        {
            TestEqual(TEXT("editable thickness"), Lines[0].GetThickness(), 4.f);
            const float Start = Progress < 1.f ? Progress : 0.f;
            const float Angle = -HALF_PI + 2.f * PI * Start;
            TestTrue(TEXT("viewport center"), Lines[0].GetPoints()[0].Equals(
                FVector2f(320 + 18 * FMath::Cos(Angle), 180 + 18 * FMath::Sin(Angle)), 0.01f));
        }
        if (Progress > 0 && !Lines.IsEmpty())
        {
            const float Angle = -HALF_PI + 2.f * PI * Progress;
            TestTrue(TEXT("clockwise from top"), Lines.Last().GetPoints().Last().Equals(
                FVector2f(320 + 18 * FMath::Cos(Angle), 180 + 18 * FMath::Sin(Angle)), 0.01f));
        }
    }
    Window->SetContent(SNullWidget::NullWidget);
    World->DestroyWorld(false);
    return true;
}
#endif
