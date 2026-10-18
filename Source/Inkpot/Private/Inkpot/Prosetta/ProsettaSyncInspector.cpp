#include "Inkpot/Prosetta/ProsettaSyncInspector.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TSharedPtr<FJsonObject> InspectorParseObject(const FString& JSON)
	{
		TSharedPtr<FJsonObject> Root;
		const FString Trimmed = JSON.TrimStartAndEnd();
		if (!Trimmed.StartsWith(TEXT("{"))) { return nullptr; }
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
		if (!FJsonSerializer::Deserialize(Reader, Root)) { return nullptr; }
		return Root;
	}

	FString InspectorStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		if (Object.IsValid() && Object->HasTypedField<EJson::String>(Field)) { Object->TryGetStringField(Field, Value); }
		return Value;
	}

	int32 InspectorIntField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		int32 Value = 0;
		if (Object.IsValid() && Object->HasTypedField<EJson::Number>(Field)) { Object->TryGetNumberField(Field, Value); }
		return Value;
	}

	bool InspectorBoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		bool Value = false;
		if (Object.IsValid()) { Object->TryGetBoolField(Field, Value); }
		return Value;
	}

	TArray<FString> InspectorStringArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Values;
		if (Object.IsValid()) { Object->TryGetStringArrayField(Field, Values); }
		return Values;
	}

	bool InspectorTimestampField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FDateTime& Out)
	{
		return UProsettaSyncInspector::ParseTimestamp(InspectorStringField(Object, Field), Out);
	}
}

FString FProsettaScopeVolumeEntry::Describe() const
{
	return Tag.IsEmpty() ? Slug : Slug + TEXT("@") + Tag;
}

const FProsettaScopeVolumeEntry* FProsettaScopeSummary::FindVolume(const FString& Slug) const
{
	return Volumes.FindByPredicate([&Slug](const FProsettaScopeVolumeEntry& V) { return V.Slug == Slug; });
}

FString FProsettaScopeSummary::Describe() const
{
	if (Volumes.IsEmpty()) { return Canonical.IsEmpty() ? TEXT("live") : Canonical; }
	TArray<FString> Parts;
	for (const FProsettaScopeVolumeEntry& Volume : Volumes) { Parts.Add(Volume.Describe()); }
	return FString::Join(Parts, TEXT(", "));
}

FProsettaScopeSummary FProsettaScopeSummary::FromString(const FString& Scope)
{
	FProsettaScopeSummary Summary;
	Summary.Canonical = Scope.TrimStartAndEnd();
	FString Value = Summary.Canonical;
	int32 Equals = INDEX_NONE;
	if (Value.FindChar(TEXT('='), Equals)) { Value = Value.Mid(Equals + 1); }
	TArray<FString> Entries;
	Value.ParseIntoArray(Entries, TEXT(","), true);
	for (FString& Entry : Entries)
	{
		Entry.TrimStartAndEndInline();
		if (Entry.IsEmpty()) { continue; }
		FProsettaScopeVolumeEntry& Volume = Summary.Volumes.AddDefaulted_GetRef();
		int32 At = INDEX_NONE;
		if (Entry.FindChar(TEXT('@'), At)) { Volume.Slug = Entry.Left(At); Volume.Tag = Entry.Mid(At + 1); }
		else { Volume.Slug = Entry; }
	}
	return Summary;
}

FString FProsettaManifestSummary::ContentSha() const
{
	if (Artifacts.IsEmpty()) { return FString(); }
	FString Combined;
	for (const FProsettaArtifactSummary& Artifact : Artifacts) { Combined += Artifact.Key + TEXT(":") + Artifact.Checksum + TEXT("\n"); }
	return FMD5::HashAnsiString(*Combined).Left(12);
}

const FProsettaArtifactSummary* FProsettaManifestSummary::FindArtifact(const FString& Key) const
{
	return Artifacts.FindByPredicate([&Key](const FProsettaArtifactSummary& A) { return A.Key == Key; });
}

TArray<FString> FProsettaManifestSummary::ScopeEntries() const
{
	TArray<FString> Entries;
	FString Value = Scope;
	int32 Equals = INDEX_NONE;
	if (Value.FindChar(TEXT('='), Equals)) { Value = Value.Mid(Equals + 1); }
	Value.ParseIntoArray(Entries, TEXT(","), true);
	for (FString& Entry : Entries) { Entry.TrimStartAndEndInline(); }
	Entries.RemoveAll([](const FString& E) { return E.IsEmpty(); });
	return Entries;
}

bool UProsettaSyncInspector::ParseTimestamp(const FString& Text, FDateTime& Out)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) { return false; }
	return FDateTime::ParseIso8601(*Trimmed, Out);
}

FString UProsettaSyncInspector::DescribeAge(const FDateTime& Then, const FDateTime& Now)
{
	const FTimespan Age = Now - Then;
	if (Age < FTimespan::FromSeconds(-60)) { return TEXT("in the future"); }
	const double Minutes = Age.GetTotalMinutes();
	if (Minutes < 1.0) { return TEXT("just now"); }
	if (Minutes < 60.0) { return FString::Printf(TEXT("%d min ago"), FMath::FloorToInt(Minutes)); }
	const double Hours = Age.GetTotalHours();
	if (Hours < 24.0) { return FString::Printf(TEXT("%d h ago"), FMath::FloorToInt(Hours)); }
	return FString::Printf(TEXT("%d d ago"), FMath::FloorToInt(Age.GetTotalDays()));
}

FString UProsettaSyncInspector::RedactCredentialLine(const FString& Line)
{
	if (Line.Contains(TEXT("token"), ESearchCase::IgnoreCase) || Line.Contains(TEXT("Bearer")) || Line.Contains(TEXT("authorization"), ESearchCase::IgnoreCase))
	{
		return TEXT("[redacted line mentioning credentials]");
	}
	return Line.Left(200);
}

bool UProsettaSyncInspector::ParseManifest(const FString& JSON, FProsettaManifestSummary& Out)
{
	Out = FProsettaManifestSummary();
	const TSharedPtr<FJsonObject> Root = InspectorParseObject(JSON);
	if (!Root.IsValid()) { return false; }
	Out.SchemaVersion = InspectorIntField(Root, TEXT("schemaVersion"));
	if (Out.SchemaVersion != 2) { return false; }
	Out.ProjectId = InspectorIntField(Root, TEXT("projectId"));
	Out.ProjectIdentifier = InspectorStringField(Root, TEXT("projectIdentifier"));
	Out.ProjectName = InspectorStringField(Root, TEXT("projectName"));
	// Scope: a canonical string on older manifests, an object with volumes on scoped ones.
	const TSharedPtr<FJsonObject>* ScopeObject = nullptr;
	if (Root->TryGetObjectField(TEXT("scope"), ScopeObject) && ScopeObject->IsValid())
	{
		Out.ScopeDetail.bFromObject = true;
		Out.ScopeDetail.Canonical = InspectorStringField(*ScopeObject, TEXT("canonical"));
		const TArray<TSharedPtr<FJsonValue>>* Volumes = nullptr;
		if ((*ScopeObject)->TryGetArrayField(TEXT("volumes"), Volumes))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Volumes)
			{
				const TSharedPtr<FJsonObject>* Volume = nullptr;
				FString Entry;
				if (Value.IsValid() && Value->TryGetObject(Volume) && Volume->IsValid())
				{
					FProsettaScopeVolumeEntry& VolumeEntry = Out.ScopeDetail.Volumes.AddDefaulted_GetRef();
					VolumeEntry.Slug = InspectorStringField(*Volume, TEXT("slug"));
					VolumeEntry.Tag = InspectorStringField(*Volume, TEXT("tag"));
					VolumeEntry.CompiledStoryArtifactKey = InspectorStringField(*Volume, TEXT("compiledStoryArtifactKey"));
					VolumeEntry.LinesArtifactKey = InspectorStringField(*Volume, TEXT("linesArtifactKey"));
					VolumeEntry.LocalizationArtifactKey = InspectorStringField(*Volume, TEXT("localizationArtifactKey"));
				}
				else if (Value.IsValid() && Value->TryGetString(Entry))
				{
					Out.ScopeDetail.Volumes.Append(FProsettaScopeSummary::FromString(Entry).Volumes);
				}
			}
		}
		if (Out.ScopeDetail.Canonical.IsEmpty() && !Out.ScopeDetail.Volumes.IsEmpty())
		{
			TArray<FString> Parts;
			for (const FProsettaScopeVolumeEntry& Volume : Out.ScopeDetail.Volumes) { Parts.Add(Volume.Describe()); }
			Out.ScopeDetail.Canonical = TEXT("volumes=") + FString::Join(Parts, TEXT(","));
		}
		Out.Scope = Out.ScopeDetail.Canonical;
	}
	else
	{
		Out.Scope = InspectorStringField(Root, TEXT("scope"));
		Out.ScopeDetail = FProsettaScopeSummary::FromString(Out.Scope);
	}
	Out.bHasGeneratedAt = InspectorTimestampField(Root, TEXT("generatedAt"), Out.GeneratedAt);
	const TSharedPtr<FJsonObject>* Generation = nullptr;
	if (Root->TryGetObjectField(TEXT("generation"), Generation) && Generation->IsValid())
	{
		Out.GenerationId = InspectorStringField(*Generation, TEXT("id"));
		// Revision: a string, or a map of namespace -> revision on multi-namespace generations.
		const TSharedPtr<FJsonValue> Revision = (*Generation)->TryGetField(TEXT("revision"));
		const TSharedPtr<FJsonObject>* RevisionMap = nullptr;
		FString RevisionText;
		if (Revision.IsValid() && Revision->TryGetString(RevisionText))
		{
			Out.GenerationRevision = RevisionText;
			Out.GenerationRevisions.Add(TEXT("*"), RevisionText);
		}
		else if (Revision.IsValid() && Revision->TryGetObject(RevisionMap) && RevisionMap->IsValid())
		{
			for (const auto& Pair : (*RevisionMap)->Values)
			{
				FString Value;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value)) { Out.GenerationRevisions.Add(FString(Pair.Key), Value); }
				else if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Number) { Out.GenerationRevisions.Add(FString(Pair.Key), FString::FromInt(static_cast<int32>(Pair.Value->AsNumber()))); }
			}
			if (const FString* Star = Out.GenerationRevisions.Find(TEXT("*"))) { Out.GenerationRevision = *Star; }
			else if (const FString* Project = Out.GenerationRevisions.Find(FString::FromInt(Out.ProjectId))) { Out.GenerationRevision = *Project; }
			else if (!Out.GenerationRevisions.IsEmpty())
			{
				TArray<FString> Keys;
				Out.GenerationRevisions.GetKeys(Keys);
				Keys.Sort();
				Out.GenerationRevision = Out.GenerationRevisions[Keys[0]];
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Artifacts = nullptr;
	if (Root->TryGetArrayField(TEXT("artifacts"), Artifacts))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Artifacts)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object->IsValid()) { continue; }
			FProsettaArtifactSummary& Artifact = Out.Artifacts.AddDefaulted_GetRef();
			Artifact.Key = InspectorStringField(*Object, TEXT("key"));
			Artifact.RelativePath = InspectorStringField(*Object, TEXT("relativePath"));
			Artifact.Checksum = InspectorStringField(*Object, TEXT("checksum"));
			double Size = 0;
			if ((*Object)->TryGetNumberField(TEXT("byteSize"), Size)) { Artifact.ByteSize = static_cast<int64>(Size); }
			Artifact.bHasUpdatedAt = InspectorTimestampField(*Object, TEXT("updatedAt"), Artifact.UpdatedAt);
		}
	}
	return true;
}

bool UProsettaSyncInspector::LoadManifestFile(const FString& Path, FProsettaManifestSummary& Out)
{
	FString JSON;
	if (!FFileHelper::LoadFileToString(JSON, *Path) || !ParseManifest(JSON, Out)) { Out = FProsettaManifestSummary(); return false; }
	Out.Path = Path;
	return true;
}

bool UProsettaSyncInspector::ParseVolumesListing(const FString& JSON, FProsettaVolumesListing& Out)
{
	Out = FProsettaVolumesListing();
	const TSharedPtr<FJsonObject> Root = InspectorParseObject(JSON);
	if (!Root.IsValid()) { return false; }
	Out.ProjectId = InspectorIntField(Root, TEXT("projectId"));
	Out.ProjectIdentifier = InspectorStringField(Root, TEXT("projectIdentifier"));
	Out.ProjectName = InspectorStringField(Root, TEXT("projectName"));
	const TArray<TSharedPtr<FJsonValue>>* Volumes = nullptr;
	if (!Root->TryGetArrayField(TEXT("volumes"), Volumes)) { return false; }
	for (const TSharedPtr<FJsonValue>& Value : *Volumes)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object->IsValid()) { continue; }
		FProsettaVolumeSummary& Volume = Out.Volumes.AddDefaulted_GetRef();
		Volume.Slug = InspectorStringField(*Object, TEXT("slug"));
		Volume.Name = InspectorStringField(*Object, TEXT("name"));
		Volume.bIsDefault = InspectorBoolField(*Object, TEXT("isDefault"));
		Volume.Status = InspectorStringField(*Object, TEXT("status"));
		Volume.ScopeParameter = InspectorStringField(*Object, TEXT("scopeParameter"));
		Volume.bHasLastCompiledAt = InspectorTimestampField(*Object, TEXT("lastCompiledAt"), Volume.LastCompiledAt);
		Volume.ErrorCount = InspectorIntField(*Object, TEXT("errorCount"));
		Volume.WarningCount = InspectorIntField(*Object, TEXT("warningCount"));
		const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
		if ((*Object)->TryGetArrayField(TEXT("tags"), Tags))
		{
			for (const TSharedPtr<FJsonValue>& Tag : *Tags)
			{
				if (!Tag.IsValid()) { continue; }
				const TSharedPtr<FJsonObject>* TagObject = nullptr;
				if (Tag->TryGetObject(TagObject) && TagObject->IsValid())
				{
					FString Name = InspectorStringField(*TagObject, TEXT("tag"));
					if (Name.IsEmpty()) { Name = InspectorStringField(*TagObject, TEXT("name")); }
					if (Name.IsEmpty()) { Name = InspectorStringField(*TagObject, TEXT("tagId")); }
					if (!Name.IsEmpty()) { Volume.Tags.Add(Name); }
				}
				else if (Tag->Type == EJson::String) { Volume.Tags.Add(Tag->AsString()); }
			}
		}
	}
	return true;
}

bool UProsettaSyncInspector::ParseWatchEvent(const FString& Line, FProsettaWatchEvent& Out)
{
	Out = FProsettaWatchEvent();
	const TSharedPtr<FJsonObject> Root = InspectorParseObject(Line);
	if (!Root.IsValid()) { return false; }
	Out.SchemaVersion = InspectorIntField(Root, TEXT("schemaVersion"));
	Out.Type = InspectorStringField(Root, TEXT("type"));
	if (Out.SchemaVersion != 1 || Out.Type.IsEmpty()) { return false; }
	Out.ProjectId = InspectorIntField(Root, TEXT("projectId"));
	Out.Scope = InspectorStringField(Root, TEXT("scope"));
	Out.Channel = InspectorStringField(Root, TEXT("channel"));
	Out.GenerationId = InspectorStringField(Root, TEXT("generationId"));
	Out.GenerationPath = InspectorStringField(Root, TEXT("generationPath"));
	Out.ManifestPath = InspectorStringField(Root, TEXT("manifestPath"));
	Out.Code = InspectorStringField(Root, TEXT("code"));
	Out.Message = InspectorStringField(Root, TEXT("message"));
	Out.ChangedKeys = InspectorStringArrayField(Root, TEXT("changedKeys"));
	Out.RemovedKeys = InspectorStringArrayField(Root, TEXT("removedKeys"));
	const TSharedPtr<FJsonObject>* Revision = nullptr;
	if (Root->TryGetObjectField(TEXT("revision"), Revision) && Revision->IsValid())
	{
		for (const auto& Pair : (*Revision)->Values)
		{
			FString Value;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value)) { Out.Revisions.Add(FString::Printf(TEXT("%s@%s"), *FString(Pair.Key), *Value)); }
			else if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Number) { Out.Revisions.Add(FString::Printf(TEXT("%s@%d"), *FString(Pair.Key), static_cast<int32>(Pair.Value->AsNumber()))); }
		}
		Out.Revisions.Sort();
	}
	const TSharedPtr<FJsonObject>* Release = nullptr;
	if (Root->TryGetObjectField(TEXT("release"), Release) && Release->IsValid())
	{
		Out.ReleaseState = InspectorStringField(*Release, TEXT("state"));
		Out.ReleaseTagId = InspectorStringField(*Release, TEXT("tagId"));
	}
	return true;
}

bool UProsettaSyncInspector::ReadCliConfigPaths(const FString& ConfigJSON, FString& OutCachePath, FString& OutMirrorPath)
{
	// Only the two path fields are read. The token stays in the file and is never surfaced.
	const TSharedPtr<FJsonObject> Root = InspectorParseObject(ConfigJSON);
	if (!Root.IsValid()) { return false; }
	OutCachePath = InspectorStringField(Root, TEXT("cachePath"));
	OutMirrorPath = InspectorStringField(Root, TEXT("mirrorPath"));
	return !OutCachePath.IsEmpty() || !OutMirrorPath.IsEmpty();
}

TArray<FString> UProsettaSyncInspector::CliConfigCandidates(const FString& ProjectDir)
{
	TArray<FString> Candidates;
	Candidates.Add(FPaths::Combine(ProjectDir, TEXT(".prosetta-sync/config.json")));
	Candidates.Add(FPaths::Combine(ProjectDir, TEXT("Saved/ProsettaSync/prosetta-sync.config.json")));
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!Home.IsEmpty())
	{
		Candidates.Add(FPaths::Combine(Home, TEXT(".prosetta-sync/config.json")));
#if PLATFORM_MAC
		Candidates.Add(FPaths::Combine(Home, TEXT("Library/Application Support/Prosetta/SyncTool/config.json")));
#else
		Candidates.Add(FPaths::Combine(Home, TEXT(".config/Prosetta/SyncTool/config.json")));
#endif
	}
	return Candidates;
}

void UProsettaSyncInspector::ResolveLocalPaths(const FString& ProjectDir, FString& OutCachePath, FString& OutMirrorPath)
{
	OutCachePath.Reset();
	OutMirrorPath = FPaths::Combine(ProjectDir, TEXT("Saved/Prosetta"));
	for (const FString& Candidate : CliConfigCandidates(ProjectDir))
	{
		FString JSON;
		if (!FFileHelper::LoadFileToString(JSON, *Candidate)) { continue; }
		FString Cache, Mirror;
		if (ReadCliConfigPaths(JSON, Cache, Mirror))
		{
			if (!Cache.IsEmpty()) { OutCachePath = Cache; }
			if (!Mirror.IsEmpty()) { OutMirrorPath = Mirror; }
			return;
		}
	}
}

TArray<FProsettaCacheSummary> UProsettaSyncInspector::ScanCacheRoot(const FString& CacheRoot)
{
	TArray<FProsettaCacheSummary> Caches;
	if (CacheRoot.IsEmpty()) { return Caches; }
	IFileManager& FileManager = IFileManager::Get();
	// The root may itself be a single namespace (cachePath points at a project cache) or a parent of namespaces.
	TArray<FString> Directories;
	if (FileManager.FileExists(*FPaths::Combine(CacheRoot, TEXT("manifest.json")))) { Directories.Add(CacheRoot); }
	else
	{
		TArray<FString> Children;
		FileManager.FindFiles(Children, *FPaths::Combine(CacheRoot, TEXT("*")), false, true);
		Children.Sort();
		for (const FString& Child : Children)
		{
			if (Child.EndsWith(TEXT(".generations"))) { continue; }
			Directories.Add(FPaths::Combine(CacheRoot, Child));
		}
	}
	for (const FString& Directory : Directories)
	{
		FProsettaCacheSummary& Cache = Caches.AddDefaulted_GetRef();
		Cache.Directory = Directory;
		Cache.Name = FPaths::GetCleanFilename(Directory);
		Cache.bHasManifest = LoadManifestFile(FPaths::Combine(Directory, TEXT("manifest.json")), Cache.Manifest);
		const FString GenerationsRoot = Directory + TEXT(".generations");
		FString CurrentId;
		FString CurrentJSON;
		if (FFileHelper::LoadFileToString(CurrentJSON, *FPaths::Combine(GenerationsRoot, TEXT("current.json"))))
		{
			const TSharedPtr<FJsonObject> Current = InspectorParseObject(CurrentJSON);
			CurrentId = InspectorStringField(Current, TEXT("generationId"));
			if (CurrentId.IsEmpty()) { CurrentId = InspectorStringField(Current, TEXT("id")); }
		}
		TArray<FString> GenerationDirs;
		FileManager.FindFiles(GenerationDirs, *FPaths::Combine(GenerationsRoot, TEXT("*")), false, true);
		for (const FString& GenerationDir : GenerationDirs)
		{
			FProsettaGenerationSummary Generation;
			Generation.Directory = FPaths::Combine(GenerationsRoot, GenerationDir);
			Generation.GenerationId = GenerationDir;
			if (!LoadManifestFile(FPaths::Combine(Generation.Directory, TEXT("manifest.json")), Generation.Manifest)) { continue; }
			if (!Generation.Manifest.GenerationId.IsEmpty()) { Generation.GenerationId = Generation.Manifest.GenerationId; }
			Generation.bIsCurrent = !CurrentId.IsEmpty() && (CurrentId == Generation.GenerationId || CurrentId == GenerationDir);
			Cache.Generations.Add(MoveTemp(Generation));
		}
		Cache.Generations.Sort([](const FProsettaGenerationSummary& A, const FProsettaGenerationSummary& B) { return A.Manifest.GeneratedAt > B.Manifest.GeneratedAt; });
	}
	return Caches;
}

FString UProsettaSyncInspector::ResolveCliPath()
{
	TArray<FString> Candidates;
	const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("PROSETTA_SYNC_PATH"));
	if (!Override.IsEmpty()) { Candidates.Add(Override); }
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!Home.IsEmpty()) { Candidates.Add(FPaths::Combine(Home, TEXT(".local/bin/prosetta-sync"))); }
	Candidates.Add(TEXT("/usr/local/bin/prosetta-sync"));
	Candidates.Add(TEXT("/opt/homebrew/bin/prosetta-sync"));
	for (const FString& Candidate : Candidates)
	{
		if (IFileManager::Get().FileExists(*Candidate)) { return Candidate; }
	}
	return FString();
}
