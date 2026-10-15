#if !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "Inkpot/Prosetta/InkpotProsettaLibrary.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"
#include "Inkpot/Prosetta/ProsettaLine.h"
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
		TestEqual(TEXT("runtime text kept"), result.Segments[0].RuntimeText, TEXT("placeholder"));
	}

	// The resolver sees the runtime inner text, so it can evaluate templates against it.
	{
		const FString fragment = TEXT("<prosetta id='0001'>I'm a math wiz, 4 equals four!</prosetta>");
		FString seen;
		auto resolver = [&seen](FName InLineId, FString& InOutText) -> bool
		{
			seen = InOutText;
			return false;
		};
		const FProsettaParseResult result = UInkpotProsettaLibrary::ParseFragmentResolved(fragment, resolver);
		TestEqual(TEXT("resolver input is runtime text"), seen, TEXT("I'm a math wiz, 4 equals four!"));
		TestEqual(TEXT("unresolved keeps runtime text"), result.CleanText, TEXT("I'm a math wiz, 4 equals four!"));
	}

	// With nested tags the outer resolver still sees the runtime text, not nested replacements.
	{
		const FString fragment = TEXT("<prosetta id='A'>Score <prosetta id='B'>4</prosetta>!</prosetta>");
		FString seenOuter;
		auto resolver = [&seenOuter](FName InLineId, FString& InOutText) -> bool
		{
			if (InLineId == FName(TEXT("B"))) { InOutText = TEXT("four"); return true; }
			seenOuter = InOutText;
			return false;
		};
		const FProsettaParseResult result = UInkpotProsettaLibrary::ParseFragmentResolved(fragment, resolver);
		TestEqual(TEXT("outer resolver sees runtime text"), seenOuter, TEXT("Score 4!"));
		if (TestEqual(TEXT("one outer segment"), result.Segments.Num(), 1))
			TestEqual(TEXT("outer runtime text"), result.Segments[0].RuntimeText, TEXT("Score 4!"));
		TestEqual(TEXT("nested display kept when outer declines"), result.CleanText, TEXT("Score four!"));
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

	// Engine Sync records carry both a database id and the tag's lineId; join on lineId.
	TestTrue(TEXT("load Engine Sync array"), metadata->LoadFromJSON(TEXT(R"JSON(
	{"lines":[{"id":"23-0001","lineId":"0001","string":"Hello","speakerShortname":"QUEEN"},{"id":"0002","string":"Legacy"}]}
	)JSON")));
	if (TestTrue(TEXT("join by tag lineId"), metadata->Resolve(FName(TEXT("0001")), line)))
		TestEqual(TEXT("Engine Sync speaker"), line.SpeakerShortname, TEXT("QUEEN"));
	TestFalse(TEXT("record id is not the join key"), metadata->Resolve(FName(TEXT("23-0001")), missing));
	TestTrue(TEXT("id-only records still join"), metadata->ResolveText(FName(TEXT("0002")), text));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaLocalizedLanguageCodeTest, "Inkpot.Prosetta.LocalizedLanguageCodes",
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::EngineFilter |
	EAutomationTestFlags::HighPriority)

bool FProsettaLocalizedLanguageCodeTest::RunTest(const FString& InParameters)
{
	TestEqual(TEXT("normalize underscore and case"), UProsettaLine::NormalizeLanguageCode(TEXT(" pt_BR ")), FString(TEXT("pt-br")));
	TestEqual(TEXT("normalize keeps base"), UProsettaLine::NormalizeLanguageCode(TEXT("FR")), FString(TEXT("fr")));

	// Sidecar keys arrive in whatever form the project authored them.
	FProsettaLineMetadata metadata;
	metadata.LineId = TEXT("0001");
	metadata.Localizations.Add(TEXT("pt_BR"), TEXT("Ola do Brasil"));
	metadata.Localizations.Add(TEXT("pt"), TEXT("Ola de Portugal"));
	metadata.Localizations.Add(TEXT("es"), TEXT("Hola"));
	metadata.Localizations.Add(TEXT("FR-ca"), TEXT("Bonjour du Quebec"));
	metadata.Localizations.Add(TEXT("de-DE"), TEXT("{2+2} Euro"));
	metadata.Localizations.Add(TEXT("it-IT"), TEXT(""));
	metadata.Localizations.Add(TEXT("zh_Hant"), TEXT("Ni hao ( traditional )"));
	metadata.Localizations.Add(TEXT("zh-Hans"), TEXT("Ni hao ( simplified )"));

	UProsettaLine* line = NewObject<UProsettaLine>(GetTransientPackage());
	line->Initialise(TEXT("0001"), TEXT("Hello"), {}, 0, 5);
	line->SetMetadata(metadata, true);
	FProsettaTemplateValues values;
	values.Add(TEXT("2+2"), TEXT("4"));
	line->SetRuntimeText(TEXT("Hello"), MoveTemp(values));

	auto Localized = [this, line](const TCHAR* InCode, const TCHAR* InExpected, bool bInFound)
	{
		FString text;
		TestTrue(FString::Printf(TEXT("'%s' found"), InCode), line->GetLocalizedText(InCode, text) == bInFound);
		TestEqual(FString::Printf(TEXT("'%s' text"), InCode), text, FString(InExpected));
	};
	Localized(TEXT("pt_BR"), TEXT("Ola do Brasil"), true);
	Localized(TEXT("pt-BR"), TEXT("Ola do Brasil"), true);
	Localized(TEXT("pt-br"), TEXT("Ola do Brasil"), true);
	Localized(TEXT("PT_br"), TEXT("Ola do Brasil"), true);
	Localized(TEXT("pt"), TEXT("Ola de Portugal"), true);
	// A region without its own translation falls back to the base language.
	Localized(TEXT("pt-PT"), TEXT("Ola de Portugal"), true);
	Localized(TEXT("es_MX"), TEXT("Hola"), true);
	Localized(TEXT("fr_CA"), TEXT("Bonjour du Quebec"), true);
	// Script and region subtags fall back through each parent, not straight to the language.
	Localized(TEXT("zh-Hant-TW"), TEXT("Ni hao ( traditional )"), true);
	Localized(TEXT("zh_hans_CN"), TEXT("Ni hao ( simplified )"), true);
	Localized(TEXT("zh"), TEXT("Hello"), false);
	// A base request does not guess between regions.
	Localized(TEXT("fr"), TEXT("Hello"), false);
	// Normalised matches still render inline expressions.
	Localized(TEXT("de_de"), TEXT("4 Euro"), true);
	// Empty translations and unknown or empty codes fall back to the line text.
	Localized(TEXT("it-IT"), TEXT("Hello"), false);
	Localized(TEXT("it"), TEXT("Hello"), false);
	Localized(TEXT("ja"), TEXT("Hello"), false);
	Localized(TEXT(""), TEXT("Hello"), false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaPauseTagTest, "Inkpot.Prosetta.PauseTags",
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::ClientContext |
	EAutomationTestFlags::EngineFilter |
	EAutomationTestFlags::HighPriority)

bool FProsettaPauseTagTest::RunTest(const FString& InParameters)
{
	auto Valid = [this](const TCHAR* InTag, float InExpected)
	{
		float seconds = -1.f;
		if (TestTrue(FString::Printf(TEXT("'%s' parses"), InTag), UInkpotProsettaLibrary::ParsePauseTag(InTag, seconds)))
			TestEqual(FString::Printf(TEXT("'%s' seconds"), InTag), seconds, InExpected);
	};
	auto Invalid = [this](const TCHAR* InTag)
	{
		float seconds = 7.f;
		TestFalse(FString::Printf(TEXT("'%s' rejected"), InTag), UInkpotProsettaLibrary::ParsePauseTag(InTag, seconds));
		TestEqual(FString::Printf(TEXT("'%s' leaves output alone"), InTag), seconds, 7.f);
	};

	// pause and wait are synonyms, matched without regard to case.
	Valid(TEXT("pause:0.2"), 0.2f);
	Valid(TEXT("wait:1.5"), 1.5f);
	Valid(TEXT("PAUSE:2"), 2.f);
	Valid(TEXT("Wait:3"), 3.f);
	// Whitespace around the key, colon and value is ignored.
	Valid(TEXT("pause: 0.2"), 0.2f);
	Valid(TEXT("pause :0.2"), 0.2f);
	Valid(TEXT("  wait  :  .5  "), 0.5f);
	Valid(TEXT("pause:\t1"), 1.f);
	// Plain non-negative decimals, including zero and a bare fraction.
	Valid(TEXT("pause:0"), 0.f);
	Valid(TEXT("pause:.5"), 0.5f);
	Valid(TEXT("pause:007"), 7.f);
	Valid(TEXT("pause:12.25"), 12.25f);

	// Everything else falls back to the default gap.
	Invalid(TEXT("pause:-1"));
	Invalid(TEXT("pause:+1"));
	Invalid(TEXT("pause:"));
	Invalid(TEXT("pause:   "));
	Invalid(TEXT("pause:."));
	Invalid(TEXT("pause:1."));
	Invalid(TEXT("pause:1.2.3"));
	Invalid(TEXT("pause:1e3"));
	Invalid(TEXT("pause:1s"));
	Invalid(TEXT("pause:abc"));
	Invalid(TEXT("pause:NaN"));
	Invalid(TEXT("pause:inf"));
	Invalid(TEXT("pause:1 2"));
	Invalid(TEXT("pause:1:2"));
	Invalid(TEXT("pause 1"));
	Invalid(TEXT("pause"));
	Invalid(TEXT("pauses:1"));
	Invalid(TEXT("delay:1"));
	Invalid(TEXT("#pause:1"));
	Invalid(TEXT(""));
	Invalid(*(FString(TEXT("pause:")) + FString::ChrN(400, TEXT('9'))));

	// A line's tags: invalid pause tags are skipped and the last valid one wins.
	float seconds = -1.f;
	TestFalse(TEXT("no tags"), UInkpotProsettaLibrary::FindPauseSeconds({}, seconds));
	TestFalse(TEXT("unrelated tags"), UInkpotProsettaLibrary::FindPauseSeconds({ TEXT("speaker:QUEEN"), TEXT("mood: calm") }, seconds));
	TestTrue(TEXT("one pause among others"), UInkpotProsettaLibrary::FindPauseSeconds({ TEXT("speaker:QUEEN"), TEXT("wait: 0.75") }, seconds));
	TestEqual(TEXT("one pause seconds"), seconds, 0.75f);
	TestTrue(TEXT("last valid wins"), UInkpotProsettaLibrary::FindPauseSeconds({ TEXT("pause:1"), TEXT("WAIT:2"), TEXT("pause:-3"), TEXT("wait:oops") }, seconds));
	TestEqual(TEXT("last valid seconds"), seconds, 2.f);
	TestTrue(TEXT("later valid overrides earlier"), UInkpotProsettaLibrary::FindPauseSeconds({ TEXT("wait:5"), TEXT("pause:0") }, seconds));
	TestEqual(TEXT("zero override"), seconds, 0.f);
	seconds = 9.f;
	TestFalse(TEXT("only invalid pauses"), UInkpotProsettaLibrary::FindPauseSeconds({ TEXT("pause:-1"), TEXT("wait:") }, seconds));
	TestEqual(TEXT("only invalid leaves output alone"), seconds, 9.f);
	TestFalse(TEXT("null line"), UInkpotProsettaLibrary::GetLinePauseSeconds(nullptr, seconds));
	return true;
}

#endif // !UE_BUILD_SHIPPING
