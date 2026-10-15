#include "Inkpot/InkpotLine.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/Prosetta/InkpotProsettaLibrary.h"
#include "Inkpot/Prosetta/ProsettaLine.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"
#include "Inkpot/Prosetta/ProsettaInlineTemplate.h"

UInkpotLine::UInkpotLine()
{
}

void UInkpotLine::Initialise(const FString &InString)
{
	RawString = InString;

	// Scan the fragment for <prosetta> tags, hydrating each into a UProsettaLine and
	// producing clean, player-facing text with the markup removed. When line metadata
	// is available we prefer the authoritative string for each identified segment, with
	// its inline Ink {expressions} filled from the text the runtime emitted.
	UInkpotStory* story = GetStory();
	UProsettaMetadata* metadata = story ? story->GetProsettaMetadata() : nullptr;

	// Values recovered while resolving, handed on to the matching segment below.
	struct FResolvedValues
	{
		FName LineId;
		FString RuntimeText;
		FProsettaTemplateValues Values;
	};
	TArray<FResolvedValues, TInlineAllocator<4>> resolvedValues;

	auto variableResolver = [story](FStringView InName, FString& OutValue) -> bool
	{
		return FProsettaInlineTemplate::ResolveStoryVariable(story, InName, OutValue);
	};

	auto resolver = [metadata, &variableResolver, &resolvedValues](FName InLineId, FString& InOutText) -> bool
	{
		if (!metadata)
			return false;

		FProsettaTemplateValues values;
		FString display;
		const bool bResolved = metadata->ResolveDisplayText(InLineId, InOutText, variableResolver, display, values);
		if (!values.IsEmpty())
			resolvedValues.Add({ InLineId, InOutText, MoveTemp(values) });
		if (bResolved)
			InOutText = MoveTemp(display);
		return bResolved;
	};

	const FProsettaParseResult parsed = UInkpotProsettaLibrary::ParseFragmentResolved(InString, resolver);

	String = parsed.CleanText;

	ProsettaSegments.Reset();
	for (const FProsettaParsedSegment& segment : parsed.Segments)
	{
		UProsettaLine* line = NewObject<UProsettaLine>(this);
		line->Initialise(segment.LineId, segment.Text, segment.Attributes, segment.StartOffset, segment.Length);

		FProsettaLineMetadata lineMetadata;
		const bool bResolved = metadata && metadata->Resolve(segment.LineId, lineMetadata);
		line->SetMetadata(MoveTemp(lineMetadata), bResolved);
		line->CaptureVariables(variableResolver);

		FProsettaTemplateValues values;
		const int32 valuesIndex = resolvedValues.IndexOfByPredicate([&segment](const FResolvedValues& InResolved)
		{
			return InResolved.LineId == segment.LineId && InResolved.RuntimeText.Equals(segment.RuntimeText, ESearchCase::CaseSensitive);
		});
		if (valuesIndex != INDEX_NONE)
		{
			values = MoveTemp(resolvedValues[valuesIndex].Values);
			resolvedValues.RemoveAt(valuesIndex, EAllowShrinking::No);
		}
		line->SetRuntimeText(segment.RuntimeText, MoveTemp(values));

		ProsettaSegments.Add(line);
	}

	// TODO : Localisation support here
	Text = FText::FromString(String);
	SetDirty(true);
}

const FString& UInkpotLine::GetString() const
{
	return String;
}

const FString& UInkpotLine::GetRawString() const
{
	return RawString;
}

TArray<UProsettaLine*> UInkpotLine::GetProsettaSegments() const
{
	TArray<UProsettaLine*> segments;
	segments.Reserve(ProsettaSegments.Num());
	for (const TObjectPtr<UProsettaLine>& segment : ProsettaSegments)
		segments.Add(segment);
	return segments;
}

bool UInkpotLine::HasProsettaTags() const
{
	return ProsettaSegments.Num() > 0;
}

UInkpotStory* UInkpotLine::GetStory() const
{
	UObject *outer = GetOuter();
	return Cast<UInkpotStory>( outer );
}

const FText& UInkpotLine::GetText() const
{
	return Text;
}

const TArray<FString> &UInkpotLine::GetTags() const
{
	return GetTagsInternal();
}

const TArray<FString> &UInkpotLine::GetTagsInternal() const
{
	UInkpotStory* story = GetStory();
	return story->GetCurrentTags();
}

bool UInkpotLine::IsDirty()
{
	return bIsDirty;
}

void UInkpotLine::SetDirty(bool bInIsDirty)
{
	bIsDirty = bInIsDirty;
}
