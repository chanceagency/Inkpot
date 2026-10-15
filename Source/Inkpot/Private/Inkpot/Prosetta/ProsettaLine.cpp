#include "Inkpot/Prosetta/ProsettaLine.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"

namespace
{
	// One character of a canonical language code: lowercase, '_' read as '-'. Shared by
	// NormalizeLanguageCode and LanguageCodeEquals so the two cannot drift apart.
	TCHAR NormalizeLanguageCodeChar(TCHAR InChar)
	{
		return InChar == TEXT('_') ? TEXT('-') : FChar::ToLower(InChar);
	}

	// True when InKey normalises ( UProsettaLine::NormalizeLanguageCode ) to InNormalized, without allocating.
	bool LanguageCodeEquals(FStringView InKey, FStringView InNormalized)
	{
		InKey = InKey.TrimStartAndEnd();
		if (InKey.Len() != InNormalized.Len())
			return false;
		for (int32 i = 0; i < InKey.Len(); ++i)
		{
			if (NormalizeLanguageCodeChar(InKey[i]) != InNormalized[i])
				return false;
		}
		return true;
	}
}

void UProsettaLine::Initialise(FName InLineId, const FString& InText, const TMap<FName, FString>& InAttributes, int32 InStart, int32 InLength)
{
	LineId = InLineId;
	Text = InText;
	Attributes = InAttributes;
	StartOffset = InStart;
	Length = InLength;
}

void UProsettaLine::SetMetadata(FProsettaLineMetadata InMetadata, bool bInResolved)
{
	Metadata = MoveTemp(InMetadata);
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

	// Freezes one variable's value. A string value that is itself a template renders
	// nested, so its variables are frozen too; those values are the only templates parsed
	// here, the line's own were parsed when the sidecar loaded.
	void SnapshotVariable(FStringView InName, FProsettaInlineTemplate::FVariableResolver InResolver, int32 InDepth, UProsettaLine::FVariableSnapshot& OutSnapshot)
	{
		if (InDepth > GMaxSnapshotDepth)
			return;
		FString name(InName);
		if (OutSnapshot.Contains(name))
			return;
		FString value;
		if (!InResolver(InName, value))
			return;
		OutSnapshot.Add(MoveTemp(name), value);
		if (!FProsettaInlineTemplate::MayContainExpressions(value))
			return;

		const FProsettaInlineTemplate parsed(value, FProsettaInlineTemplate::EUsage::RenderOnly);
		for (const FStringView expression : parsed.GetExpressions())
		{
			if (FProsettaInlineTemplate::IsIdentifierExpression(expression))
				SnapshotVariable(expression, InResolver, InDepth + 1, OutSnapshot);
		}
	}
}

void UProsettaLine::CaptureVariables(FProsettaInlineTemplate::FVariableResolver InResolver)
{
	VariableSnapshot.Reset();
	if (!Metadata.bVariableNamesCollected)
		UProsettaMetadata::CollectVariableNames(Metadata);
	for (const FString& name : Metadata.VariableNames)
		SnapshotVariable(name, InResolver, 0, VariableSnapshot);
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

FString UProsettaLine::NormalizeLanguageCode(const FString& InLanguageCode)
{
	FString code = InLanguageCode.TrimStartAndEnd();
	for (TCHAR& ch : code)
		ch = NormalizeLanguageCodeChar(ch);
	return code;
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

	// A regional request falls back through its parents ( "zh-hant-tw" to "zh-hant" to "zh" ).
	FStringView code(requested);
	for (;;)
	{
		for (const TPair<FString, FString>& pair : Metadata.Localizations)
		{
			if (!pair.Value.IsEmpty() && LanguageCodeEquals(pair.Key, code))
				return &pair.Value;
		}
		int32 dash;
		if (!code.FindLastChar(TEXT('-'), dash) || dash == 0)
			return nullptr;
		code.LeftInline(dash);
	}
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
