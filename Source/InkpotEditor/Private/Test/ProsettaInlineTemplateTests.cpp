#if !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Asset/InkpotStoryAsset.h"
#include "Inkpot/Inkpot.h"
#include "Inkpot/InkpotChoice.h"
#include "Inkpot/InkpotLine.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/Prosetta/ProsettaInlineTemplate.h"
#include "Inkpot/Prosetta/ProsettaLine.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"

namespace ProsettaInlineTemplateTests
{
	constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter |
		EAutomationTestFlags::HighPriority;

	/** The first recovered value for an expression, or "<missing>". */
	FString ValueOf(const FProsettaTemplateValues& InValues, const TCHAR* InExpression)
	{
		const FProsettaTemplateValues::FEntry* entry = InValues.Find(FProsettaInlineTemplate::NormalizeExpressionKey(InExpression));
		return entry && entry->Values.Num() > 0 ? entry->Values[0] : FString(TEXT("<missing>"));
	}

	/** Extracts from the default template, then renders the given ( translated ) template. */
	FString Inject(const TCHAR* InDefault, const TCHAR* InRuntime, const TCHAR* InTranslation, bool* bOutResolved = nullptr)
	{
		FProsettaTemplateValues values;
		FProsettaInlineTemplate(InDefault).ExtractValues(InRuntime, values);
		FString out;
		const bool bResolved = FProsettaInlineTemplate(InTranslation).Render(&values, out);
		if (bOutResolved)
			*bOutResolved = bResolved;
		return out;
	}
}

// ---------------------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateParseTest, "Inkpot.Prosetta.InlineTemplate.Parse", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateParseTest::RunTest(const FString& InParameters)
{
	{
		const FProsettaInlineTemplate parsed(TEXT("I'm a math wiz, {2+2} equals four!"));
		TestTrue(TEXT("has expressions"), parsed.HasExpressions());
		const auto expressions = parsed.GetExpressions();
		if (TestEqual(TEXT("one expression"), expressions.Num(), 1))
			TestEqual(TEXT("expression text"), FString(expressions[0]), TEXT("2+2"));
	}

	{
		const FProsettaInlineTemplate parsed(TEXT("Plain dialogue."));
		TestFalse(TEXT("plain has no expressions"), parsed.HasExpressions());
		TestFalse(TEXT("plain pre-check"), FProsettaInlineTemplate::MayContainExpressions(TEXT("Plain dialogue.")));
	}

	// Quote-, escape- and nesting-aware placeholder boundaries.
	{
		const FProsettaInlineTemplate parsed(TEXT("A {\"}\"} B {'{'} C {x {y} z} D {a \\} b}"));
		const auto expressions = parsed.GetExpressions();
		if (TestEqual(TEXT("four expressions"), expressions.Num(), 4))
		{
			TestEqual(TEXT("double-quoted brace"), FString(expressions[0]), TEXT("\"}\""));
			TestEqual(TEXT("single-quoted brace"), FString(expressions[1]), TEXT("'{'"));
			TestEqual(TEXT("nested braces"), FString(expressions[2]), TEXT("x {y} z"));
			TestEqual(TEXT("escaped brace"), FString(expressions[3]), TEXT("a \\} b"));
		}
	}

	// Apostrophes within words are prose, not quotes.
	{
		const FProsettaInlineTemplate parsed(TEXT("{visited: You're back|Welcome}. {a: it's} and {b: don't}"));
		const auto expressions = parsed.GetExpressions();
		if (TestEqual(TEXT("contractions keep boundaries"), expressions.Num(), 3))
		{
			TestEqual(TEXT("contraction in conditional"), FString(expressions[0]), TEXT("visited: You're back|Welcome"));
			TestEqual(TEXT("contractions do not pair"), FString(expressions[2]), TEXT("b: don't"));
		}
	}

	// An unbalanced brace is literal text and renders unchanged.
	{
		const FProsettaInlineTemplate parsed(TEXT("Open { brace and {gold} gold"));
		const auto expressions = parsed.GetExpressions();
		if (TestEqual(TEXT("unbalanced brace skipped"), expressions.Num(), 1))
			TestEqual(TEXT("balanced expression kept"), FString(expressions[0]), TEXT("gold"));

		const FProsettaInlineTemplate unclosed(TEXT("Just an { opening"));
		FString out;
		TestTrue(TEXT("unclosed renders"), unclosed.Render(nullptr, out));
		TestEqual(TEXT("unclosed verbatim"), out, TEXT("Just an { opening"));
	}

	// Expression keys are trimmed with inner whitespace collapsed.
	TestEqual(TEXT("key normalisation"), FProsettaInlineTemplate::NormalizeExpressionKey(TEXT("  gold   *\t2 ")), TEXT("gold * 2"));
	{
		const FProsettaInlineTemplate parsed(TEXT("{  gold   * 2 }"));
		const auto expressions = parsed.GetExpressions();
		if (TestEqual(TEXT("padded expression"), expressions.Num(), 1))
			TestEqual(TEXT("padded expression trimmed"), FString(expressions[0]), TEXT("gold   * 2"));
	}

	// Plain-text projection used for alignment.
	TestEqual(TEXT("markup stripped"), FProsettaInlineTemplate::NormalizeToPlainText(TEXT("  <b>Hi</b>   there\n friend ")), TEXT("Hi there friend"));
	TestEqual(TEXT("bare angle brackets kept"), FProsettaInlineTemplate::NormalizeToPlainText(TEXT("hp < max > 0 <3 <>glue")), TEXT("hp < max > 0 <3 glue"));
	TestEqual(TEXT("lone angle bracket kept"), FProsettaInlineTemplate::NormalizeToPlainText(TEXT("a < b")), TEXT("a < b"));

	return true;
}

// ---------------------------------------------------------------------------------------
// Ink brace escapes: "\{" outside an expression is literal text
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateEscapeTest, "Inkpot.Prosetta.InlineTemplate.Escapes", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateEscapeTest::RunTest(const FString& InParameters)
{
	auto resolver = [](FStringView InName, FString& OutValue) -> bool
	{
		if (InName != TEXTVIEW("gold"))
			return false;
		OutValue = TEXT("7");
		return true;
	};

	// Ink prints "Type \{gold}" as "Type {gold}"; it is never a placeholder.
	{
		const FProsettaInlineTemplate parsed(TEXT("Type \\{gold}"));
		TestEqual(TEXT("escaped brace is not an expression"), parsed.NumExpressions(), 0);
		TestTrue(TEXT("escaped brace still needs rendering"), parsed.NeedsRendering());
		FString out;
		TestTrue(TEXT("escaped brace renders"), parsed.Render(nullptr, resolver, out));
		TestEqual(TEXT("escaped brace literal"), out, TEXT("Type {gold}"));
		TestFalse(TEXT("never the backslash and value"), out.Contains(TEXT("\\7")));
	}

	// Escapes sit alongside real placeholders, and an escaped backslash leaves the brace live.
	{
		FString out;
		TestTrue(TEXT("mixed renders"), FProsettaInlineTemplate(TEXT("\\{gold\\} costs {gold}")).Render(nullptr, resolver, out));
		TestEqual(TEXT("mixed text"), out, TEXT("{gold} costs 7"));
		TestTrue(TEXT("escaped backslash renders"), FProsettaInlineTemplate(TEXT("C:\\\\{gold}")).Render(nullptr, resolver, out));
		TestEqual(TEXT("escaped backslash then placeholder"), out, TEXT("C:\\7"));
		// Escapes inside an expression belong to the expression.
		TestEqual(TEXT("escape inside expression"), FString(FProsettaInlineTemplate(TEXT("{a \\} b}")).GetExpressions()[0]), TEXT("a \\} b"));
	}

	// Values are recovered against the unescaped literal text the runtime emits.
	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("extract with escapes"), FProsettaInlineTemplate(TEXT("Type \\{gold} for {gold} coins")).ExtractValues(TEXT("Type {gold} for 9 coins"), values));
		const FProsettaTemplateValues::FEntry* entry = values.Find(TEXT("gold"));
		TestTrue(TEXT("escaped template value"), entry && entry->Values.Num() == 1 && entry->Values[0] == TEXT("9"));
	}

	// The sidecar's authoritative string is unescaped even with no placeholders.
	{
		UProsettaMetadata* metadata = NewObject<UProsettaMetadata>();
		TestTrue(TEXT("loaded"), metadata->LoadFromJSON(TEXT(R"JSON({"lines":[{"id":"23-0001","lineId":"0001","string":"Type \\{gold} to pay"}]})JSON")));
		FString text;
		FProsettaTemplateValues values;
		TestTrue(TEXT("escaped line resolves"), metadata->ResolveDisplayText(FName(TEXT("0001")), TEXT("Type {gold} to pay"), resolver, text, values));
		TestEqual(TEXT("escaped line text"), text, TEXT("Type {gold} to pay"));
	}

	return true;
}

// ---------------------------------------------------------------------------------------
// Extraction: recovering placeholder values from the Ink runtime's evaluated text
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateExtractTest, "Inkpot.Prosetta.InlineTemplate.Extract", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateExtractTest::RunTest(const FString& InParameters)
{
	using namespace ProsettaInlineTemplateTests;

	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("math wiz extracted"), FProsettaInlineTemplate(TEXT("I'm a math wiz, {2+2} equals four!")).ExtractValues(TEXT("I'm a math wiz, 4 equals four!"), values));
		TestEqual(TEXT("2+2 value"), ValueOf(values, TEXT("2+2")), TEXT("4"));
	}

	// Several placeholders, with lookup keys normalised.
	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("multiple extracted"), FProsettaInlineTemplate(TEXT("{hero} has { gold } gold and {gold  *  2} silver.")).ExtractValues(TEXT("Ada has 7 gold and 14 silver."), values));
		TestEqual(TEXT("hero"), ValueOf(values, TEXT("hero")), TEXT("Ada"));
		TestEqual(TEXT("gold"), ValueOf(values, TEXT("gold")), TEXT("7"));
		TestEqual(TEXT("gold * 2"), ValueOf(values, TEXT("gold * 2")), TEXT("14"));
	}

	// Placeholders at the very start and end of the line.
	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("edges extracted"), FProsettaInlineTemplate(TEXT("{a} middle {b}")).ExtractValues(TEXT("first middle last"), values));
		TestEqual(TEXT("leading"), ValueOf(values, TEXT("a")), TEXT("first"));
		TestEqual(TEXT("trailing"), ValueOf(values, TEXT("b")), TEXT("last"));
	}

	// A value containing the following literal: lazy, then backtracking to the anchored end.
	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("backtracking extracted"), FProsettaInlineTemplate(TEXT("{a} and {b}!")).ExtractValues(TEXT("rock and roll and jazz!"), values));
		TestEqual(TEXT("lazy first"), ValueOf(values, TEXT("a")), TEXT("rock"));
		TestEqual(TEXT("greedy remainder"), ValueOf(values, TEXT("b")), TEXT("roll and jazz"));

		TestTrue(TEXT("value contains trailing literal"), FProsettaInlineTemplate(TEXT("{name} said hi")).ExtractValues(TEXT("said hi said hi"), values));
		TestEqual(TEXT("value equals literal"), ValueOf(values, TEXT("name")), TEXT("said hi"));
	}

	// Repeated expressions keep one value per occurrence.
	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("repeated extracted"), FProsettaInlineTemplate(TEXT("Roll {RANDOM(1,6)} then {RANDOM(1,6)}.")).ExtractValues(TEXT("Roll 2 then 5."), values));
		const FProsettaTemplateValues::FEntry* entry = values.Find(TEXT("RANDOM(1,6)"));
		if (TestNotNull(TEXT("repeated entry"), entry) && TestEqual(TEXT("two occurrences"), entry->Values.Num(), 2))
		{
			TestEqual(TEXT("first roll"), entry->Values[0], TEXT("2"));
			TestEqual(TEXT("second roll"), entry->Values[1], TEXT("5"));
		}
	}

	// An expression that emitted nothing ( e.g. a false conditional ) yields an empty value.
	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("empty emission extracted"), FProsettaInlineTemplate(TEXT("Before {flag: and} after")).ExtractValues(TEXT("Before  after"), values));
		TestEqual(TEXT("empty value"), ValueOf(values, TEXT("flag: and")), TEXT(""));
	}

	// Markup and whitespace differences are ignored for alignment.
	{
		FProsettaTemplateValues values;
		TestTrue(TEXT("markup extracted"), FProsettaInlineTemplate(TEXT("<b>Score:</b>   {score}\npoints")).ExtractValues(TEXT("Score: <i>42</i> points"), values));
		TestEqual(TEXT("value through markup"), ValueOf(values, TEXT("score")), TEXT("42"));
	}

	// Adjacent placeholders with no anchoring literal cannot be split.
	{
		FProsettaTemplateValues values;
		TestFalse(TEXT("adjacent ambiguous"), FProsettaInlineTemplate(TEXT("{a}{b} end")).ExtractValues(TEXT("12 end"), values));
		TestFalse(TEXT("space-separated ambiguous"), FProsettaInlineTemplate(TEXT("{a} {b} end")).ExtractValues(TEXT("1 2 end"), values));

		// The anchored neighbour is still recovered.
		TestTrue(TEXT("partially ambiguous"), FProsettaInlineTemplate(TEXT("x {a}{b} y {c}!")).ExtractValues(TEXT("x 12 y 3!"), values));
		TestEqual(TEXT("anchored value"), ValueOf(values, TEXT("c")), TEXT("3"));
		TestNull(TEXT("ambiguous skipped"), values.Find(TEXT("a")));

		// A key with an unrecovered occurrence keeps no values, so none shift onto it.
		TestTrue(TEXT("repeated partially ambiguous"), FProsettaInlineTemplate(TEXT("{r}{x} then {r} at {y}.")).ExtractValues(TEXT("3a then 5 at 9."), values));
		TestNull(TEXT("repeated key dropped"), values.Find(TEXT("r")));
		TestEqual(TEXT("other key kept"), ValueOf(values, TEXT("y")), TEXT("9"));
	}

	// Divergent text, unevaluated text and expression-free templates recover nothing.
	{
		FProsettaTemplateValues values;
		TestFalse(TEXT("prefix mismatch"), FProsettaInlineTemplate(TEXT("I'm a math wiz, {2+2} equals four!")).ExtractValues(TEXT("Something else entirely"), values));
		TestFalse(TEXT("suffix mismatch"), FProsettaInlineTemplate(TEXT("I'm a math wiz, {2+2} equals four!")).ExtractValues(TEXT("I'm a math wiz, 4 equals five!"), values));
		TestFalse(TEXT("runtime is the template"), FProsettaInlineTemplate(TEXT("I'm a math wiz, {2+2} equals four!")).ExtractValues(TEXT("I'm a math wiz, {2+2} equals four!"), values));
		TestFalse(TEXT("runtime is the template, respaced"), FProsettaInlineTemplate(TEXT("{gold * 2} silver")).ExtractValues(TEXT("{ gold  * 2 } silver"), values));
		TestFalse(TEXT("no expressions"), FProsettaInlineTemplate(TEXT("Plain")).ExtractValues(TEXT("Plain"), values));
		TestTrue(TEXT("values reset on failure"), values.IsEmpty());
	}

	// Pathological input is bounded by the alignment step budget.
	{
		FString pattern;
		for (int32 i = 0; i < 10; ++i)
			pattern += FString::Printf(TEXT("{v%d}x"), i);
		pattern += TEXT("{tail}y");
		const FString runtime = FString::ChrN(400, TEXT('x'));

		const double start = FPlatformTime::Seconds();
		FProsettaTemplateValues values;
		TestFalse(TEXT("pathological fails"), FProsettaInlineTemplate(pattern).ExtractValues(runtime, values));
		TestTrue(TEXT("pathological bounded"), FPlatformTime::Seconds() - start < 1.0);
	}

	return true;
}

// ---------------------------------------------------------------------------------------
// Rendering: injecting values into any language's template
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateRenderTest, "Inkpot.Prosetta.InlineTemplate.Render", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateRenderTest::RunTest(const FString& InParameters)
{
	using namespace ProsettaInlineTemplateTests;

	const TCHAR* mathWiz = TEXT("I'm a math wiz, {2+2} equals four!");
	const TCHAR* mathWizRuntime = TEXT("I'm a math wiz, 4 equals four!");

	TestEqual(TEXT("default language"), Inject(mathWiz, mathWizRuntime, mathWiz), mathWizRuntime);
	TestEqual(TEXT("alternate language"), Inject(mathWiz, mathWizRuntime, TEXT("Sono un genio della matematica, {2+2} fa quattro!")), TEXT("Sono un genio della matematica, 4 fa quattro!"));
	// Keys collapse whitespace but do not reinterpret the expression: "2 + 2" is not "2+2".
	TestEqual(TEXT("different expression text unresolved"), Inject(mathWiz, mathWizRuntime, TEXT("Je suis un as des maths, { 2 + 2 } font quatre !")), TEXT("Je suis un as des maths, { 2 + 2 } font quatre !"));
	TestEqual(TEXT("translation key collapsed whitespace"), Inject(TEXT("{gold  * 2} silver"), TEXT("14 silver"), TEXT("{gold * 2} argento")), TEXT("14 argento"));

	// Translations may reorder placeholders.
	TestEqual(TEXT("reordered"), Inject(TEXT("{hero} has {gold} gold and {gold * 2} silver."), TEXT("Ada has 7 gold and 14 silver."), TEXT("{gold} oro e {gold * 2} argento per {hero}.")), TEXT("7 oro e 14 argento per Ada."));

	// Repeated expressions consume values in order; extra occurrences clamp to the last.
	TestEqual(TEXT("occurrence order"), Inject(TEXT("{x} and {x}"), TEXT("1 and 2"), TEXT("{x}, {x}, {x}")), TEXT("1, 2, 2"));

	// Markup in the template is preserved; only placeholders are replaced.
	TestEqual(TEXT("markup preserved"), Inject(TEXT("<b>{score}</b> points"), TEXT("<b>42</b> points"), TEXT("<b>{score}</b> punti")), TEXT("<b>42</b> punti"));

	// Literals evaluate without a story.
	{
		FString out;
		TestTrue(TEXT("literals resolve"), FProsettaInlineTemplate(TEXT("{true} {false} [{null}] {42} {3.50} {-7} {\"q\"} {'a\\'b'}")).Render(nullptr, out));
		TestEqual(TEXT("literal values"), out, TEXT("true false [] 42 3.5 -7 q a'b"));
	}

	// Unresolvable placeholders are left verbatim and reported.
	{
		FString out;
		TestFalse(TEXT("call unresolved"), FProsettaInlineTemplate(TEXT("Score {calculateFinalScore()} now")).Render(nullptr, out));
		TestEqual(TEXT("call verbatim"), out, TEXT("Score {calculateFinalScore()} now"));
	}

	// Live variables, preferring recovered values.
	{
		TMap<FString, FString> variables;
		variables.Add(TEXT("hero"), TEXT("Ada"));
		variables.Add(TEXT("gold"), TEXT("9"));
		variables.Add(TEXT("greeting"), TEXT("Hello {hero}"));
		variables.Add(TEXT("loop"), TEXT("{loop}"));
		auto resolver = [&variables](FStringView InName, FString& OutValue) -> bool
		{
			if (const FString* value = variables.Find(FString(InName)))
			{
				OutValue = *value;
				return true;
			}
			return false;
		};

		FString out;
		TestTrue(TEXT("variables resolve"), FProsettaInlineTemplate(TEXT("{hero} has {gold} gold")).Render(nullptr, resolver, out));
		TestEqual(TEXT("variable values"), out, TEXT("Ada has 9 gold"));

		FProsettaTemplateValues values;
		values.Add(TEXT("gold"), TEXT("7"));
		TestTrue(TEXT("recovered preferred"), FProsettaInlineTemplate(TEXT("{hero} has {gold} gold")).Render(&values, resolver, out));
		TestEqual(TEXT("recovered value wins"), out, TEXT("Ada has 7 gold"));

		// Ink itself never re-renders a value, but the Prosetta web player does
		// ( renderRuntimeValue ), so live string values that are templates nest for parity.
		TestTrue(TEXT("nested template"), FProsettaInlineTemplate(TEXT("{greeting}!")).Render(nullptr, resolver, out));
		TestEqual(TEXT("nested rendered"), out, TEXT("Hello Ada!"));
		TestTrue(TEXT("nested string literal"), FProsettaInlineTemplate(TEXT("{\"{hero}\"}!")).Render(nullptr, resolver, out));
		TestEqual(TEXT("string literal rendered"), out, TEXT("Ada!"));

		// Recovered values are the runtime's final text and are never re-rendered.
		FProsettaTemplateValues frozen;
		frozen.Add(TEXT("greeting"), TEXT("Hi {hero}"));
		TestTrue(TEXT("recovered value final"), FProsettaInlineTemplate(TEXT("{greeting}!")).Render(&frozen, resolver, out));
		TestEqual(TEXT("recovered value not nested"), out, TEXT("Hi {hero}!"));

		TestFalse(TEXT("self-reference unresolved"), FProsettaInlineTemplate(TEXT("{loop}")).Render(nullptr, resolver, out));
		TestEqual(TEXT("self-reference terminates"), out, TEXT("{loop}"));

		// A nested template's unresolved placeholder fails the whole line.
		variables.Add(TEXT("partial"), TEXT("Hello {title} {hero}"));
		TestFalse(TEXT("nested unresolved reported"), FProsettaInlineTemplate(TEXT("{partial}!")).Render(nullptr, resolver, out));
		TestEqual(TEXT("nested unresolved verbatim"), out, TEXT("Hello {title} Ada!"));

		TestFalse(TEXT("unknown variable"), FProsettaInlineTemplate(TEXT("{missing} {hero}")).Render(nullptr, resolver, out));
		TestEqual(TEXT("unknown verbatim"), out, TEXT("{missing} Ada"));
	}

	// Expression-free text takes the fast path.
	{
		FString out;
		TestTrue(TEXT("fast path"), FProsettaInlineTemplate(TEXT("No placeholders here.")).Render(nullptr, out));
		TestEqual(TEXT("fast path verbatim"), out, TEXT("No placeholders here."));
	}

	return true;
}

// ---------------------------------------------------------------------------------------
// Render-only templates skip the alignment ExtractValues needs
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateRenderOnlyTest, "Inkpot.Prosetta.InlineTemplate.RenderOnly", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateRenderOnlyTest::RunTest(const FString& InParameters)
{
	using namespace ProsettaInlineTemplateTests;

	const TCHAR* source = TEXT("<b>{hero}</b> has {gold} gold and {gold * 2} silver.");
	const TCHAR* runtime = TEXT("<b>Ada</b> has 7 gold and 14 silver.");
	const FProsettaInlineTemplate aligned(source);
	const FProsettaInlineTemplate renderOnly(source, FProsettaInlineTemplate::EUsage::RenderOnly);

	TestTrue(TEXT("default usage aligns up front"), aligned.IsAligned());
	TestFalse(TEXT("render-only skips the alignment"), renderOnly.IsAligned());
	TestFalse(TEXT("plain text never aligns"), FProsettaInlineTemplate(TEXT("Plain.")).IsAligned());
	TestEqual(TEXT("render-only parses the same placeholders"), renderOnly.NumExpressions(), aligned.NumExpressions());

	FProsettaTemplateValues values;
	values.Add(TEXT("hero"), TEXT("Ada"));
	values.Add(TEXT("gold"), TEXT("7"));
	values.Add(TEXT("gold * 2"), TEXT("14"));
	FString alignedOut, renderOnlyOut;
	TestTrue(TEXT("aligned renders"), aligned.Render(&values, alignedOut));
	TestTrue(TEXT("render-only renders"), renderOnly.Render(&values, renderOnlyOut));
	TestEqual(TEXT("render output identical"), renderOnlyOut, alignedOut);

	// ExtractValues still works on a render-only template, building the alignment per call.
	FProsettaTemplateValues extracted;
	TestTrue(TEXT("render-only still extracts"), renderOnly.ExtractValues(runtime, extracted));
	TestEqual(TEXT("render-only hero"), ValueOf(extracted, TEXT("hero")), TEXT("Ada"));
	TestEqual(TEXT("render-only gold * 2"), ValueOf(extracted, TEXT("gold * 2")), TEXT("14"));
	TestFalse(TEXT("extracting leaves it render-only"), renderOnly.IsAligned());
	TestFalse(TEXT("render-only divergence fails"), renderOnly.ExtractValues(TEXT("Something else"), extracted));

	// Nested values ( rendered render-only ) still resolve recursively.
	auto resolver = [](FStringView InName, FString& OutValue) -> bool
	{
		if (InName == TEXTVIEW("greeting")) { OutValue = TEXT("Hello {hero}"); return true; }
		if (InName == TEXTVIEW("hero")) { OutValue = TEXT("Ada"); return true; }
		return false;
	};
	FString nested;
	TestTrue(TEXT("nested render-only resolves"), FProsettaInlineTemplate(TEXT("{greeting}!"), FProsettaInlineTemplate::EUsage::RenderOnly).Render(nullptr, resolver, nested));
	TestEqual(TEXT("nested render-only output"), nested, TEXT("Hello Ada!"));
	return true;
}

// ---------------------------------------------------------------------------------------
// Metadata: authoritative display text for a line
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateMetadataTest, "Inkpot.Prosetta.InlineTemplate.Metadata", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateMetadataTest::RunTest(const FString& InParameters)
{
	UProsettaMetadata* metadata = NewObject<UProsettaMetadata>();
	TestTrue(TEXT("loaded"), metadata->LoadFromJSON(TEXT(R"JSON(
	{"lines":[
		{"id":"23-0001","lineId":"0001","string":"I'm a math wiz, {2+2} equals four!"},
		{"id":"23-0002","lineId":"0002","string":"Your score: {calculateFinalScore()}"},
		{"id":"23-0003","lineId":"0003","string":"No expressions."}
	]}
	)JSON")));

	TestNotNull(TEXT("template cached"), metadata->FindTemplate(FName(TEXT("0001"))));
	TestNull(TEXT("plain lines not cached"), metadata->FindTemplate(FName(TEXT("0003"))));

	auto noVariables = [](FStringView, FString&) { return false; };

	FString text;
	FProsettaTemplateValues values;
	TestTrue(TEXT("evaluated"), metadata->ResolveDisplayText(FName(TEXT("0001")), TEXT("I'm a math wiz, 4 equals four!"), noVariables, text, values));
	TestEqual(TEXT("evaluated text"), text, TEXT("I'm a math wiz, 4 equals four!"));
	TestEqual(TEXT("values returned"), values.Num(), 1);

	// A function call recovered from the runtime text.
	TestTrue(TEXT("call recovered"), metadata->ResolveDisplayText(FName(TEXT("0002")), TEXT("Your score: 88"), noVariables, text, values));
	TestEqual(TEXT("call text"), text, TEXT("Your score: 88"));

	// Unrecoverable: the caller keeps the runtime text rather than showing "{...}".
	TestFalse(TEXT("unrecoverable"), metadata->ResolveDisplayText(FName(TEXT("0002")), TEXT("Different words"), noVariables, text, values));

	TestTrue(TEXT("plain line"), metadata->ResolveDisplayText(FName(TEXT("0003")), TEXT("ignored"), noVariables, text, values));
	TestEqual(TEXT("plain verbatim"), text, TEXT("No expressions."));

	TestFalse(TEXT("unknown line"), metadata->ResolveDisplayText(FName(TEXT("ZZZZ")), TEXT("x"), noVariables, text, values));

	return true;
}

// ---------------------------------------------------------------------------------------
// UProsettaLine: emission-time variable snapshot and language code matching
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateLineSnapshotTest, "Inkpot.Prosetta.InlineTemplate.LineSnapshot", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateLineSnapshotTest::RunTest(const FString& InParameters)
{
	TMap<FString, FString> variables;
	variables.Add(TEXT("hero"), TEXT("Ada"));
	variables.Add(TEXT("title"), TEXT("Sir"));
	variables.Add(TEXT("mood"), TEXT("glad"));
	variables.Add(TEXT("greeting"), TEXT("Hi {nick}"));
	variables.Add(TEXT("nick"), TEXT("Addy"));
	auto resolver = [&variables](FStringView InName, FString& OutValue) -> bool
	{
		if (const FString* value = variables.Find(FString(InName)))
		{
			OutValue = *value;
			return true;
		}
		return false;
	};

	FProsettaLineMetadata metadata;
	metadata.LineId = FName(TEXT("0001"));
	// Adjacent placeholders cannot be split from the runtime text, so nothing is recovered.
	metadata.String = TEXT("{hero}{title} waves.");
	metadata.Localizations.Add(TEXT("it"), TEXT("{title}{hero} saluta, {mood}."));
	metadata.Localizations.Add(TEXT("pt-BR"), TEXT("{hero} acena. {greeting}"));
	metadata.Localizations.Add(TEXT("fr"), TEXT("{hero} salue."));
	metadata.Localizations.Add(TEXT("de"), TEXT(""));

	UProsettaLine* line = NewObject<UProsettaLine>();
	line->Initialise(metadata.LineId, TEXT("AdaSir waves."), {}, 0, 13);
	line->SetMetadata(metadata, true);
	FProsettaTemplateValues values;
	FProsettaInlineTemplate(metadata.String).ExtractValues(TEXT("AdaSir waves."), values);
	TestTrue(TEXT("adjacent placeholders not recovered"), values.IsEmpty());
	line->SetRuntimeText(TEXT("AdaSir waves."), MoveTemp(values));
	line->CaptureVariables(resolver);

	// The story moves on before the line is re-presented in another language.
	variables[TEXT("hero")] = TEXT("Bea");
	variables[TEXT("title")] = TEXT("Dame");
	variables[TEXT("mood")] = TEXT("cross");
	variables[TEXT("nick")] = TEXT("Bee");

	FString localized;
	TestTrue(TEXT("adjacent and translation-only"), line->GetLocalizedText(TEXT("it"), localized));
	TestEqual(TEXT("emission-time values"), localized, TEXT("SirAda saluta, glad."));
	TestTrue(TEXT("nested template variables"), line->GetLocalizedText(TEXT("pt-BR"), localized));
	TestEqual(TEXT("nested emission-time values"), localized, TEXT("Ada acena. Hi Addy"));

	// Language codes match regardless of case and separator, then fall back to the base language.
	TestTrue(TEXT("underscore and case"), line->GetLocalizedText(TEXT("pt_br"), localized));
	TestEqual(TEXT("underscore and case text"), localized, TEXT("Ada acena. Hi Addy"));
	TestTrue(TEXT("upper case"), line->GetLocalizedText(TEXT("IT"), localized));
	TestEqual(TEXT("upper case text"), localized, TEXT("SirAda saluta, glad."));
	TestTrue(TEXT("base language"), line->GetLocalizedText(TEXT("fr-CA"), localized));
	TestEqual(TEXT("base language text"), localized, TEXT("Ada salue."));
	TestTrue(TEXT("FindLocalization base"), line->FindLocalization(TEXT("fr_ca")) != nullptr);
	TestFalse(TEXT("empty translation"), line->GetLocalizedText(TEXT("de-AT"), localized));
	TestEqual(TEXT("empty translation falls back"), localized, TEXT("AdaSir waves."));
	TestFalse(TEXT("unknown language"), line->GetLocalizedText(TEXT("ja"), localized));
	TestEqual(TEXT("normalised code"), UProsettaLine::NormalizeLanguageCode(TEXT(" PT_br ")), TEXT("pt-br"));

	return true;
}

// ---------------------------------------------------------------------------------------
// End to end: compiled Ink + Prosetta sidecar through UInkpotStory
// ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateStoryTest, "Inkpot.Prosetta.InlineTemplate.Story", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateStoryTest::RunTest(const FString& InParameters)
{
	/*
	 * Compiled with inklecate from:
	 *
	 *   VAR gold = 7
	 *   VAR hero = "Ada"
	 *   <prosetta id="0001">I'm a math wiz, {2+2} equals four!</prosetta>
	 *   <prosetta id="0002">{hero} has {gold} gold and {gold * 2} silver.</prosetta>
	 *   <prosetta id="0005">Twice is {double(gold)}, {&tick|tock}.</prosetta>
	 *   * <prosetta id="0003">Pay {gold - 2} coins</prosetta>
	 *     ~ gold = gold - 2
	 *     <prosetta id="0004">{hero} now has {gold} gold.</prosetta>
	 *     -> END
	 *
	 *   === function double(x)
	 *   ~ return x * 2
	 */
	const FString compiled = TEXT(R"JSON({"inkVersion":21,"root":[["^<prosetta id=\"0001\">I'm a math wiz, ","ev",2,2,"+","out","/ev","^ equals four!</prosetta>","\n","^<prosetta id=\"0002\">","ev",{"VAR?":"hero"},"out","/ev","^ has ","ev",{"VAR?":"gold"},"out","/ev","^ gold and ","ev",{"VAR?":"gold"},2,"*","out","/ev","^ silver.</prosetta>","\n","^<prosetta id=\"0005\">Twice is ","ev",{"VAR?":"gold"},{"f()":"double"},"out","/ev","^, ",["ev","visit",2,"%","/ev","ev","du",0,"==","/ev",{"->":".^.s0","c":true},"ev","du",1,"==","/ev",{"->":".^.s1","c":true},"nop",{"s0":["pop","^tick",{"->":"0.35.17"},null],"s1":["pop","^tock",{"->":"0.35.17"},null],"#f":5}],"^.</prosetta>","\n",["ev",{"^->":"0.38.$r1"},{"temp=":"$r"},"str",{"->":".^.s"},[{"#n":"$r1"}],"/str","/ev",{"*":"0.c-0","flg":18},{"s":["^<prosetta id=\"0003\">Pay ","ev",{"VAR?":"gold"},2,"-","out","/ev","^ coins</prosetta>",{"->":"$r","var":true},null]}],{"c-0":["ev",{"^->":"0.c-0.$r2"},"/ev",{"temp=":"$r"},{"->":"0.38.s"},[{"#n":"$r2"}],"\n","ev",{"VAR?":"gold"},2,"-","/ev",{"VAR=":"gold","re":true},"^<prosetta id=\"0004\">","ev",{"VAR?":"hero"},"out","/ev","^ now has ","ev",{"VAR?":"gold"},"out","/ev","^ gold.</prosetta>","\n","end",{"->":"0.g-0"},{"#f":5}],"g-0":["done",null]}],"done",{"double":[{"temp=":"x"},"ev",{"VAR?":"x"},2,"*","/ev","~ret",null],"global decl":["ev",7,{"VAR=":"gold"},"str","^Ada","/str",{"VAR=":"hero"},"/ev","end",null]}],"listDefs":{}})JSON");

	// Engine Sync array form: database "id" alongside the tag's "lineId".
	const FString sidecar = TEXT(R"JSON(
	{"lines":[
		{"id":"23-0001","lineId":"0001","string":"I'm a math wiz, {2+2} equals four!",
			"localizations":{"it":"Sono un genio della matematica, {2+2} fa quattro!","metl":"{hero} says {2+2}","de":"{gold} Gold, {2+2}"}},
		{"id":"23-0002","lineId":"0002","string":"{hero} has {gold} gold and {gold * 2} silver.",
			"localizations":{"it":"{gold} oro e {gold * 2} argento per {hero}."}},
		{"id":"23-0005","lineId":"0005","string":"Twice is {double(gold)}, {&tick|tock}.",
			"localizations":{"it":"Il doppio fa {double(gold)}, {&tick|tock}."}},
		{"id":"23-0003","lineId":"0003","string":"Pay {gold - 2} coins",
			"localizations":{"it":"Paga {gold - 2} monete"}},
		{"id":"23-0004","lineId":"0004","string":"{hero} now has {gold} gold.",
			"localizations":{"it":"{hero} ora ha {gold} oro.","ko":"{hero}: {gold} / {hero}"}}
	]}
	)JSON");

	UInkpot* inkpot = GEngine ? GEngine->GetEngineSubsystem<UInkpot>() : nullptr;
	if (!TestNotNull(TEXT("inkpot subsystem"), inkpot))
		return false;

	UInkpotStoryAsset* asset = NewObject<UInkpotStoryAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	asset->SetCompiledJSON(compiled);
	asset->SetProsettaJSON(sidecar);

	UInkpotStory* story = inkpot->BeginStory(asset);
	if (!TestNotNull(TEXT("story began"), story) || !TestNotNull(TEXT("sidecar attached"), story->GetProsettaMetadata()))
	{
		if (story)
			inkpot->EndStory(story);
		return false;
	}

	auto ContinueTo = [this, story](const TCHAR* InLineId) -> UProsettaLine*
	{
		while (story->CanContinue())
		{
			story->Continue();
			UInkpotLine* line = story->GetCurrentLine();
			const TArray<UProsettaLine*> segments = line->GetProsettaSegments();
			if (segments.Num() > 0 && segments[0]->GetLineId() == FName(InLineId))
			{
				TestEqual(FString::Printf(TEXT("%s line text matches segment"), InLineId), line->GetString().TrimStartAndEnd(), segments[0]->GetText());
				return segments[0];
			}
		}
		AddError(FString::Printf(TEXT("Line %s was not reached"), InLineId));
		return nullptr;
	};

	FString localized;
	UProsettaLine* mathWiz = nullptr;

	// The bug: the sidecar string replaced the evaluated text, so "{2+2}" reached the UI.
	if (UProsettaLine* line = ContinueTo(TEXT("0001")))
	{
		mathWiz = line;
		TestTrue(TEXT("0001 resolved"), line->IsResolved());
		TestEqual(TEXT("0001 default language"), line->GetText(), TEXT("I'm a math wiz, 4 equals four!"));
		TestEqual(TEXT("0001 runtime text"), line->GetRuntimeText(), TEXT("I'm a math wiz, 4 equals four!"));
		FString value;
		TestTrue(TEXT("0001 value exposed"), line->GetTemplateValue(TEXT("2+2"), value));
		TestEqual(TEXT("0001 value"), value, TEXT("4"));
		TestTrue(TEXT("0001 it"), line->GetLocalizedText(TEXT("it"), localized));
		TestEqual(TEXT("0001 alternate language"), localized, TEXT("Sono un genio della matematica, 4 fa quattro!"));
		// A translation may reference story state the default string does not.
		TestTrue(TEXT("0001 metl"), line->GetLocalizedText(TEXT("metl"), localized));
		TestEqual(TEXT("0001 live variable"), localized, TEXT("Ada says 4"));
		// Missing languages fall back to the evaluated default.
		TestFalse(TEXT("0001 fr missing"), line->GetLocalizedText(TEXT("fr"), localized));
		TestEqual(TEXT("0001 fallback"), localized, TEXT("I'm a math wiz, 4 equals four!"));
	}

	if (UProsettaLine* line = ContinueTo(TEXT("0002")))
	{
		TestEqual(TEXT("0002 default language"), line->GetText(), TEXT("Ada has 7 gold and 14 silver."));
		TestTrue(TEXT("0002 it"), line->GetLocalizedText(TEXT("it"), localized));
		TestEqual(TEXT("0002 reordered translation"), localized, TEXT("7 oro e 14 argento per Ada."));
	}

	// Function calls and sequences can only be known from what the runtime emitted.
	if (UProsettaLine* line = ContinueTo(TEXT("0005")))
	{
		TestEqual(TEXT("0005 default language"), line->GetText(), TEXT("Twice is 14, tick."));
		TestTrue(TEXT("0005 it"), line->GetLocalizedText(TEXT("it"), localized));
		TestEqual(TEXT("0005 alternate language"), localized, TEXT("Il doppio fa 14, tick."));
	}

	// Choices are hydrated through the same path.
	const TArray<UInkpotChoice*>& choices = story->GetCurrentChoices();
	if (TestEqual(TEXT("one choice"), choices.Num(), 1))
	{
		UInkpotChoice* choice = choices[0];
		TestEqual(TEXT("choice default language"), choice->GetString(), TEXT("Pay 5 coins"));
		const TArray<UProsettaLine*> segments = choice->GetProsettaSegments();
		if (TestEqual(TEXT("choice segment"), segments.Num(), 1))
		{
			TestTrue(TEXT("choice it"), segments[0]->GetLocalizedText(TEXT("it"), localized));
			TestEqual(TEXT("choice alternate language"), localized, TEXT("Paga 5 monete"));
		}
		story->ChooseChoice(choice);
	}

	// Values reflect story state at emission time.
	if (UProsettaLine* line = ContinueTo(TEXT("0004")))
	{
		TestEqual(TEXT("0004 default language"), line->GetText(), TEXT("Ada now has 5 gold."));
		TestTrue(TEXT("0004 it"), line->GetLocalizedText(TEXT("it"), localized));
		TestEqual(TEXT("0004 alternate language"), localized, TEXT("Ada ora ha 5 oro."));
		TestTrue(TEXT("0004 ko"), line->GetLocalizedText(TEXT("ko"), localized));
		TestEqual(TEXT("0004 repeated placeholder"), localized, TEXT("Ada: 5 / Ada"));
	}

	// A line re-presented after the story moved on ( gold is now 5 ) keeps its
	// emission-time value for a variable only its translation references.
	if (mathWiz)
	{
		TestTrue(TEXT("0001 de after choice"), mathWiz->GetLocalizedText(TEXT("de"), localized));
		TestEqual(TEXT("0001 emission-time variable"), localized, TEXT("7 Gold, 4"));
	}

	inkpot->EndStory(story);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaInlineTemplateThroughputTest, "Inkpot.Prosetta.InlineTemplate.Throughput", ProsettaInlineTemplateTests::Flags)

bool FProsettaInlineTemplateThroughputTest::RunTest(const FString& InParameters)
{
	// Per-line cost of the hot path: extract from the default template, render a translation.
	const FProsettaInlineTemplate source(TEXT("<b>{hero}</b> has {gold} gold and {gold * 2} silver, {&tick|tock}."));
	const FProsettaInlineTemplate translation(TEXT("<b>{hero}</b> ha {gold} oro e {gold * 2} argento, {&tick|tock}."));
	const FProsettaInlineTemplate plain(TEXT("A line without any inline logic at all, as most lines are."));
	const FString runtime = TEXT("<b>Ada</b> has 7 gold and 14 silver, tick.");

	constexpr int32 Iterations = 20000;
	FProsettaTemplateValues values;
	FString rendered;
	bool bAllResolved = true;
	const double start = FPlatformTime::Seconds();
	for (int32 i = 0; i < Iterations; ++i)
	{
		values.Reset();
		rendered.Reset();
		bAllResolved &= source.ExtractValues(runtime, values) && translation.Render(&values, rendered);
	}
	const double templated = FPlatformTime::Seconds() - start;

	const double plainStart = FPlatformTime::Seconds();
	for (int32 i = 0; i < Iterations; ++i)
	{
		rendered.Reset();
		bAllResolved &= plain.Render(nullptr, rendered);
	}
	const double untemplated = FPlatformTime::Seconds() - plainStart;

	TestTrue(TEXT("every iteration resolved"), bAllResolved);
	TestEqual(TEXT("rendered translation"), rendered, FString(TEXT("A line without any inline logic at all, as most lines are.")));
	AddInfo(FString::Printf(TEXT("Extract+render: %.2f us/line; plain render: %.3f us/line"), templated * 1e6 / Iterations, untemplated * 1e6 / Iterations));
	// Generous ceiling so slow CI hosts pass, while catching accidental quadratic work.
	TestTrue(TEXT("templated lines stay in the microsecond range"), templated / Iterations < 50e-6);
	return true;
}

#endif // !UE_BUILD_SHIPPING
