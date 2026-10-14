#include "Inkpot/Prosetta/ProsettaLine.h"
#include "Inkpot/InkpotStory.h"

void UProsettaLine::Initialise(FName InLineId, const FString& InText, const TMap<FName, FString>& InAttributes, int32 InStart, int32 InLength)
{
	LineId = InLineId;
	Text = InText;
	Attributes = InAttributes;
	StartOffset = InStart;
	Length = InLength;
}

void UProsettaLine::SetMetadata(const FProsettaLineMetadata& InMetadata, bool bInResolved)
{
	Metadata = InMetadata;
	bResolved = bInResolved;
}

void UProsettaLine::SetRuntimeText(const FString& InRuntimeText, FProsettaTemplateValues&& InValues)
{
	RuntimeText = InRuntimeText;
	TemplateValues = MoveTemp(InValues);
}

const FString& UProsettaLine::GetRuntimeText() const
{
	return RuntimeText;
}

bool UProsettaLine::GetTemplateValue(const FString& InExpression, FString& OutValue) const
{
	const FProsettaTemplateValues::FEntry* entry = TemplateValues.Find(FProsettaInlineTemplate::NormalizeExpressionKey(InExpression));
	if (!entry || entry->Values.IsEmpty())
		return false;
	OutValue = entry->Values[0];
	return true;
}

const FProsettaTemplateValues& UProsettaLine::GetTemplateValues() const
{
	return TemplateValues;
}

bool UProsettaLine::RenderTemplate(const FString& InTemplate, FString& OutText) const
{
	if (!FProsettaInlineTemplate::MayContainExpressions(InTemplate))
	{
		OutText = InTemplate;
		return true;
	}

	UInkpotStory* story = GetTypedOuter<UInkpotStory>();
	const FProsettaInlineTemplate parsed(InTemplate);
	return parsed.Render(&TemplateValues, [story](FStringView InName, FString& OutValue)
	{
		return FProsettaInlineTemplate::ResolveStoryVariable(story, InName, OutValue);
	}, OutText);
}

bool UProsettaLine::GetLocalizedText(const FString& InLanguageCode, FString& OutText) const
{
	const FString* translation = Metadata.Localizations.Find(InLanguageCode);
	if (translation && !translation->IsEmpty() && RenderTemplate(*translation, OutText))
		return true;
	OutText = Text;
	return false;
}

FName UProsettaLine::GetLineId() const
{
	return LineId;
}

const FString& UProsettaLine::GetText() const
{
	return Text;
}

const TMap<FName, FString>& UProsettaLine::GetAttributes() const
{
	return Attributes;
}

bool UProsettaLine::GetAttribute(FName InName, FString& OutValue) const
{
	if (const FString* value = Attributes.Find(InName))
	{
		OutValue = *value;
		return true;
	}
	return false;
}

bool UProsettaLine::IsResolved() const
{
	return bResolved;
}

const FProsettaLineMetadata& UProsettaLine::GetMetadata() const
{
	return Metadata;
}

int32 UProsettaLine::GetStartOffset() const
{
	return StartOffset;
}

int32 UProsettaLine::GetLength() const
{
	return Length;
}
