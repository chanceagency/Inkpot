#include "Inkpot/Prosetta/ProsettaMetadata.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Utility/InkpotLog.h"

namespace
{
	void ReadStringMap(const TSharedPtr<FJsonObject>& InObject, const FString& InField, TMap<FString, FString>& OutMap)
	{
		const TSharedPtr<FJsonObject>* mapObject = nullptr;
		if (!InObject->TryGetObjectField(InField, mapObject) || !mapObject->IsValid())
			return;
		for (const auto& pair : (*mapObject)->Values)
		{
			FString value;
			if (pair.Value.IsValid() && pair.Value->TryGetString(value))
				OutMap.Add(FString(*pair.Key), value);
		}
	}

	FProsettaLineMetadata ReadLine(FName InLineId, const TSharedPtr<FJsonObject>& InObject)
	{
		FProsettaLineMetadata meta;
		meta.LineId = InLineId;
		InObject->TryGetStringField(TEXT("string"), meta.String);
		InObject->TryGetStringField(TEXT("kind"), meta.Kind);
		if (!InObject->TryGetStringField(TEXT("speaker"), meta.SpeakerShortname))
			InObject->TryGetStringField(TEXT("speakerShortname"), meta.SpeakerShortname);
		ReadStringMap(InObject, TEXT("tags"), meta.Tags);
		ReadStringMap(InObject, TEXT("localizations"), meta.Localizations);
		return meta;
	}
}

bool UProsettaMetadata::LoadFromJSON(const FString& InJSON)
{
	Lines.Empty();
	Templates.Empty();

	if (InJSON.IsEmpty())
		return false;

	TSharedPtr<FJsonObject> root;
	const TSharedRef<TJsonReader<>> reader = TJsonReaderFactory<>::Create(InJSON);
	if (!FJsonSerializer::Deserialize(reader, root) || !root.IsValid())
	{
		INKPOT_ERROR("Could not parse Prosetta sidecar JSON.");
		return false;
	}

	// "lines" may be supplied either as an object keyed by line id, or as an array of
	// objects each carrying a "lineId" ( the tag id ) and / or "id" field.
	const TSharedPtr<FJsonObject>* linesObject = nullptr;
	if (root->TryGetObjectField(TEXT("lines"), linesObject) && linesObject->IsValid())
	{
		Lines.Reserve((*linesObject)->Values.Num());
		for (const auto& pair : (*linesObject)->Values)
		{
			const TSharedPtr<FJsonObject>* lineObject = nullptr;
			if (pair.Value.IsValid() && pair.Value->TryGetObject(lineObject) && lineObject->IsValid())
			{
				const FName lineId(*pair.Key);
				Lines.Add(lineId, ReadLine(lineId, *lineObject));
			}
		}
	}
	else
	{
		const TArray<TSharedPtr<FJsonValue>>* linesArray = nullptr;
		if (root->TryGetArrayField(TEXT("lines"), linesArray))
		{
			Lines.Reserve(linesArray->Num());
			for (const TSharedPtr<FJsonValue>& value : *linesArray)
			{
				const TSharedPtr<FJsonObject>* lineObject = nullptr;
				if (!value.IsValid() || !value->TryGetObject(lineObject) || !lineObject->IsValid())
					continue;
				// Engine Sync records carry both a database "id" ( e.g. "23-0008" ) and the
				// "lineId" used by the <prosetta id> tag ( e.g. "0008" ); join on the latter.
				FString lineId;
				if (!(*lineObject)->TryGetStringField(TEXT("lineId"), lineId) || lineId.IsEmpty())
					(*lineObject)->TryGetStringField(TEXT("id"), lineId);
				if (lineId.IsEmpty())
					continue;
				const FName nameLineId(*lineId);
				Lines.Add(nameLineId, ReadLine(nameLineId, *lineObject));
			}
		}
	}

	// Parse inline expression templates once, up front, so the per-line hot path only
	// aligns and renders.
	for (const TPair<FName, FProsettaLineMetadata>& pair : Lines)
	{
		if (FProsettaInlineTemplate::MayContainExpressions(pair.Value.String))
		{
			FProsettaInlineTemplate parsed(pair.Value.String);
			if (parsed.HasExpressions())
				Templates.Add(pair.Key, MoveTemp(parsed));
		}
	}

	return Lines.Num() > 0;
}

bool UProsettaMetadata::Resolve(FName InLineId, FProsettaLineMetadata& OutMetadata) const
{
	if (const FProsettaLineMetadata* found = Lines.Find(InLineId))
	{
		OutMetadata = *found;
		return true;
	}
	return false;
}

bool UProsettaMetadata::ResolveText(FName InLineId, FString& OutText) const
{
	const FProsettaLineMetadata* found = Lines.Find(InLineId);
	if (found && !found->String.IsEmpty())
	{
		OutText = found->String;
		return true;
	}
	return false;
}

bool UProsettaMetadata::ResolveDisplayText(FName InLineId, FStringView InRuntimeText, FProsettaInlineTemplate::FVariableResolver InResolver,
	FString& OutText, FProsettaTemplateValues& OutValues) const
{
	OutValues.Reset();

	const FProsettaLineMetadata* found = Lines.Find(InLineId);
	if (!found || found->String.IsEmpty())
		return false;

	const FProsettaInlineTemplate* parsed = Templates.Find(InLineId);
	if (!parsed)
	{
		OutText = found->String;
		return true;
	}

	parsed->ExtractValues(InRuntimeText, OutValues);

	FString rendered;
	if (!parsed->Render(&OutValues, InResolver, rendered))
		return false;

	OutText = MoveTemp(rendered);
	return true;
}

const FProsettaInlineTemplate* UProsettaMetadata::FindTemplate(FName InLineId) const
{
	return Templates.Find(InLineId);
}

int32 UProsettaMetadata::Num() const
{
	return Lines.Num();
}

bool UProsettaMetadata::IsEmpty() const
{
	return Lines.Num() == 0;
}
