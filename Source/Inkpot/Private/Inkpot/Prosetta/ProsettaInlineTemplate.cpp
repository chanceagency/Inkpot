#include "Inkpot/Prosetta/ProsettaInlineTemplate.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/InkpotStoryInternal.h"
#include "Ink/Value.h"
#include "Ink/VariableState.h"
#include "String/Find.h"

namespace
{
	// Private-use codepoints cannot appear in authored prose, so placeholders can be
	// swapped for sentinels before markup stripping without risk of collision.
	constexpr TCHAR GSentinelStart = TEXT('');
	constexpr TCHAR GSentinelEnd = TEXT('');

	// Nested template rendering ( a variable whose value is itself a template ).
	constexpr int32 GMaxRecursionDepth = 12;

	// Upper bound on literal alignment attempts, guarding pathological inputs.
	constexpr int32 GMaxAlignmentSteps = 16384;

	struct FRawSegment
	{
		int32 Start = 0;      // literal: text start. expression: trimmed expression start
		int32 Len = 0;        // literal: text length. expression: trimmed expression length
		int32 OuterStart = 0; // expression only: the '{'
		int32 OuterLen = 0;   // expression only: through the matching '}'
		bool bExpression = false;
	};

	using FRawSegments = TArray<FRawSegment, TInlineAllocator<8>>;

	FStringView TrimView(FStringView InView)
	{
		return InView.TrimStartAndEnd();
	}

	/**
	 * Splits text into literal and {expression} segments with a quote-aware,
	 * escape-aware, nested-brace-aware scan. An unbalanced '{' stays literal.
	 * Mirrors splitTemplateExpressionSegments in the Prosetta web player so the
	 * two can never disagree on placeholder boundaries.
	 */
	void SplitSegments(FStringView InText, FRawSegments& OutSegments)
	{
		const int32 len = InText.Len();
		int32 literalStart = 0;
		int32 index = 0;

		auto FlushLiteral = [&](int32 InEnd)
		{
			if (InEnd > literalStart)
				OutSegments.Add({ literalStart, InEnd - literalStart, 0, 0, false });
		};

		while (index < len)
		{
			if (InText[index] != TEXT('{'))
			{
				++index;
				continue;
			}

			int32 cursor = index + 1;
			int32 braceDepth = 1;
			bool bInSingleQuote = false;
			bool bInDoubleQuote = false;
			bool bEscaped = false;

			while (cursor < len)
			{
				const TCHAR next = InText[cursor++];
				if (bEscaped)
				{
					bEscaped = false;
					continue;
				}
				if (next == TEXT('\\'))
				{
					bEscaped = true;
					continue;
				}
				if (bInSingleQuote)
				{
					bInSingleQuote = next != TEXT('\'');
					continue;
				}
				if (bInDoubleQuote)
				{
					bInDoubleQuote = next != TEXT('"');
					continue;
				}
				if (next == TEXT('\''))
					bInSingleQuote = true;
				else if (next == TEXT('"'))
					bInDoubleQuote = true;
				else if (next == TEXT('{'))
					++braceDepth;
				else if (next == TEXT('}') && --braceDepth == 0)
					break;
			}

			if (braceDepth != 0)
			{
				// unbalanced, the '{' is literal text
				++index;
				continue;
			}

			FlushLiteral(index);

			const FStringView inner = InText.Mid(index + 1, cursor - index - 2);
			const FStringView trimmed = TrimView(inner);
			const int32 trimmedStart = trimmed.IsEmpty() ? index + 1 : static_cast<int32>(trimmed.GetData() - InText.GetData());
			OutSegments.Add({ trimmedStart, trimmed.Len(), index, cursor - index, true });

			index = cursor;
			literalStart = cursor;
		}

		FlushLiteral(len);
	}

	void AppendNormalizedKey(FStringView InExpression, FString& OutKey)
	{
		bool bPendingSpace = false;
		for (const TCHAR ch : TrimView(InExpression))
		{
			if (FChar::IsWhitespace(ch))
			{
				bPendingSpace = true;
				continue;
			}
			if (bPendingSpace)
			{
				OutKey.AppendChar(TEXT(' '));
				bPendingSpace = false;
			}
			OutKey.AppendChar(ch);
		}
	}

	/** Compares an expression against a normalised key without allocating. */
	bool KeyEquals(FStringView InExpression, FStringView InKey)
	{
		const FStringView trimmed = TrimView(InExpression);
		int32 k = 0;
		bool bPendingSpace = false;
		for (const TCHAR ch : trimmed)
		{
			if (FChar::IsWhitespace(ch))
			{
				bPendingSpace = true;
				continue;
			}
			if (bPendingSpace)
			{
				if (k >= InKey.Len() || InKey[k] != TEXT(' '))
					return false;
				++k;
				bPendingSpace = false;
			}
			if (k >= InKey.Len() || InKey[k] != ch)
				return false;
			++k;
		}
		return k == InKey.Len();
	}

	int32 SkipWhitespace(FStringView InText, int32 InPos)
	{
		while (InPos < InText.Len() && FChar::IsWhitespace(InText[InPos]))
			++InPos;
		return InPos;
	}

	struct FAligner
	{
		FStringView Runtime;
		TConstArrayView<FString> Literals;
		int32 NumCaptures = 0;
		int32 Steps = 0;
		TArray<TPair<int32, int32>, TInlineAllocator<8>> Captures;

		/**
		 * Equivalent to the web player's anchored lazy pattern
		 *     ^L0\s*(.*?)\s*L1\s*(.*?) ... \s*Ln$
		 * where each Lk is a literal with its boundary whitespace trimmed: each interior
		 * literal is tried at successive occurrences, earliest first, backtracking when
		 * the remainder of the line cannot be aligned.
		 */
		bool Match(int32 InCapture, int32 InPos)
		{
			if (++Steps > GMaxAlignmentSteps)
				return false;

			if (InCapture == NumCaptures - 1)
			{
				const FString& last = Literals[NumCaptures];
				if (!Runtime.EndsWith(last, ESearchCase::CaseSensitive))
					return false;
				const int32 end = Runtime.Len() - last.Len();
				if (end < InPos)
					return false;
				Captures[InCapture] = { InPos, end };
				return true;
			}

			const FString& literal = Literals[InCapture + 1];
			int32 search = InPos;
			while (search <= Runtime.Len())
			{
				const int32 found = literal.IsEmpty()
					? search
					: UE::String::FindFirst(Runtime.RightChop(search), literal, ESearchCase::CaseSensitive);
				if (found == INDEX_NONE)
					return false;
				const int32 at = literal.IsEmpty() ? found : search + found;

				Captures[InCapture] = { InPos, at };
				if (Match(InCapture + 1, SkipWhitespace(Runtime, at + literal.Len())))
					return true;
				if (Steps > GMaxAlignmentSteps)
					return false;
				search = at + 1;
			}
			return false;
		}
	};

	FString FormatNumberLiteral(double InValue)
	{
		const double rounded = FMath::RoundToDouble(InValue);
		if (rounded == InValue && FMath::Abs(InValue) < 1e15)
			return FString::Printf(TEXT("%lld"), static_cast<int64>(rounded));
		return FString::SanitizeFloat(InValue);
	}

	bool IsNumberLiteral(FStringView InText)
	{
		// ^[+-]?(?:\d+(?:\.\d+)?|\.\d+)$
		int32 i = 0;
		const int32 len = InText.Len();
		if (i < len && (InText[i] == TEXT('+') || InText[i] == TEXT('-')))
			++i;
		const int32 intStart = i;
		while (i < len && FChar::IsDigit(InText[i])) ++i;
		const bool bHasInt = i > intStart;
		if (i < len && InText[i] == TEXT('.'))
		{
			++i;
			const int32 fracStart = i;
			while (i < len && FChar::IsDigit(InText[i])) ++i;
			if (i == fracStart)
				return false;
		}
		else if (!bHasInt)
		{
			return false;
		}
		return i == len;
	}

	bool IsIdentifier(FStringView InText)
	{
		if (InText.IsEmpty() || !(FChar::IsAlpha(InText[0]) || InText[0] == TEXT('_')))
			return false;
		for (const TCHAR ch : InText)
		{
			if (!(FChar::IsAlnum(ch) || ch == TEXT('_')))
				return false;
		}
		return true;
	}

	/** Parses a whole-expression quoted string literal with backslash escapes. */
	bool TryParseStringLiteral(FStringView InText, FString& OutValue)
	{
		if (InText.Len() < 2)
			return false;
		const TCHAR quote = InText[0];
		if (quote != TEXT('\'') && quote != TEXT('"'))
			return false;

		OutValue.Reset();
		bool bEscaped = false;
		for (int32 i = 1; i < InText.Len(); ++i)
		{
			const TCHAR ch = InText[i];
			if (bEscaped)
			{
				OutValue.AppendChar(ch);
				bEscaped = false;
			}
			else if (ch == TEXT('\\'))
			{
				bEscaped = true;
			}
			else if (ch == quote)
			{
				return i == InText.Len() - 1;
			}
			else
			{
				OutValue.AppendChar(ch);
			}
		}
		return false;
	}
}

void FProsettaTemplateValues::Add(FStringView InKey, FString InValue)
{
	for (FEntry& entry : Entries)
	{
		if (FStringView(entry.Key).Equals(InKey, ESearchCase::CaseSensitive))
		{
			entry.Values.Add(MoveTemp(InValue));
			return;
		}
	}
	FEntry& entry = Entries.AddDefaulted_GetRef();
	entry.Key = FString(InKey);
	entry.Values.Add(MoveTemp(InValue));
}

const FProsettaTemplateValues::FEntry* FProsettaTemplateValues::Find(FStringView InKey) const
{
	for (const FEntry& entry : Entries)
	{
		if (FStringView(entry.Key).Equals(InKey, ESearchCase::CaseSensitive))
			return &entry;
	}
	return nullptr;
}

FProsettaInlineTemplate::FProsettaInlineTemplate(FString InSource)
	: Source(MoveTemp(InSource))
{
	if (MayContainExpressions(Source))
	{
		Parse();
		BuildAlignment();
	}
}

bool FProsettaInlineTemplate::MayContainExpressions(FStringView InText)
{
	int32 index;
	return InText.FindChar(TEXT('{'), index);
}

FString FProsettaInlineTemplate::NormalizeExpressionKey(FStringView InExpression)
{
	FString key;
	key.Reserve(InExpression.Len());
	AppendNormalizedKey(InExpression, key);
	return key;
}

FString FProsettaInlineTemplate::NormalizeToPlainText(FStringView InText)
{
	FString out;
	out.Reserve(InText.Len());
	bool bPendingSpace = false;
	const int32 len = InText.Len();
	for (int32 i = 0; i < len; ++i)
	{
		const TCHAR ch = InText[i];
		if (ch == TEXT('<'))
		{
			int32 close;
			if (InText.RightChop(i + 1).FindChar(TEXT('>'), close))
			{
				i += close + 1;
				continue;
			}
		}
		if (FChar::IsWhitespace(ch))
		{
			bPendingSpace = true;
			continue;
		}
		if (bPendingSpace && !out.IsEmpty())
			out.AppendChar(TEXT(' '));
		bPendingSpace = false;
		out.AppendChar(ch);
	}
	return out;
}

bool FProsettaInlineTemplate::ResolveStoryVariable(UInkpotStory* InStory, FStringView InName, FString& OutText)
{
	if (!InStory || !InStory->IsValid())
		return false;
	const TSharedPtr<FInkpotStoryInternal> story = InStory->GetStoryInternal();
	if (!story.IsValid())
		return false;
	const TSharedPtr<Ink::FVariableState> variables = story->GetVariablesState();
	if (!variables.IsValid())
		return false;

	const TSharedPtr<Ink::FValue> value = Ink::FObject::DynamicCastTo<Ink::FValue>(variables->GetVariable(FString(InName)));
	if (!value.IsValid())
		return false;

	switch (value->GetType())
	{
	case Ink::EValueType::Bool:
	case Ink::EValueType::Int:
	case Ink::EValueType::Float:
	case Ink::EValueType::String:
	case Ink::EValueType::List:
		OutText = value->ToString();
		return true;
	default:
		return false;
	}
}

TArray<FStringView, TInlineAllocator<8>> FProsettaInlineTemplate::GetExpressions() const
{
	TArray<FStringView, TInlineAllocator<8>> expressions;
	for (const FSegment& segment : Segments)
	{
		// Expression segments span their braces so Render can write them verbatim.
		if (segment.bExpression)
			expressions.Add(SegmentView(segment).Mid(1, segment.Len - 2).TrimStartAndEnd());
	}
	return expressions;
}

FStringView FProsettaInlineTemplate::SegmentView(const FSegment& InSegment) const
{
	return FStringView(Source).Mid(InSegment.Start, InSegment.Len);
}

void FProsettaInlineTemplate::Parse()
{
	FRawSegments raw;
	SplitSegments(Source, raw);

	Segments.Reset();
	ExpressionKeys.Reset();
	ExpressionCount = 0;
	for (const FRawSegment& segment : raw)
	{
		if (segment.bExpression)
		{
			// keep the full {...} range so unresolved placeholders render verbatim
			Segments.Add({ segment.OuterStart, segment.OuterLen, true });
			ExpressionKeys.Add(NormalizeExpressionKey(FStringView(Source).Mid(segment.Start, segment.Len)));
			++ExpressionCount;
		}
		else
		{
			Segments.Add({ segment.Start, segment.Len, false });
		}
	}
}

void FProsettaInlineTemplate::BuildAlignment()
{
	Alignment = FAlignment();
	if (ExpressionCount == 0)
		return;

	// Swap each placeholder for a sentinel, then project into the same plain-text space
	// as the runtime text ( markup stripped, whitespace collapsed ) so the literals
	// between placeholders can anchor the alignment.
	FString sentinelized;
	sentinelized.Reserve(Source.Len());
	int32 expressionIndex = 0;
	for (const FSegment& segment : Segments)
	{
		if (!segment.bExpression)
		{
			sentinelized += FStringView(Source).Mid(segment.Start, segment.Len);
			continue;
		}
		sentinelized.AppendChar(GSentinelStart);
		sentinelized.AppendInt(expressionIndex++);
		sentinelized.AppendChar(GSentinelEnd);
	}

	const FString normalized = NormalizeToPlainText(sentinelized);

	FString literal;
	const int32 len = normalized.Len();
	for (int32 i = 0; i < len; ++i)
	{
		const TCHAR ch = normalized[i];
		if (ch == GSentinelStart)
		{
			int32 end = i + 1;
			while (end < len && FChar::IsDigit(normalized[end])) ++end;
			if (end < len && end > i + 1 && normalized[end] == GSentinelEnd)
			{
				Alignment.Literals.Add(FString(FStringView(literal).TrimStartAndEnd()));
				Alignment.ExpressionIndices.Add(FCString::Atoi(*normalized.Mid(i + 1, end - i - 1)));
				literal.Reset();
				i = end;
				continue;
			}
		}
		literal.AppendChar(ch);
	}
	Alignment.Literals.Add(FString(FStringView(literal).TrimStartAndEnd()));
}

bool FProsettaInlineTemplate::ExtractValues(FStringView InRuntimeText, FProsettaTemplateValues& OutValues) const
{
	OutValues.Reset();

	const int32 numCaptures = Alignment.ExpressionIndices.Num();
	if (ExpressionCount == 0 || numCaptures == 0)
		return false;

	const FString runtime = NormalizeToPlainText(InRuntimeText);

	// Runtime text that still carries one of our own placeholders is not evaluated
	// text ( e.g. a surface passing the template through ); live evaluation handles it.
	if (MayContainExpressions(runtime))
	{
		FRawSegments runtimeSegments;
		SplitSegments(runtime, runtimeSegments);
		for (const FRawSegment& segment : runtimeSegments)
		{
			if (!segment.bExpression)
				continue;
			const FStringView expression = FStringView(runtime).Mid(segment.Start, segment.Len);
			for (const FString& key : ExpressionKeys)
			{
				if (KeyEquals(expression, key))
					return false;
			}
		}
	}

	const FString& first = Alignment.Literals[0];
	if (!FStringView(runtime).StartsWith(first, ESearchCase::CaseSensitive))
		return false;

	FAligner aligner;
	aligner.Runtime = runtime;
	aligner.Literals = Alignment.Literals;
	aligner.NumCaptures = numCaptures;
	aligner.Captures.SetNumZeroed(numCaptures);
	if (!aligner.Match(0, SkipWhitespace(runtime, first.Len())))
		return false;

	for (int32 k = 0; k < numCaptures; ++k)
	{
		// A placeholder flanked by an interior literal with no anchoring text is
		// ambiguous ( the shared text cannot be split ), so leave it to live evaluation.
		const bool bEmptyBefore = k > 0 && Alignment.Literals[k].IsEmpty();
		const bool bEmptyAfter = k < numCaptures - 1 && Alignment.Literals[k + 1].IsEmpty();
		if (bEmptyBefore || bEmptyAfter)
			continue;

		const int32 expression = Alignment.ExpressionIndices[k];
		if (!ExpressionKeys.IsValidIndex(expression))
			continue;

		const TPair<int32, int32>& capture = aligner.Captures[k];
		const FStringView value = FStringView(runtime).Mid(capture.Key, capture.Value - capture.Key).TrimStartAndEnd();
		OutValues.Add(ExpressionKeys[expression], FString(value));
	}

	return !OutValues.IsEmpty();
}

bool FProsettaInlineTemplate::Render(const FProsettaTemplateValues* InValues, FVariableResolver InResolver, FString& OutText) const
{
	return RenderInternal(InValues, &InResolver, 0, OutText);
}

bool FProsettaInlineTemplate::Render(const FProsettaTemplateValues* InValues, FString& OutText) const
{
	return RenderInternal(InValues, nullptr, 0, OutText);
}

bool FProsettaInlineTemplate::RenderInternal(const FProsettaTemplateValues* InValues, const FVariableResolver* InResolver, int32 InDepth, FString& OutText) const
{
	if (ExpressionCount == 0)
	{
		OutText = Source;
		return true;
	}

	OutText.Reset(Source.Len());

	// per-entry occurrence cursors, so repeated expressions consume values in order
	TArray<int32, TInlineAllocator<8>> occurrences;
	if (InValues)
		occurrences.SetNumZeroed(InValues->Num());

	bool bAllResolved = true;
	int32 expressionIndex = 0;
	FString scratch;

	for (const FSegment& segment : Segments)
	{
		const FStringView view = SegmentView(segment);
		if (!segment.bExpression)
		{
			OutText += view;
			continue;
		}

		const FString& key = ExpressionKeys[expressionIndex++];

		// Prefer the value recovered from the runtime text ( top level only ).
		if (InDepth == 0 && InValues)
		{
			if (const FProsettaTemplateValues::FEntry* entry = InValues->Find(key))
			{
				if (entry->Values.Num() > 0)
				{
					const int32 entryIndex = static_cast<int32>(entry - InValues->GetEntries().GetData());
					const int32 occurrence = occurrences[entryIndex]++;
					// overflow occurrences clamp to the last value
					OutText += entry->Values[FMath::Min(occurrence, entry->Values.Num() - 1)];
					continue;
				}
			}
		}

		// Live evaluation: literals and story variables.
		bool bResolved = true;
		bool bMayNest = false;
		const FStringView expression = view.Mid(1, view.Len() - 2).TrimStartAndEnd();
		if (expression.Equals(TEXTVIEW("true"), ESearchCase::CaseSensitive) || expression.Equals(TEXTVIEW("false"), ESearchCase::CaseSensitive))
		{
			scratch = FString(expression);
		}
		else if (expression.Equals(TEXTVIEW("null"), ESearchCase::CaseSensitive))
		{
			scratch.Reset();
		}
		else if (IsNumberLiteral(expression))
		{
			scratch = FormatNumberLiteral(FCString::Atod(*FString(expression)));
		}
		else if (TryParseStringLiteral(expression, scratch))
		{
			bMayNest = true;
		}
		else if (InResolver && IsIdentifier(expression) && (*InResolver)(expression, scratch))
		{
			bMayNest = true;
		}
		else
		{
			bResolved = false;
		}

		if (!bResolved)
		{
			bAllResolved = false;
			OutText += view;
			continue;
		}

		if (bMayNest && InDepth < GMaxRecursionDepth && MayContainExpressions(scratch))
		{
			FString nested;
			const FProsettaInlineTemplate nestedTemplate(scratch);
			nestedTemplate.RenderInternal(nullptr, InResolver, InDepth + 1, nested);
			OutText += nested;
		}
		else
		{
			OutText += scratch;
		}
	}

	return bAllResolved;
}
