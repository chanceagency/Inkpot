#include "ProsettaProfileStore.h"
#include "DirectoryWatcherModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "ISourceControlModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SourceControlHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaProfiles, Log, All);

FProsettaProfileStore::FProsettaProfileStore(FString InDirectory)
	: Directory(FPaths::ConvertRelativePathToFull(MoveTemp(InDirectory)))
{
}

FProsettaProfileStore::~FProsettaProfileStore()
{
	if (WatchHandle.IsValid())
	{
		if (FDirectoryWatcherModule* Module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
		{
			if (IDirectoryWatcher* Watcher = Module->Get()) { Watcher->UnregisterDirectoryChangedCallback_Handle(Directory, WatchHandle); }
		}
	}
}

FString FProsettaProfileStore::DefaultDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("ProsettaSync"), TEXT("Profiles")));
}

FString FProsettaProfileStore::PathFor(const FString& Name) const
{
	return FPaths::Combine(Directory, Name + TEXT(".json"));
}

bool FProsettaProfileStore::IsValidName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Len() > 64 || Name.StartsWith(TEXT(".")) || Name.TrimStartAndEnd() != Name) { return false; }
	for (const TCHAR C : Name)
	{
		if (!FChar::IsAlnum(C) && C != TEXT(' ') && C != TEXT('-') && C != TEXT('_') && C != TEXT('.')) { return false; }
	}
	return true;
}

TArray<FString> FProsettaProfileStore::List() const
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	TArray<FString> Names;
	for (const FString& File : Files)
	{
		const FString Name = FPaths::GetBaseFilename(File);
		if (IsValidName(Name)) { Names.Add(Name); }
	}
	Names.Sort();
	return Names;
}

bool FProsettaProfileStore::Load(const FString& Name, FProsettaSyncProfile& Out, FString& OutError) const
{
	if (!IsValidName(Name)) { OutError = TEXT("invalid profile name"); return false; }
	FString Json;
	const FString Path = PathFor(Name);
	if (!FFileHelper::LoadFileToString(Json, *Path)) { OutError = TEXT("could not read ") + Path; return false; }
	FString Key;
	if (ContainsSecretKey(Json, Key))
	{
		OutError = FString::Printf(TEXT("%s holds a '%s' key; profiles must not contain credentials. Remove it and keep the token in .prosetta-sync/config.json."), *Path, *Key);
		return false;
	}
	if (!FProsettaSyncProfile::FromJson(Json, Out, OutError)) { OutError = Path + TEXT(": ") + OutError; return false; }
	Out.Name = Name;
	return true;
}

bool FProsettaProfileStore::ShouldUseSourceControl(const FString& Path) const
{
	if (!bUseSourceControl || !ISourceControlModule::Get().IsEnabled()) { return false; }
	const FString Full = FPaths::ConvertRelativePathToFull(Path);
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	return FPaths::IsUnderDirectory(Full, Project) && !FPaths::IsUnderDirectory(Full, Saved);
}

bool FProsettaProfileStore::WriteFile(const FString& Path, const FString& Contents, FString& OutError)
{
	FString Key;
	if (ContainsSecretKey(Contents, Key))
	{
		OutError = FString::Printf(TEXT("refusing to write a profile with a '%s' key"), *Key);
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) { OutError = TEXT("could not create ") + FPaths::GetPath(Path); return false; }
	const bool bSourceControl = ShouldUseSourceControl(Path);
	if (bSourceControl && FPaths::FileExists(Path))
	{
		// Silent: an unavailable provider must not block a local edit.
		USourceControlHelpers::CheckOutOrAddFile(Path, true);
	}
	if (!FFileHelper::SaveStringToFile(Contents, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { OutError = TEXT("could not write ") + Path; return false; }
	if (bSourceControl) { USourceControlHelpers::CheckOutOrAddFile(Path, true); }
	OnListChanged.Broadcast();
	return true;
}

bool FProsettaProfileStore::Save(const FProsettaSyncProfile& Profile, FString& OutError)
{
	if (!IsValidName(Profile.Name)) { OutError = TEXT("invalid profile name"); return false; }
	return WriteFile(PathFor(Profile.Name), Profile.ToJson(), OutError);
}

bool FProsettaProfileStore::SaveAs(FProsettaSyncProfile Profile, const FString& NewName, FString& OutError)
{
	Profile.Name = NewName;
	return Save(Profile, OutError);
}

bool FProsettaProfileStore::Delete(const FString& Name, FString& OutError)
{
	if (!IsValidName(Name)) { OutError = TEXT("invalid profile name"); return false; }
	const FString Path = PathFor(Name);
	if (!FPaths::FileExists(Path)) { OutError = TEXT("no such profile"); return false; }
	if (ShouldUseSourceControl(Path)) { USourceControlHelpers::MarkFileForDelete(Path, true); }
	if (FPaths::FileExists(Path) && !IFileManager::Get().Delete(*Path)) { OutError = TEXT("could not delete ") + Path; return false; }
	OnListChanged.Broadcast();
	return true;
}

FProsettaSyncProfile FProsettaProfileStore::EnsureDefault(const FString& DefaultName)
{
	FProsettaSyncProfile Profile;
	FString Error;
	if (Load(DefaultName, Profile, Error)) { return Profile; }
	Profile = FProsettaSyncProfile();
	Profile.Name = DefaultName;
	if (List().IsEmpty())
	{
		if (!Save(Profile, Error)) { UE_LOG(LogProsettaProfiles, Warning, TEXT("Could not create the default Prosetta profile: %s"), *Error); }
	}
	return Profile;
}

bool FProsettaProfileStore::IsDirty(const FProsettaSyncProfile& Edited) const
{
	FProsettaSyncProfile Saved;
	FString Error;
	return !Load(Edited.Name, Saved, Error) || Saved != Edited;
}

bool FProsettaProfileStore::ContainsSecretKey(const TSharedPtr<FJsonObject>& Json, FString& OutKey)
{
	if (!Json.IsValid()) { return false; }
	for (const auto& Pair : Json->Values)
	{
		const FString Key(Pair.Key);
		const FString Lower = Key.ToLower();
		if (Lower.Contains(TEXT("token")) || Lower.Contains(TEXT("secret")) || Lower.Contains(TEXT("password")) || Lower.Contains(TEXT("apikey")) || Lower.Contains(TEXT("api_key")) || Lower.Contains(TEXT("authorization")))
		{
			OutKey = Key;
			return true;
		}
		const TSharedPtr<FJsonObject>* Child = nullptr;
		if (Pair.Value.IsValid() && Pair.Value->TryGetObject(Child) && ContainsSecretKey(*Child, OutKey)) { return true; }
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (Pair.Value.IsValid() && Pair.Value->TryGetArray(Items))
		{
			for (const TSharedPtr<FJsonValue>& Item : *Items)
			{
				if (Item.IsValid() && Item->TryGetObject(Child) && ContainsSecretKey(*Child, OutKey)) { return true; }
			}
		}
	}
	return false;
}

bool FProsettaProfileStore::ContainsSecretKey(const FString& Json, FString& OutKey)
{
	TSharedPtr<FJsonObject> Object;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) && ContainsSecretKey(Object, OutKey);
}

void FProsettaProfileStore::WatchDirectory()
{
	if (WatchHandle.IsValid()) { return; }
	IFileManager::Get().MakeDirectory(*Directory, true);
	FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	if (IDirectoryWatcher* Watcher = Module.Get())
	{
		Watcher->RegisterDirectoryChangedCallback_Handle(Directory,
			IDirectoryWatcher::FDirectoryChanged::CreateLambda([this](const TArray<FFileChangeData>&) { OnListChanged.Broadcast(); }),
			WatchHandle, 0);
	}
}
