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
	// Rendering only: skip the alignment ExtractValues would need.
	const FProsettaInlineTemplate parsed(InTemplate, FProsettaInlineTemplate::EUsage::RenderOnly);
	return parsed.Render(&TemplateValues, [story](FStringView InName, FString& OutValue)
	{
		return FProsettaInlineTemplate::ResolveStoryVariable(story, InName, OutValue);
	}, OutText);
}

FString UProsettaLine::NormalizeLanguageCode(const FString& InLanguageCode)
{
	return InLanguageCode.TrimStartAndEnd().ToLower().Replace(TEXT("_"), TEXT("-"));
}

const FString* UProsettaLine::FindLocalization(const FString& InLanguageCode) const
{
	// Sidecar keys may be written "pt-BR", "pt_BR" or "pt-br"; compare normalised forms.
	const FString requested = NormalizeLanguageCode(InLanguageCode);
	if (requested.IsEmpty())
		return nullptr;
	const FString* exact = Metadata.Localizations.Find(InLanguageCode);
	if (exact && !exact->IsEmpty())
		return exact;

	// A regional request ( "pt-br" ) falls back to its base language ( "pt" ).
	int32 dash;
	const FString base = requested.FindChar(TEXT('-'), dash) ? requested.Left(dash) : FString();
	const FString* baseMatch = nullptr;
	for (const TPair<FString, FString>& pair : Metadata.Localizations)
	{
		if (pair.Value.IsEmpty())
			continue;
		const FString key = NormalizeLanguageCode(pair.Key);
		if (key == requested)
			return &pair.Value;
		if (!baseMatch && !base.IsEmpty() && key == base)
			baseMatch = &pair.Value;
	}
	return baseMatch;
}

bool UProsettaLine::GetLocalizedText(const FString& InLanguageCode, FString& OutText) const
{
	const FString* translation = FindLocalization(InLanguageCode);
	if (translation && RenderTemplate(*translation, OutText))
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
