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

namespace
{
	// Matches the template renderer's nesting limit.
	constexpr int32 GMaxSnapshotDepth = 12;

	void SnapshotTemplateVariables(const FString& InTemplate, FProsettaInlineTemplate::FVariableResolver InResolver, int32 InDepth, TMap<FString, FString>& OutSnapshot)
	{
		if (InDepth > GMaxSnapshotDepth || !FProsettaInlineTemplate::MayContainExpressions(InTemplate))
			return;

		const FProsettaInlineTemplate parsed(InTemplate);
		for (const FStringView expression : parsed.GetExpressions())
		{
			if (!FProsettaInlineTemplate::IsIdentifierExpression(expression))
				continue;
			const FString name(expression);
			if (OutSnapshot.Contains(name))
				continue;
			FString value;
			if (!InResolver(expression, value))
				continue;
			OutSnapshot.Add(name, value);
			// A string value that is itself a template renders nested; freeze its variables too.
			SnapshotTemplateVariables(value, InResolver, InDepth + 1, OutSnapshot);
		}
	}
}

void UProsettaLine::CaptureVariables(FProsettaInlineTemplate::FVariableResolver InResolver)
{
	VariableSnapshot.Reset();
	SnapshotTemplateVariables(Metadata.String, InResolver, 0, VariableSnapshot);
	for (const TPair<FString, FString>& localization : Metadata.Localizations)
		SnapshotTemplateVariables(localization.Value, InResolver, 0, VariableSnapshot);
}

FString UProsettaLine::NormalizeLanguageCode(const FString& InLanguageCode)
{
	return InLanguageCode.TrimStartAndEnd().ToLower().Replace(TEXT("_"), TEXT("-"));
}

const FString* UProsettaLine::FindLocalization(const FString& InLanguageCode) const
{
	if (const FString* exact = Metadata.Localizations.Find(InLanguageCode))
	{
		if (!exact->IsEmpty())
			return exact;
	}

	const FString normalized = NormalizeLanguageCode(InLanguageCode);
	if (normalized.IsEmpty())
		return nullptr;

	int32 separator = INDEX_NONE;
	const FString base = normalized.FindChar(TEXT('-'), separator) ? normalized.Left(separator) : FString();

	const FString* baseMatch = nullptr;
	for (const TPair<FString, FString>& localization : Metadata.Localizations)
	{
		if (localization.Value.IsEmpty())
			continue;
		const FString key = NormalizeLanguageCode(localization.Key);
		if (key == normalized)
			return &localization.Value;
		if (!baseMatch && !base.IsEmpty() && key == base)
			baseMatch = &localization.Value;
	}
	return baseMatch;
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
	return parsed.Render(&TemplateValues, [this, story](FStringView InName, FString& OutValue)
	{
		// Emission-time values first: a line re-presented later ( e.g. after a language
		// switch ) must not pick up variables the story has since changed.
		if (const FString* captured = VariableSnapshot.Find(FString(InName)))
		{
			OutValue = *captured;
			return true;
		}
		return FProsettaInlineTemplate::ResolveStoryVariable(story, InName, OutValue);
	}, OutText);
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
