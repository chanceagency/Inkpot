#if !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "Inkpot/Prosetta/InkpotProsettaLibrary.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaParserTest, "Inkpot.Prosetta.Parser",
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::EngineFilter |
	EAutomationTestFlags::HighPriority)

bool FProsettaParserTest::RunTest(const FString& InParameters)
{
	// Plain text passes through untouched and yields no segments.
	{
		const FProsettaParseResult result = UInkpotProsettaLibrary::ParseFragment(TEXT("Just some dialogue."));
		TestEqual(TEXT("plain text unchanged"), result.CleanText, TEXT("Just some dialogue."));
		TestFalse(TEXT("plain text has no tags"), result.bHasTags);
		TestEqual(TEXT("plain text no segments"), result.Segments.Num(), 0);
	}

	// A single tag is stripped to its inner text and one segment is recovered with its
	// id and data attributes ( both quote styles ).
	{
		const FString fragment = TEXT("<prosetta id='023A' animate='wave' sfx=\"carHorn.mp3\">Honk honk, mothertrucker!</prosetta>");
		const FProsettaParseResult result = UInkpotProsettaLibrary::ParseFragment(fragment);
		TestTrue(TEXT("has tags"), result.bHasTags);
		TestEqual(TEXT("clean text is inner text"), result.CleanText, TEXT("Honk honk, mothertrucker!"));
		if (TestEqual(TEXT("one segment"), result.Segments.Num(), 1))
		{
			const FProsettaParsedSegment& seg = result.Segments[0];
			TestEqual(TEXT("line id"), seg.LineId, FName(TEXT("023A")));
			TestEqual(TEXT("segment start"), seg.StartOffset, 0);
			TestEqual(TEXT("segment length"), seg.Length, result.CleanText.Len());
			TestEqual(TEXT("attr count"), seg.Attributes.Num(), 2);
			const FString* animate = seg.Attributes.Find(FName(TEXT("animate")));
			const FString* sfx = seg.Attributes.Find(FName(TEXT("sfx")));
			TestTrue(TEXT("animate present"), animate != nullptr && *animate == TEXT("wave"));
			TestTrue(TEXT("sfx present"), sfx != nullptr && *sfx == TEXT("carHorn.mp3"));
		}
	}

	// Surrounding prose and offsets are preserved.
	{
		const FString fragment = TEXT("She said <prosetta id='0BB3'>hello</prosetta> warmly.");
		const FProsettaParseResult result = UInkpotProsettaLibrary::ParseFragment(fragment);
		TestEqual(TEXT("prose preserved"), result.CleanText, TEXT("She said hello warmly."));
		if (TestEqual(TEXT("one segment"), result.Segments.Num(), 1))
		{
			TestEqual(TEXT("line id preserved"), result.Segments[0].LineId.ToString(), FString(TEXT("0BB3")));
			TestEqual(TEXT("offset after prefix"), result.Segments[0].StartOffset, 9);
			TestEqual(TEXT("segment length"), result.Segments[0].Length, 5);
		}
	}

	// A resolver supplying an authoritative string replaces the inner text in the
	// cleaned output, and offsets reflect the replacement.
	{
		const FString fragment = TEXT("<prosetta id='023A'>placeholder</prosetta>");
		auto resolver = [](FName InLineId, FString& OutText) -> bool
		{
			if (InLineId == FName(TEXT("023A"))) { OutText = TEXT("Honk honk!"); return true; }
			return false;
		};
		const FProsettaParseResult result = UInkpotProsettaLibrary::ParseFragmentResolved(fragment, resolver);
		TestEqual(TEXT("authoritative string used"), result.CleanText, TEXT("Honk honk!"));
		TestEqual(TEXT("segment text resolved"), result.Segments[0].Text, TEXT("Honk honk!"));
	}

	// Malformed / unbalanced markup degrades gracefully rather than dropping content.
	{
		const FProsettaParseResult result = UInkpotProsettaLibrary::ParseFragment(TEXT("oops <prosetta id='x'>no close"));
		TestTrue(TEXT("malformed keeps content"), result.CleanText.Contains(TEXT("no close")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaMetadataTest, "Inkpot.Prosetta.Metadata",
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::EngineFilter |
	EAutomationTestFlags::HighPriority)

bool FProsettaMetadataTest::RunTest(const FString& InParameters)
{
	const FString json = TEXT(R"JSON(
	{
		"version": 1,
		"projectId": 12,
		"lines": {
			"023A": {
				"string": "Honk honk, mothertrucker!",
				"kind": "dialogue",
				"speaker": "TRUCKER",
				"tags": { "mood": "angry" },
				"localizations": { "fr": "Tut tut !" }
			}
		}
	}
	)JSON");

	UProsettaMetadata* metadata = NewObject<UProsettaMetadata>();
	TestTrue(TEXT("loaded"), metadata->LoadFromJSON(json));
	TestEqual(TEXT("one line"), metadata->Num(), 1);

	FString text;
	TestTrue(TEXT("resolve text"), metadata->ResolveText(FName(TEXT("023A")), text));
	TestEqual(TEXT("authoritative string"), text, TEXT("Honk honk, mothertrucker!"));

	FProsettaLineMetadata line;
	if (TestTrue(TEXT("resolve metadata"), metadata->Resolve(FName(TEXT("023A")), line)))
	{
		TestEqual(TEXT("sidecar line id preserved"), line.LineId.ToString(), FString(TEXT("023A")));
		TestEqual(TEXT("kind"), line.Kind, TEXT("dialogue"));
		TestEqual(TEXT("speaker"), line.SpeakerShortname, TEXT("TRUCKER"));
		const FString* mood = line.Tags.Find(TEXT("mood"));
		TestTrue(TEXT("tag mood"), mood != nullptr && *mood == TEXT("angry"));
		const FString* fr = line.Localizations.Find(TEXT("fr"));
		TestTrue(TEXT("localization fr"), fr != nullptr && *fr == TEXT("Tut tut !"));
	}

	FProsettaLineMetadata missing;
	TestFalse(TEXT("unknown id unresolved"), metadata->Resolve(FName(TEXT("ZZZZ")), missing));

	return true;
}

#endif // !UE_BUILD_SHIPPING
