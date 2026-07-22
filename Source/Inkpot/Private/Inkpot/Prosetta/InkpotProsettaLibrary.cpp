#include "Inkpot/Prosetta/InkpotProsettaLibrary.h"

namespace
{
	const TCHAR* GProsettaOpenName = TEXT("<prosetta");
	const TCHAR* GProsettaCloseName = TEXT("</prosetta");
	const int32 GOpenNameLen = 9;  // length of "<prosetta"
	const int32 GCloseNameLen = 10; // length of "</prosetta"

	bool IsNameBoundary(TCHAR InChar)
	{
		return InChar == TEXT('\0') || FChar::IsWhitespace(InChar) || InChar == TEXT('>') || InChar == TEXT('/');
	}

	TCHAR CharAt(const FString& InText, int32 InIndex)
	{
		return InText.IsValidIndex(InIndex) ? InText[InIndex] : TEXT('\0');
	}

	// Finds the next occurrence of NameToken that is a genuine tag ( i.e. followed by a
	// name boundary ), starting the search at InFrom. Returns INDEX_NONE if not found.
	int32 FindTagToken(const FString& InText, const TCHAR* InNameToken, int32 InNameLen, int32 InFrom)
	{
		int32 search = InFrom;
		while (true)
		{
			const int32 found = InText.Find(InNameToken, ESearchCase::IgnoreCase, ESearchDir::FromStart, search);
			if (found == INDEX_NONE)
				return INDEX_NONE;
			if (IsNameBoundary(CharAt(InText, found + InNameLen)))
				return found;
			search = found + InNameLen;
		}
	}

	// Splits the text found between the tag name and its closing '>' into an id and the
	// remaining data attributes. Supports single quoted, double quoted, and bare values.
	void ParseAttributes(const FString& InAttrText, FString& OutLineId, TMap<FName, FString>& OutAttributes)
	{
		const int32 len = InAttrText.Len();
		int32 i = 0;
		auto SkipWhitespace = [&]() { while (i < len && FChar::IsWhitespace(InAttrText[i])) ++i; };

		SkipWhitespace();
		while (i < len)
		{
			// read key
			const int32 keyStart = i;
			while (i < len && !FChar::IsWhitespace(InAttrText[i]) && InAttrText[i] != TEXT('=')) ++i;
			FString key = InAttrText.Mid(keyStart, i - keyStart);
			if (key.IsEmpty())
			{
				++i;
				continue;
			}

			SkipWhitespace();
			FString value;
			if (i < len && InAttrText[i] == TEXT('='))
			{
				++i; // consume '='
				SkipWhitespace();
				if (i < len && (InAttrText[i] == TEXT('"') || InAttrText[i] == TEXT('\'')))
				{
					const TCHAR quote = InAttrText[i++];
					const int32 valStart = i;
					while (i < len && InAttrText[i] != quote) ++i;
					value = InAttrText.Mid(valStart, i - valStart);
					if (i < len) ++i; // consume closing quote
				}
				else
				{
					const int32 valStart = i;
					while (i < len && !FChar::IsWhitespace(InAttrText[i])) ++i;
					value = InAttrText.Mid(valStart, i - valStart);
				}
			}

			if (key.Equals(TEXT("id"), ESearchCase::IgnoreCase))
				OutLineId = value;
			else
				OutAttributes.Add(FName(*key), value);

			SkipWhitespace();
		}
	}
}

FProsettaParseResult UInkpotProsettaLibrary::ParseFragmentResolved(const FString& InFragment, FTextResolver InResolver)
{
	FProsettaParseResult result;
	FString& clean = result.CleanText;

	const int32 len = InFragment.Len();
	int32 cursor = 0;

	while (cursor < len)
	{
		const int32 tagStart = FindTagToken(InFragment, GProsettaOpenName, GOpenNameLen, cursor);
		if (tagStart == INDEX_NONE)
		{
			clean += InFragment.Mid(cursor);
			break;
		}

		// copy any literal text preceding the tag
		clean += InFragment.Mid(cursor, tagStart - cursor);

		const int32 afterName = tagStart + GOpenNameLen;
		const int32 openEnd = InFragment.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, afterName);
		if (openEnd == INDEX_NONE)
		{
			// malformed open tag, treat the remainder as literal text
			clean += InFragment.Mid(tagStart);
			break;
		}

		FString attrText = InFragment.Mid(afterName, openEnd - afterName);
		const bool bSelfClosing = attrText.TrimEnd().EndsWith(TEXT("/"));
		if (bSelfClosing)
		{
			attrText = attrText.TrimEnd();
			attrText = attrText.LeftChop(1);
		}

		FProsettaParsedSegment segment;
		ParseAttributes(attrText, segment.LineId, segment.Attributes);

		FString innerText;
		int32 nextCursor;
		if (bSelfClosing)
		{
			nextCursor = openEnd + 1;
		}
		else
		{
			// depth-aware search for the matching close tag, so nested tags are handled
			int32 depth = 1;
			int32 pos = openEnd + 1;
			int32 innerEnd = INDEX_NONE;
			int32 afterClose = INDEX_NONE;
			while (pos < len)
			{
				const int32 nextOpen = FindTagToken(InFragment, GProsettaOpenName, GOpenNameLen, pos);
				const int32 nextClose = FindTagToken(InFragment, GProsettaCloseName, GCloseNameLen, pos);
				if (nextClose == INDEX_NONE)
					break;
				if (nextOpen != INDEX_NONE && nextOpen < nextClose)
				{
					++depth;
					pos = nextOpen + GOpenNameLen;
				}
				else
				{
					--depth;
					if (depth == 0)
					{
						innerEnd = nextClose;
						const int32 closeGt = InFragment.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, nextClose);
						afterClose = (closeGt == INDEX_NONE) ? len : closeGt + 1;
						break;
					}
					pos = nextClose + GCloseNameLen;
				}
			}

			if (innerEnd == INDEX_NONE)
			{
				// unbalanced tag, treat the remainder as literal text
				clean += InFragment.Mid(tagStart);
				break;
			}

			const FString rawInner = InFragment.Mid(openEnd + 1, innerEnd - (openEnd + 1));
			// strip any nested markup from the inner text for display
			innerText = ParseFragmentResolved(rawInner, InResolver).CleanText;
			nextCursor = afterClose;
		}

		// resolve the display text: prefer an authoritative replacement, else inner text
		FString displayText = innerText;
		InResolver(segment.LineId, displayText);

		segment.StartOffset = clean.Len();
		segment.Text = displayText;
		segment.Length = displayText.Len();
		clean += displayText;

		result.Segments.Add(MoveTemp(segment));
		result.bHasTags = true;
		cursor = nextCursor;
	}

	return result;
}

FProsettaParseResult UInkpotProsettaLibrary::ParseFragment(const FString& InFragment)
{
	return ParseFragmentResolved(InFragment, [](const FString&, FString&) { return false; });
}

FString UInkpotProsettaLibrary::StripTags(const FString& InFragment)
{
	return ParseFragment(InFragment).CleanText;
}
