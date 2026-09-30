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
	// objects each carrying an "id" / "lineId" field.
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
				FString lineId;
				if (!(*lineObject)->TryGetStringField(TEXT("id"), lineId))
					(*lineObject)->TryGetStringField(TEXT("lineId"), lineId);
				if (lineId.IsEmpty())
					continue;
				const FName nameLineId(*lineId);
				Lines.Add(nameLineId, ReadLine(nameLineId, *lineObject));
			}
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

int32 UProsettaMetadata::Num() const
{
	return Lines.Num();
}

bool UProsettaMetadata::IsEmpty() const
{
	return Lines.Num() == 0;
}
