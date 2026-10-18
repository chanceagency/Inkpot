#include "ProsettaStoryAssetBuild.h"
#include "ProsettaDashboardSettings.h"
#include "Inkpot/Prosetta/ProsettaStorySource.h"
#include "Asset/InkpotStoryAsset.h"
#include "Asset/InkpotStoryAssetFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "EditorReimportHandler.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaStoryAssetBuild, Log, All);

FString ProsettaDefaultImportStageDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ProsettaImport")));
}

FString ProsettaDefaultStoryAssetPath()
{
	const UProsettaSyncProjectSettings* Settings = GetDefault<UProsettaSyncProjectSettings>();
	return Settings && !Settings->StoryAssetPath.IsEmpty() ? Settings->StoryAssetPath : FString(TEXT("/Game/ProsettaSyncedStory"));
}

FString ProsettaVolumeAssetPath(const FString& Slug)
{
	return FString::Printf(TEXT("/Game/Prosetta/Volumes/%s/Story"), *Slug);
}

bool SplitProsettaAssetPath(const FString& InAssetPath, FString& OutPackagePath, FString& OutAssetName)
{
	FString Normalized = InAssetPath;
	const int32 DotIndex = Normalized.Find(TEXT("."));
	if (DotIndex != INDEX_NONE) { Normalized = Normalized.Left(DotIndex); }
	int32 SlashIndex = INDEX_NONE;
	if (!Normalized.FindLastChar(TEXT('/'), SlashIndex) || SlashIndex <= 0 || SlashIndex == Normalized.Len() - 1) { return false; }
	OutPackagePath = Normalized.Left(SlashIndex);
	OutAssetName = Normalized.Mid(SlashIndex + 1);
	return FPackageName::IsValidLongPackageName(OutPackagePath) && FPackageName::IsValidLongPackageName(Normalized);
}

bool ReadProsettaCompiledStory(const FString& Path, FString& OutInkJson, FString& OutError)
{
	FString Export;
	TSharedPtr<FJsonObject> Object;
	if (!FFileHelper::LoadFileToString(Export, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Export), Object) || !Object.IsValid())
	{
		OutError = FString::Printf(TEXT("Could not read compiled story JSON: %s"), *Path);
		return false;
	}
	const TSharedPtr<FJsonObject>* Wrapped = nullptr;
	if (Object->TryGetObjectField(TEXT("story"), Wrapped)) { Object = *Wrapped; }
	int32 InkVersion = 0;
	if (!Object->TryGetNumberField(TEXT("inkVersion"), InkVersion) || InkVersion <= 0 || !Object->HasTypedField<EJson::Array>(TEXT("root")) ||
		!FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&OutInkJson)))
	{
		OutError = FString::Printf(TEXT("Compiled story is missing a valid Ink version/root: %s"), *Path);
		return false;
	}
	return true;
}

namespace
{
	bool SaveStaged(const FString& Path, const FString& Contents, FString& OutError)
	{
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) || !FFileHelper::SaveStringToFile(Contents, *Path))
		{
			OutError = FString::Printf(TEXT("Could not write staged file: %s"), *Path);
			return false;
		}
		return true;
	}

	/** The story package plus the dirty packages the presentation import wrote: <Story>_Presentation and everything under <dir>/ProsettaMedia. */
	TArray<UPackage*> BuiltPackages(UObject* Asset)
	{
		UPackage* StoryPackage = Asset->GetOutermost();
		const FString PresentationName = StoryPackage->GetName() + TEXT("_Presentation");
		const FString MediaPrefix = FPaths::GetPath(StoryPackage->GetName()) + TEXT("/ProsettaMedia/");
		TArray<UPackage*> Packages = { StoryPackage };
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			if (Package == StoryPackage || !Package->IsDirty()) { continue; }
			const FString Name = Package->GetName();
			if (Name == PresentationName || Name.StartsWith(MediaPrefix)) { Packages.Add(Package); }
		}
		return Packages;
	}

	bool SaveDirty(const FProsettaStoryAssetBuildOptions& Options, UObject* Asset, FString& OutError)
	{
		if (!Options.bSave) { return true; }
		// Only what this build wrote: never the user's unrelated unsaved edits.
		const TArray<UPackage*> Packages = BuiltPackages(Asset);
		if (IsRunningCommandlet() || GIsAutomationTesting)
		{
			for (UPackage* Package : Packages)
			{
				const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
				FSavePackageArgs Args;
				Args.TopLevelFlags = RF_Public | RF_Standalone;
				Args.SaveFlags = SAVE_NoError;
				if (!UPackage::SavePackage(Package, Package == Asset->GetOutermost() ? Asset : Package->FindAssetInPackage(), *Filename, Args))
				{
					OutError = FString::Printf(TEXT("Saving %s failed."), *Filename);
					return false;
				}
			}
			return true;
		}
		if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true))
		{
			OutError = TEXT("Import succeeded but saving the story and presentation packages failed.");
			return false;
		}
		return true;
	}
}

FProsettaStoryAssetBuildResult RebuildProsettaStoryAsset(const FProsettaStoryAssetBuildOptions& InOptions)
{
	FProsettaStoryAssetBuildResult Result;
	FProsettaStoryAssetBuildOptions Options = InOptions;
	if (Options.MirrorRoot.IsEmpty()) { Options.MirrorRoot = ProsettaStorySource::MirrorRoot(); }
	Options.MirrorRoot = FPaths::ConvertRelativePathToFull(Options.MirrorRoot);
	if (Options.AssetPath.IsEmpty()) { Options.AssetPath = Options.VolumeSlug.IsEmpty() ? ProsettaDefaultStoryAssetPath() : ProsettaVolumeAssetPath(Options.VolumeSlug); }
	if (Options.StageDir.IsEmpty()) { Options.StageDir = ProsettaDefaultImportStageDir(); }
	Options.StageDir = FPaths::ConvertRelativePathToFull(Options.StageDir);
	if (FPaths::IsUnderDirectory(Options.StageDir, Options.MirrorRoot))
	{
		Result.Error = TEXT("The stage directory must not be inside the Saved/Prosetta mirror.");
		return Result;
	}

	FString PackagePath, AssetName;
	if (!SplitProsettaAssetPath(Options.AssetPath, PackagePath, AssetName))
	{
		Result.Error = FString::Printf(TEXT("Invalid asset path: %s"), *Options.AssetPath);
		return Result;
	}
	Result.ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);

	// Inputs: explicit paths win, otherwise the manifest decides where the volume's files are.
	FProsettaResolvedStoryPaths Resolved;
	ProsettaStorySource::ResolveStoryPaths(Options.MirrorRoot, Options.VolumeSlug, Resolved);
	if (!Options.VolumeSlug.IsEmpty() && Resolved.bFellBackToCanonical && Options.CompiledStoryPath.IsEmpty())
	{
		Result.Error = TEXT("Cannot rebuild: ") + Resolved.Note;
		return Result;
	}
	const bool bExplicitCompiled = !Options.CompiledStoryPath.IsEmpty();
	const FString Concatenated = FPaths::Combine(Options.MirrorRoot, TEXT("concatenated"), TEXT("source.ink"));
	// The mirror's compiled story belongs to the mirror's source only; a custom source ink keeps its own compile.
	if (!bExplicitCompiled && (Options.SourceInkPath.IsEmpty() || FPaths::IsSamePath(Options.SourceInkPath, Concatenated)))
	{
		Options.CompiledStoryPath = Resolved.CompiledStoryPath;
	}
	if (Options.LinesPath.IsEmpty()) { Options.LinesPath = Resolved.LinesPath; }
	if (Options.SourceInkPath.IsEmpty() && Options.VolumeSlug.IsEmpty() && FPaths::FileExists(Concatenated)) { Options.SourceInkPath = Concatenated; }
	if (!Options.CompiledStoryPath.IsEmpty()) { Options.CompiledStoryPath = FPaths::ConvertRelativePathToFull(Options.CompiledStoryPath); }
	Options.LinesPath = FPaths::ConvertRelativePathToFull(Options.LinesPath);
	if (!Options.SourceInkPath.IsEmpty()) { Options.SourceInkPath = FPaths::ConvertRelativePathToFull(Options.SourceInkPath); }

	FString LinesJson;
	if (!FFileHelper::LoadFileToString(LinesJson, *Options.LinesPath))
	{
		Result.Error = FString::Printf(TEXT("Could not read synced line metadata: %s"), *Options.LinesPath);
		return Result;
	}
	FString CompiledJson;
	if (bExplicitCompiled || FPaths::FileExists(Options.CompiledStoryPath))
	{
		if (!ReadProsettaCompiledStory(Options.CompiledStoryPath, CompiledJson, Result.Error)) { return Result; }
		Result.CompiledStoryPath = Options.CompiledStoryPath;
	}
	const bool bInkFlow = !Options.SourceInkPath.IsEmpty();
	if (!bInkFlow && CompiledJson.IsEmpty())
	{
		Result.Error = FString::Printf(TEXT("No compiled story or source ink to import for %s (looked for %s)."), *Options.AssetPath, *Options.CompiledStoryPath);
		return Result;
	}

	// Stage. The .ink flow compiles the source and keeps the synced compiled story; the compiled flow
	// imports <Name>.ink.json with a <Name>.ink.prosetta.json sidecar (the factory's naming).
	FString StagedMain, StagedSidecar;
	if (bInkFlow)
	{
		FString InkSource;
		if (!FFileHelper::LoadFileToString(InkSource, *Options.SourceInkPath))
		{
			Result.Error = FString::Printf(TEXT("Could not read synced ink source: %s"), *Options.SourceInkPath);
			return Result;
		}
		StagedMain = FPaths::Combine(Options.StageDir, AssetName + TEXT(".ink"));
		StagedSidecar = FPaths::Combine(Options.StageDir, AssetName + TEXT(".prosetta.json"));
		if (!SaveStaged(StagedMain, InkSource, Result.Error)) { return Result; }
	}
	else
	{
		StagedMain = FPaths::Combine(Options.StageDir, AssetName + TEXT(".ink.json"));
		StagedSidecar = FPaths::Combine(Options.StageDir, AssetName + TEXT(".ink.prosetta.json"));
		if (!SaveStaged(StagedMain, CompiledJson, Result.Error)) { return Result; }
	}
	if (!SaveStaged(StagedSidecar, LinesJson, Result.Error)) { return Result; }
	Result.StagedPath = StagedMain;

	if (Options.bImportPresentation && Options.ImportPresentation)
	{
		FString PresentationError;
		if (!Options.ImportPresentation(Options, PresentationError))
		{
			Result.Error = PresentationError.IsEmpty() ? FString(TEXT("Presentation import failed; see the log for the failing voice take or portrait.")) : PresentationError;
			return Result;
		}
	}

	UInkpotStoryAsset* Asset = LoadObject<UInkpotStoryAsset>(nullptr, *Result.ObjectPath);
	if (Asset)
	{
		FReimportManager::Instance()->UpdateReimportPath(Asset, StagedMain, INDEX_NONE);
		if (!FReimportManager::Instance()->Reimport(Asset, false, false, StagedMain, nullptr, INDEX_NONE, false, true, false))
		{
			Result.Error = FString::Printf(TEXT("Reimport failed for asset: %s"), *Result.ObjectPath);
			return Result;
		}
		Result.bReimported = true;
	}
	else if (IsRunningCommandlet() || GIsAutomationTesting || !FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		// No UI, no asset-tools progress dialogs: import straight through the factory.
		UPackage* Package = CreatePackage(*(PackagePath / AssetName));
		Package->FullyLoad();
		UInkpotStoryAssetFactory* Factory = NewObject<UInkpotStoryAssetFactory>();
		bool bCancelled = false;
		UObject* Imported = Factory->ImportObject(UInkpotStoryAsset::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone, StagedMain, nullptr, bCancelled);
		Asset = Cast<UInkpotStoryAsset>(Imported);
		if (!Asset)
		{
			Result.Error = FString::Printf(TEXT("Import created no story asset for %s"), *StagedMain);
			return Result;
		}
		FAssetRegistryModule::AssetCreated(Asset);
	}
	else
	{
		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->GroupName = TEXT("ProsettaSyncImport");
		ImportData->DestinationPath = PackagePath;
		ImportData->bReplaceExisting = true;
		ImportData->Factory = NewObject<UInkpotStoryAssetFactory>();
		ImportData->Filenames.Add(StagedMain);
		FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const TArray<UObject*> Imported = AssetTools.Get().ImportAssetsAutomated(ImportData);
		Asset = Imported.IsEmpty() ? nullptr : Cast<UInkpotStoryAsset>(Imported[0]);
		if (!Asset)
		{
			Result.Error = FString::Printf(TEXT("Import created no story asset for %s"), *StagedMain);
			return Result;
		}
	}
	if (bInkFlow && !CompiledJson.IsEmpty())
	{
		// Keep Prosetta's compiled story rather than inklecate's: it is what the mirror plays.
		Asset->SetCompiledJSON(CompiledJson);
	}
	Asset->MarkPackageDirty();
	if (!SaveDirty(Options, Asset, Result.Error)) { return Result; }
	Result.bSucceeded = true;
	UE_LOG(LogProsettaStoryAssetBuild, Display, TEXT("%s %s from %s%s"), Result.bReimported ? TEXT("Reimported") : TEXT("Imported"), *Result.ObjectPath, *StagedMain,
		Result.CompiledStoryPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (compiled story %s)"), *Result.CompiledStoryPath));
	return Result;
}
