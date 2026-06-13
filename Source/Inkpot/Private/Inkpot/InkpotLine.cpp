#include "Inkpot/InkpotLine.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/Prosetta/InkpotProsettaLibrary.h"
#include "Inkpot/Prosetta/ProsettaLine.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"

UInkpotLine::UInkpotLine()
{
}

void UInkpotLine::Initialise(const FString &InString)
{
	RawString = InString;

	// Scan the fragment for <prosetta> tags, hydrating each into a UProsettaLine and
	// producing clean, player-facing text with the markup removed. When line metadata
	// is available we prefer the authoritative string for each identified segment.
	UInkpotStory* story = GetStory();
	UProsettaMetadata* metadata = story ? story->GetProsettaMetadata() : nullptr;

	auto resolver = [metadata](const FString& InLineId, FString& OutText) -> bool
	{
		return metadata ? metadata->ResolveText(InLineId, OutText) : false;
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
		line->SetMetadata(lineMetadata, bResolved);

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

const TArray<TObjectPtr<UProsettaLine>>& UInkpotLine::GetProsettaSegments() const
{
	return ProsettaSegments;
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
