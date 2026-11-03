#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Asset/InkpotStoryAsset.h"
#include "Inkpot/Inkpot.h"
#include "Inkpot/InkpotChoice.h"
#include "Inkpot/InkpotStory.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace InkpotStoryEventTests
{
	/**
	 * Compiled with inklecate from:
	 *
	 *   Hello
	 *   * Pick
	 *     Picked
	 *     -> END
	 */
	const TCHAR* ChoiceStoryJSON = TEXT(R"JSON({"inkVersion":21,"root":[["^Hello","\n",["ev",{"^->":"0.2.$r1"},{"temp=":"$r"},"str",{"->":".^.s"},[{"#n":"$r1"}],"/str","/ev",{"*":"0.c-0","flg":18},{"s":["^Pick",{"->":"$r","var":true},null]}],{"c-0":["ev",{"^->":"0.c-0.$r2"},"/ev",{"temp=":"$r"},{"->":"0.2.s"},[{"#n":"$r2"}],"\n","^Picked","\n","end",{"->":"0.g-0"},{"#f":5}],"g-0":["done",null]}],"done",null],"listDefs":{}})JSON");

	/** Number of events the story's history recorded, or INDEX_NONE if it has none. */
	int32 HistoryEventCount(UInkpotStory* InStory)
	{
		TArray<UObject*> inner;
		GetObjectsWithOuter(InStory, inner, false);
		for (UObject* object : inner)
		{
			if (object->GetClass()->GetFName() != TEXT("InkpotStoryHistory"))
				continue;
			const FArrayProperty* events = FindFProperty<FArrayProperty>(object->GetClass(), TEXT("Events"));
			if (!events)
				return INDEX_NONE;
			FScriptArrayHelper helper(events, events->ContainerPtrToValuePtr<void>(object));
			return helper.Num();
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotStoryMakeChoiceEventTest, "Inkpot.Story.Events.MakeChoiceWithoutContinueListener",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FInkpotStoryMakeChoiceEventTest::RunTest(const FString& InParameters)
{
	UInkpot* inkpot = GEngine ? GEngine->GetEngineSubsystem<UInkpot>() : nullptr;
	if (!TestNotNull(TEXT("inkpot subsystem"), inkpot))
		return false;

	UInkpotStoryAsset* asset = NewObject<UInkpotStoryAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	asset->SetCompiledJSON(InkpotStoryEventTests::ChoiceStoryJSON);
	UInkpotStory* story = inkpot->BeginStory(asset);
	if (!TestNotNull(TEXT("story began"), story))
		return false;

	// The story history listens to OnMakeChoice. With no OnContinue listener left,
	// a made choice must still reach it.
	story->OnContinue().Clear();
	TestEqual(TEXT("history starts empty"), InkpotStoryEventTests::HistoryEventCount(story), 0);
	while (story->CanContinue())
		story->Continue();
	const TArray<UInkpotChoice*>& choices = story->GetCurrentChoices();
	if (TestEqual(TEXT("one choice"), choices.Num(), 1))
	{
		story->ChooseChoice(choices[0]);
		TestEqual(TEXT("OnMakeChoice reached a listener without OnContinue"), InkpotStoryEventTests::HistoryEventCount(story), 1);
	}

	inkpot->EndStory(story);
	return true;
}

#endif
