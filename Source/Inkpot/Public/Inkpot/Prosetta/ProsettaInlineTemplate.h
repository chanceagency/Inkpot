#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"
#include "Templates/Function.h"

class UInkpotStory;

/**
 * FProsettaTemplateValues
 * Runtime values recovered for the {expression} placeholders of a Prosetta line
 * template, keyed by normalised expression text ( trimmed, whitespace collapsed ).
 * Repeated expressions keep one value per occurrence, in source order.
 */
struct INKPOT_API FProsettaTemplateValues
{
	struct FEntry
	{
		FString Key;
		TArray<FString, TInlineAllocator<1>> Values;
	};

	/** Appends a value for the given normalised key. */
	void Add(FStringView InKey, FString InValue);

	/** The values recorded for a normalised key, or nullptr when there are none. */
	const FEntry* Find(FStringView InKey) const;

	int32 Num() const { return Entries.Num(); }
	bool IsEmpty() const { return Entries.IsEmpty(); }
	void Reset() { Entries.Reset(); }

	const TArray<FEntry, TInlineAllocator<4>>& GetEntries() const { return Entries; }

private:
	TArray<FEntry, TInlineAllocator<4>> Entries;
};

/**
 * FProsettaInlineTemplate
 * A Prosetta line string ( from the sidecar line tables or a translation ) parsed
 * into literal text and inline Ink {expression} placeholders, e.g.
 *     "I'm a math wiz, {2+2} equals four!"
 *
 * Mirrors the Prosetta web "Inline Ink Player":
 *  - ExtractValues aligns the default-language template against the text the Ink
 *    runtime actually emitted for the line, recovering the value each placeholder
 *    produced at emission time ( so arbitrary Ink expressions, conditionals,
 *    sequences and function calls are all supported ).
 *  - Render injects those values into any language's template, falling back to
 *    live evaluation of literals and story variables for placeholders that were
 *    not recovered ( e.g. a translation that references a different variable ).
 *
 * Parsing is a single allocation-light scan; templates without a '{' take a fast
 * path and are returned unchanged.
 */
class INKPOT_API FProsettaInlineTemplate
{
public:
	/** Resolves a variable name to its display text. Returns false when unknown. */
	using FVariableResolver = TFunctionRef<bool(FStringView /*VariableName*/, FString& /*OutText*/)>;

	/** What a parsed template will be used for. */
	enum class EUsage : uint8
	{
		/** ExtractValues and Render: the alignment is built once, up front ( cached templates ). */
		ExtractAndRender,
		/** Render only: no alignment is built; ExtractValues still works but builds it per call. */
		RenderOnly,
	};

	FProsettaInlineTemplate() = default;
	explicit FProsettaInlineTemplate(FString InSource, EUsage InUsage = EUsage::ExtractAndRender);

	/** Cheap pre-check: true when the text could contain an {expression}. */
	static bool MayContainExpressions(FStringView InText);

	/** Canonical lookup key for an expression: trimmed, inner whitespace collapsed. */
	static FString NormalizeExpressionKey(FStringView InExpression);

	/** Strips <markup>, collapses whitespace runs to one space and trims. */
	static FString NormalizeToPlainText(FStringView InText);

	/** Resolves a variable against the story's current variable state. */
	static bool ResolveStoryVariable(UInkpotStory* InStory, FStringView InName, FString& OutText);

	const FString& GetSource() const { return Source; }
	bool HasExpressions() const { return ExpressionCount > 0; }
	int32 NumExpressions() const { return ExpressionCount; }

	/** True when the alignment ExtractValues uses was built at construction. */
	bool IsAligned() const { return bAligned; }

	/** The trimmed text of each expression placeholder, in source order. */
	TArray<FStringView, TInlineAllocator<8>> GetExpressions() const;

	/**
	 * ExtractValues
	 * Recovers the value each placeholder produced by aligning this ( default
	 * language ) template with the Ink runtime's evaluated text for the line.
	 * Placeholders with no anchoring literal between them are ambiguous and skipped.
	 * Returns false when nothing could be recovered ( no placeholders, the texts
	 * diverged, or the runtime text is itself unevaluated template text ).
	 */
	bool ExtractValues(FStringView InRuntimeText, FProsettaTemplateValues& OutValues) const;

	/**
	 * Render
	 * Writes the template with each placeholder replaced by its recovered value,
	 * else its live value ( literal or story variable ). Unresolvable placeholders
	 * are written verbatim as {expression}.
	 * Returns true when every placeholder was resolved.
	 */
	bool Render(const FProsettaTemplateValues* InValues, FVariableResolver InResolver, FString& OutText) const;

	/** Render without live evaluation, using only recovered values and literals. */
	bool Render(const FProsettaTemplateValues* InValues, FString& OutText) const;

private:
	struct FSegment
	{
		int32 Start = 0;
		int32 Len = 0;
		bool bExpression = false;
	};

	/** The template projected to plain text: literals between surviving placeholders. */
	struct FAlignment
	{
		TArray<FString, TInlineAllocator<8>> Literals;
		TArray<int32, TInlineAllocator<8>> ExpressionIndices;
	};

	void Parse();
	void BuildAlignment(FAlignment& OutAlignment) const;
	FStringView SegmentView(const FSegment& InSegment) const;
	bool RenderInternal(const FProsettaTemplateValues* InValues, const FVariableResolver* InResolver, int32 InDepth, FString& OutText) const;

	FString Source;
	TArray<FSegment, TInlineAllocator<8>> Segments;
	TArray<FString, TInlineAllocator<8>> ExpressionKeys;
	FAlignment Alignment;
	int32 ExpressionCount = 0;
	bool bAligned = false;
};
