#include "ProsettaSyncProfile.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "ProsettaSyncProfile"

FString FProsettaVolumeScope::ToString() const
{
	return Tag.IsEmpty() ? Slug : Slug + TEXT("@") + Tag;
}

FProsettaVolumeScope FProsettaVolumeScope::Parse(const FString& Entry)
{
	FProsettaVolumeScope Scope;
	if (!Entry.TrimStartAndEnd().Split(TEXT("@"), &Scope.Slug, &Scope.Tag)) { Scope.Slug = Entry.TrimStartAndEnd(); }
	return Scope;
}

bool FProsettaVolumeScope::IsValidSlug(const FString& Slug)
{
	if (Slug.IsEmpty()) { return false; }
	for (const TCHAR C : Slug)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_')) { return false; }
	}
	return true;
}

bool FProsettaSyncProfile::Validate(TArray<FText>& OutErrors) const
{
	const int32 Before = OutErrors.Num();
	if (Mode != EProsettaSyncMode::Verify)
	{
		TSet<FString> Seen;
		for (const FProsettaVolumeScope& Volume : Volumes)
		{
			if (!FProsettaVolumeScope::IsValidSlug(Volume.Slug))
			{
				OutErrors.Add(FText::Format(LOCTEXT("BadSlug", "Volume slug '{0}' must be letters, digits, '-' or '_'."), FText::FromString(Volume.Slug)));
			}
			else if (Seen.Contains(Volume.Slug))
			{
				OutErrors.Add(FText::Format(LOCTEXT("DuplicateSlug", "Volume '{0}' is listed twice."), FText::FromString(Volume.Slug)));
			}
			Seen.Add(Volume.Slug);
			if (Mode == EProsettaSyncMode::Watch && Volume.IsFixedTag())
			{
				OutErrors.Add(FText::Format(LOCTEXT("FixedTagWatch", "Volume '{0}' pins release '{1}'; fixed tags use one-shot sync, so set mode to Sync once."), FText::FromString(Volume.Slug), FText::FromString(Volume.Tag)));
			}
		}
	}
	if (Mode == EProsettaSyncMode::Watch)
	{
		if (Channel == EProsettaWatchChannel::LatestRelease && Volumes.Num() != 1)
		{
			OutErrors.Add(LOCTEXT("LatestReleaseOneVolume", "The latest-release channel watches exactly one volume."));
		}
		// A relative name ("ffmpeg") is looked up on PATH when the decoder is resolved (ResolveProsettaAudioDecoder).
		if (bImportOnAccept && !AudioDecoderPath.FilePath.IsEmpty() && !FPaths::IsRelative(AudioDecoderPath.FilePath) && !FPaths::FileExists(AudioDecoderPath.FilePath))
		{
			OutErrors.Add(FText::Format(LOCTEXT("DecoderMissing", "Audio decoder not found: {0}"), FText::FromString(AudioDecoderPath.FilePath)));
		}
	}
	if (!BaseUrl.IsEmpty())
	{
		const FString Lower = BaseUrl.ToLower();
		const bool bHttps = Lower.StartsWith(TEXT("https://"));
		const bool bLoopback = Lower.StartsWith(TEXT("http://localhost")) || Lower.StartsWith(TEXT("http://127.0.0.1")) || Lower.StartsWith(TEXT("http://[::1]"));
		if (!bHttps && !bLoopback)
		{
			OutErrors.Add(LOCTEXT("BaseUrlScheme", "Base URL must use https, or http only for localhost / 127.0.0.1."));
		}
		if (Lower.Contains(TEXT("api.prosetta.dev")))
		{
			OutErrors.Add(LOCTEXT("StagingRelay", "api.prosetta.dev is behind Vercel protection: run the loopback relay (prosetta.py relay, which needs the project's Scripts/prosetta-relay.py) and use http://127.0.0.1:61417."));
		}
	}
	if (!ProjectId.IsEmpty() && !ProjectId.IsNumeric())
	{
		OutErrors.Add(LOCTEXT("ProjectIdNumeric", "Project id must be a number."));
	}
	// Project-relative like the PROSETTA_SYNC_BIN export (FProsettaDashboardModule::ApplyProfileEnvironment).
	if (!CliBinaryOverride.FilePath.IsEmpty() && !FPaths::FileExists(FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), CliBinaryOverride.FilePath)))
	{
		OutErrors.Add(FText::Format(LOCTEXT("CliMissing", "CLI binary not found: {0}"), FText::FromString(CliBinaryOverride.FilePath)));
	}
	return OutErrors.Num() == Before;
}

FString FProsettaSyncProfile::ScopeString() const
{
	TArray<FString> Entries;
	for (const FProsettaVolumeScope& Volume : Volumes) { Entries.Add(Volume.ToString()); }
	return FString::Join(Entries, TEXT(","));
}

bool FProsettaSyncProfile::IsFixedTag() const
{
	return Volumes.ContainsByPredicate([](const FProsettaVolumeScope& V) { return V.IsFixedTag(); });
}

FString FProsettaSyncProfile::ChannelName() const
{
	return Channel == EProsettaWatchChannel::LatestRelease ? TEXT("latest-release") : TEXT("live");
}

TArray<FString> FProsettaSyncProfile::ConnectionFlags() const
{
	TArray<FString> Flags;
	if (!BaseUrl.IsEmpty()) { Flags.Append({TEXT("--base-url"), BaseUrl}); }
	if (!ProjectId.IsEmpty()) { Flags.Append({TEXT("--project-id"), ProjectId}); }
	return Flags;
}

TArray<FString> FProsettaSyncProfile::BuildWatchFlags() const
{
	TArray<FString> Flags = {TEXT("--channel"), ChannelName()};
	if (!Volumes.IsEmpty()) { Flags.Append({TEXT("--volumes"), ScopeString()}); }
	if (bNoMirror) { Flags.Add(TEXT("--no-mirror")); }
	Flags.Append(ConnectionFlags());
	return Flags;
}

TArray<FString> FProsettaSyncProfile::BuildSyncFlags() const
{
	TArray<FString> Flags;
	if (bRepair) { Flags.Add(TEXT("--repair")); }
	if (bSnapshot) { Flags.Add(TEXT("--snapshot")); }
	Flags.Append(ConnectionFlags());
	return Flags;
}

FString FProsettaSyncProfile::ToJson() const
{
	FString Json;
	FJsonObjectConverter::UStructToJsonObjectString(*this, Json, 0, 0, 0, nullptr, true);
	return Json;
}

bool FProsettaSyncProfile::FromJson(const FString& Json, FProsettaSyncProfile& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
	{
		OutError = TEXT("not a JSON object");
		return false;
	}
	int32 Schema = CurrentSchemaVersion;
	if (Object->TryGetNumberField(TEXT("schemaVersion"), Schema) && Schema > CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("profile schemaVersion %d is newer than this editor supports (%d)"), Schema, CurrentSchemaVersion);
		return false;
	}
	FProsettaSyncProfile Parsed;
	if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Parsed, 0, 0))
	{
		OutError = TEXT("profile fields could not be read");
		return false;
	}
	Parsed.SchemaVersion = CurrentSchemaVersion;
	Out = MoveTemp(Parsed);
	return true;
}

bool FProsettaSyncProfile::operator==(const FProsettaSyncProfile& Other) const
{
	return Name == Other.Name && Mode == Other.Mode && Channel == Other.Channel && Volumes == Other.Volumes && bNoMirror == Other.bNoMirror
		&& bImportOnAccept == Other.bImportOnAccept && AudioDecoderPath.FilePath == Other.AudioDecoderPath.FilePath && bRepair == Other.bRepair
		&& bSnapshot == Other.bSnapshot && BaseUrl == Other.BaseUrl && ProjectId == Other.ProjectId && CliBinaryOverride.FilePath == Other.CliBinaryOverride.FilePath
		&& bAutoStartWatchOnEditorLaunch == Other.bAutoStartWatchOnEditorLaunch && CachePath == Other.CachePath && MirrorPath == Other.MirrorPath;
}

#undef LOCTEXT_NAMESPACE
