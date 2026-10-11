#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

namespace
{
	FString Short(const FString& InHash)
	{
		return InHash.Len() > 8 ? InHash.Left(8) : InHash;
	}

	FString StringField(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField)
	{
		FString value;
		if (InObject.IsValid())
			InObject->TryGetStringField(InField, value);
		return value;
	}

	bool Parse(const FString& InJSON, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> reader = TJsonReaderFactory<>::Create(InJSON);
		return FJsonSerializer::Deserialize(reader, OutObject) && OutObject.IsValid();
	}

	/** A revision field may be a string or a map of project id -> revision. */
	FString RevisionFor(const TSharedPtr<FJsonValue>& InValue, int32 InProjectId)
	{
		if (!InValue.IsValid())
			return FString();
		FString text;
		if (InValue->TryGetString(text))
			return text;
		const TSharedPtr<FJsonObject>* map = nullptr;
		if (InValue->TryGetObject(map) && map->IsValid())
		{
			const FString key = FString::FromInt(InProjectId);
			for (const auto& pair : (*map)->Values)
			{
				FString revision;
				if ((InProjectId == 0 || FString(*pair.Key) == key) && pair.Value->TryGetString(revision))
					return revision;
			}
		}
		return FString();
	}
}

bool FProsettaStoryProvenance::IsSet() const
{
	return ProjectId > 0 || !ProjectIdentifier.IsEmpty() || !ProjectName.IsEmpty() || !GenerationId.IsEmpty() || !BuildId.IsEmpty() || !Namespace.IsEmpty() || !VolumeSlug.IsEmpty() || !ReleaseTagId.IsEmpty();
}

FString FProsettaStoryProvenance::ToLogString() const
{
	TArray<FString> parts;
	if (ProjectId > 0)
		parts.Add(FString::Printf(TEXT("project %d"), ProjectId));
	else if (!ProjectIdentifier.IsEmpty())
		parts.Add(TEXT("project ") + ProjectIdentifier);
	else if (!ProjectName.IsEmpty())
		parts.Add(TEXT("project '") + ProjectName + TEXT("'"));
	if (!Channel.IsEmpty())
		parts.Add(Channel);
	if (!VolumeSlug.IsEmpty() || !Revision.IsEmpty())
	{
		FString volume = VolumeSlug.IsEmpty() ? FString(TEXT("rev")) : VolumeSlug;
		if (!VolumeTag.IsEmpty()) volume += TEXT("#") + VolumeTag;
		if (!Revision.IsEmpty()) volume += TEXT("@") + Revision;
		parts.Add(volume);
	}
	else if (!VolumeTag.IsEmpty())
		parts.Add(TEXT("tag ") + VolumeTag);
	if (!ReleaseTagId.IsEmpty())
		parts.Add(TEXT("release ") + ReleaseTagId);
	if (!GenerationId.IsEmpty())
		parts.Add(TEXT("gen ") + Short(GenerationId));
	if (!BuildId.IsEmpty())
		parts.Add(TEXT("build ") + Short(BuildId));
	if (parts.Num() == 0 && !Namespace.IsEmpty())
		parts.Add(Namespace);
	return parts.Num() > 0 ? TEXT("Prosetta ") + FString::Join(parts, TEXT(" ")) : FString();
}

bool FProsettaStoryProvenance::FromSyncManifest(const TSharedPtr<FJsonObject>& InManifest, const TSharedPtr<FJsonObject>& InWatchEvent, const FString& InVolumeSlug, FProsettaStoryProvenance& OutProvenance)
{
	OutProvenance = FProsettaStoryProvenance();
	if (!InManifest.IsValid())
		return false;

	double number = 0;
	if (InManifest->TryGetNumberField(TEXT("projectId"), number))
		OutProvenance.ProjectId = static_cast<int32>(number);
	OutProvenance.ProjectIdentifier = StringField(InManifest, TEXT("projectIdentifier"));
	OutProvenance.ProjectName = StringField(InManifest, TEXT("projectName"));
	OutProvenance.GeneratedAt = StringField(InManifest, TEXT("generatedAt"));

	// Scope: a canonical string on older manifests, an object with volumes on scoped ones.
	const TSharedPtr<FJsonObject>* scope = nullptr;
	FString scopeText;
	if (InManifest->TryGetObjectField(TEXT("scope"), scope) && scope->IsValid())
	{
		OutProvenance.Scope = StringField(*scope, TEXT("canonical"));
		const TArray<TSharedPtr<FJsonValue>>* volumes = nullptr;
		if ((*scope)->TryGetArrayField(TEXT("volumes"), volumes))
		{
			const TSharedPtr<FJsonObject>* chosen = nullptr;
			for (const TSharedPtr<FJsonValue>& value : *volumes)
			{
				const TSharedPtr<FJsonObject>* volume = nullptr;
				if (!value->TryGetObject(volume) || !volume->IsValid()) continue;
				const FString slug = StringField(*volume, TEXT("slug"));
				if ((!InVolumeSlug.IsEmpty() && slug == InVolumeSlug) || (InVolumeSlug.IsEmpty() && volumes->Num() == 1))
				{
					chosen = volume;
					break;
				}
			}
			if (chosen)
			{
				OutProvenance.VolumeSlug = StringField(*chosen, TEXT("slug"));
				OutProvenance.VolumeTag = StringField(*chosen, TEXT("tag"));
			}
		}
	}
	else if (InManifest->TryGetStringField(TEXT("scope"), scopeText))
	{
		OutProvenance.Scope = scopeText;
	}
	if (OutProvenance.VolumeSlug.IsEmpty() && !InVolumeSlug.IsEmpty())
		OutProvenance.VolumeSlug = InVolumeSlug;

	const TSharedPtr<FJsonObject>* generation = nullptr;
	if (InManifest->TryGetObjectField(TEXT("generation"), generation) && generation->IsValid())
	{
		OutProvenance.GenerationId = StringField(*generation, TEXT("id"));
		OutProvenance.Revision = RevisionFor((*generation)->TryGetField(TEXT("revision")), OutProvenance.ProjectId);
	}

	// The compiled story artifact's checksum is the build id.
	const TArray<TSharedPtr<FJsonValue>>* artifacts = nullptr;
	if (InManifest->TryGetArrayField(TEXT("artifacts"), artifacts))
	{
		const FString wanted = OutProvenance.VolumeSlug.IsEmpty() ? FString(TEXT("compiled-story")) : TEXT("compiled-story:") + OutProvenance.VolumeSlug;
		FString fallback, fallbackSlug;
		int32 compiledCount = 0;
		for (const TSharedPtr<FJsonValue>& value : *artifacts)
		{
			const TSharedPtr<FJsonObject>* artifact = nullptr;
			if (!value->TryGetObject(artifact) || !artifact->IsValid()) continue;
			const FString key = StringField(*artifact, TEXT("key"));
			const bool bVolumeKey = key.StartsWith(TEXT("compiled-story:"));
			if (key != TEXT("compiled-story") && !bVolumeKey) continue;
			++compiledCount;
			const FString checksum = StringField(*artifact, TEXT("checksum")).ToLower();
			if (key == wanted) { OutProvenance.BuildId = checksum; break; }
			fallback = checksum;
			fallbackSlug = bVolumeKey ? key.RightChop(15) : FString();
		}
		// Only adopt an unmatched artifact (and its volume) when it is the sole compiled story.
		if (OutProvenance.BuildId.IsEmpty() && compiledCount == 1)
		{
			OutProvenance.BuildId = fallback;
			if (OutProvenance.VolumeSlug.IsEmpty())
				OutProvenance.VolumeSlug = fallbackSlug;
		}
	}

	if (InWatchEvent.IsValid())
	{
		if (OutProvenance.ProjectId == 0 && InWatchEvent->TryGetNumberField(TEXT("projectId"), number))
			OutProvenance.ProjectId = static_cast<int32>(number);
		OutProvenance.Channel = StringField(InWatchEvent, TEXT("channel"));
		if (OutProvenance.Scope.IsEmpty())
			OutProvenance.Scope = StringField(InWatchEvent, TEXT("scope"));
		if (OutProvenance.GenerationId.IsEmpty())
			OutProvenance.GenerationId = StringField(InWatchEvent, TEXT("generationId"));
		if (OutProvenance.Revision.IsEmpty())
			OutProvenance.Revision = RevisionFor(InWatchEvent->TryGetField(TEXT("revision")), OutProvenance.ProjectId);
		const TSharedPtr<FJsonObject>* release = nullptr;
		if (InWatchEvent->TryGetObjectField(TEXT("release"), release) && release->IsValid())
		{
			OutProvenance.ReleaseTagId = StringField(*release, TEXT("tagId"));
			OutProvenance.ReleasePublishedAt = StringField(*release, TEXT("publishedAt"));
		}
	}
	return OutProvenance.IsSet();
}

bool FProsettaStoryProvenance::FromSyncManifestJSON(const FString& InManifestJSON, const FString& InVolumeSlug, FProsettaStoryProvenance& OutProvenance)
{
	TSharedPtr<FJsonObject> manifest;
	if (!Parse(InManifestJSON, manifest))
	{
		OutProvenance = FProsettaStoryProvenance();
		return false;
	}
	return FromSyncManifest(manifest, nullptr, InVolumeSlug, OutProvenance);
}

bool FProsettaStoryProvenance::FromSyncManifestFile(const FString& InManifestFilename, const FString& InVolumeSlug, FProsettaStoryProvenance& OutProvenance)
{
	FString text;
	if (!FFileHelper::LoadFileToString(text, *InManifestFilename))
	{
		OutProvenance = FProsettaStoryProvenance();
		return false;
	}
	return FromSyncManifestJSON(text, InVolumeSlug, OutProvenance);
}

bool FProsettaStoryProvenance::FromCompiledWrapper(const FString& InWrapperJSON, FProsettaStoryProvenance& OutProvenance)
{
	OutProvenance = FProsettaStoryProvenance();
	TSharedPtr<FJsonObject> root;
	if (!Parse(InWrapperJSON, root) || !root->HasField(TEXT("story")))
		return false;
	OutProvenance.ProjectName = StringField(root, TEXT("projectName"));
	OutProvenance.GeneratedAt = StringField(root, TEXT("compiledAt"));
	double number = 0;
	if (root->TryGetNumberField(TEXT("projectId"), number))
		OutProvenance.ProjectId = static_cast<int32>(number);
	return OutProvenance.IsSet();
}
