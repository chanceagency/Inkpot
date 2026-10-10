#include "Asset/InkpotStoryAssetFactory.h"
#include "Prosetta/ProsettaChecksum.h"
#include "Asset/InkpotStoryAsset.h"
#include "Misc/FileHelper.h"
#include "Compiler/InkCompiler.h"
#include "Logging/MessageLog.h"
#include "EditorFramework/AssetImportData.h"
#include "GameplayTags/InkpotTagUtility.h"
#include "Inkpot/Inkpot.h"
#include "Inkpot/InkpotStory.h"
#include "Settings/InkpotPreferences.h"
#include "Settings/InkpotSettings.h"
#include "ImportPipeline/InkpotImportPipeline.h"
#include "ImportPipeline/InkpotImportPipelineLibrary.h"
#include "Serialization/JsonSerializer.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"

#define LOCTEXT_NAMESPACE "InkpotStoryAssetFactory"

namespace
{
	// Looks for a "<storyname>.prosetta.json" sidecar next to the imported Ink file and,
	// when present, bakes its contents onto the asset so the line metadata is available
	// to the cooked runtime.
	void LoadProsettaSidecar(const FString& InFullFilePath, UInkpotStoryAsset* InAsset)
	{
		const FString sidecarPath = FPaths::Combine(
			FPaths::GetPath(InFullFilePath),
			FPaths::GetBaseFilename(InFullFilePath) + TEXT(".prosetta.json"));

		if (!FPaths::FileExists(sidecarPath))
			return;

		FString prosettaJSON;
		if (FFileHelper::LoadFileToString(prosettaJSON, *sidecarPath))
			InAsset->SetProsettaJSON(prosettaJSON);
	}
}

UInkpotStoryAssetFactory::UInkpotStoryAssetFactory()
{
	Formats.Add(TEXT("json;Compiled Ink Story (.ink.json)"));
	Formats.Add(FString(TEXT("ink;")) + LOCTEXT("FormatTxt", "Ink Story File").ToString());
	SupportedClass = UInkpotStoryAsset::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bAutomatedReimport = true;
}

bool UInkpotStoryAssetFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("ink"), ESearchCase::IgnoreCase) || Filename.EndsWith(TEXT(".ink.json"), ESearchCase::IgnoreCase);
}

FSoftClassPath UInkpotStoryAssetFactory::GetCustomPiplineClass()  const
{
	const UInkpotSettings* settings = GetDefault<UInkpotSettings>();
	return settings->CustomImportClass;
}

bool UInkpotStoryAssetFactory::IsUsingCustomPipeline() const
{
	return GetCustomPiplineClass().IsValid();
}

UObject* UInkpotStoryAssetFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& InFullFilePath, const TCHAR* InParms, FFeedbackContext* InWarn, bool& bOutOperationCanceled)
{
	const FString fileExtension = FPaths::GetExtension(InFullFilePath);
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, *fileExtension);

	UInkpotStoryAsset* newAsset = nullptr;
	if (FPaths::GetExtension(InFullFilePath).Equals(TEXT("json"), ESearchCase::IgnoreCase))
	{
		FString compiled, metadata, error;
		const FString sidecar = FPaths::Combine(FPaths::GetPath(InFullFilePath), FPaths::GetBaseFilename(InFullFilePath) + TEXT(".prosetta.json"));
		if (ReadCompiledFiles(InFullFilePath, sidecar, compiled, metadata, error))
		{
			newAsset = NewObject<UInkpotStoryAsset>(InParent, InClass, InName, InFlags);
			newAsset->SetCompiledJSON(compiled);
			newAsset->SetProsettaJSON(metadata);
			newAsset->SetProsettaProvenance(CompiledProvenance(InFullFilePath, compiled));
			newAsset->UpdateAssetInfo(InFullFilePath);
		}
		else if (InWarn)
			InWarn->Logf(ELogVerbosity::Error, TEXT("Compiled Ink import failed: %s"), *error);
	}
	else if( IsUsingCustomPipeline() )
		newAsset = ExecuteCustomImportPipeline(InClass, InParent, InName, InFlags, InFullFilePath, InParms, InWarn, bOutOperationCanceled);
	else 
		newAsset = ExecuteStandardImportPipeline(InClass, InParent, InName, InFlags, InFullFilePath, InParms, InWarn, bOutOperationCanceled);

	if (!newAsset)
		bOutOperationCanceled = true;
	else
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, newAsset);

	return newAsset;
}

UInkpotStoryAsset* UInkpotStoryAssetFactory::ExecuteStandardImportPipeline(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& InFullFilePath, const TCHAR* InParms, FFeedbackContext* InWarn, bool& bOutOperationCanceled)
{
	UInkpotStoryAsset* newAsset = nullptr;

	FString inkStory;
	FString inkJSON;
	if (LoadAndCompileStory(InFullFilePath, inkStory, inkJSON))
	{
		newAsset = NewObject<UInkpotStoryAsset>(InParent, InClass, InName, InFlags);
		newAsset->SetSource(inkStory);
		newAsset->SetCompiledJSON(inkJSON);
		LoadProsettaSidecar(InFullFilePath, newAsset);
		newAsset->UpdateAssetInfo(InFullFilePath);
		GenerateTAGs(InParent, newAsset);
	}

	return newAsset;
}

UInkpotStoryAsset* UInkpotStoryAssetFactory::ExecuteCustomImportPipeline(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& InFullFilePath, const TCHAR* InParms, FFeedbackContext* InWarn, bool& bOutOperationCanceled)
{
	FSoftClassPath importPipelineClassName = GetCustomPiplineClass();
	UClass* importPipelineClass = importPipelineClassName.IsValid() ? LoadObject<UClass>(NULL, *importPipelineClassName.ToString()) : nullptr;
	if (!importPipelineClass)
		return nullptr;

	UInkpotImportPipeline* importPipeline = NewObject<UInkpotImportPipeline>( InParent, importPipelineClass );
	if (!importPipeline)
		return nullptr;

	UInkpotStoryAsset* newAsset = importPipeline->CreateFile(InClass, InParent, InName, InFlags, InFullFilePath, InParms, InWarn, bOutOperationCanceled);

	return newAsset;
}

void UInkpotStoryAssetFactory::GenerateTAGs( UObject* InParent, UInkpotStoryAsset *InStoryAsset ) const
{
	bool bSuccess;
	UInkpotImportPipelineLibrary::GenerateTAGs(InStoryAsset, bSuccess);
}

bool UInkpotStoryAssetFactory::LoadAndCompileStory( const FString& InFilename, FString &OutStory, FString &OutCompiledStory) const
{
	if ( !FFileHelper::LoadFileToString( OutStory, *InFilename ) )
		return false;
	bool bSuccess;
	UInkpotImportPipelineLibrary::CompileInkFile(InFilename, bSuccess, OutCompiledStory);
	return bSuccess;
}

bool UInkpotStoryAssetFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UInkpotStoryAsset* InkpotStoryAsset = Cast<UInkpotStoryAsset>(Obj);
	if (!InkpotStoryAsset)
		return false;
	UAssetImportData *importData = InkpotStoryAsset->GetAssetImportData();
	if (!importData)
		return false;
	importData->ExtractFilenames(OutFilenames);
	return true;
}

void UInkpotStoryAssetFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UInkpotStoryAsset* InkpotStoryAsset  = Cast<UInkpotStoryAsset>(Obj);
	if (!InkpotStoryAsset)
		return;
	UAssetImportData *importData = InkpotStoryAsset->GetAssetImportData();
	if (!importData)
		return;
	if (!ensure(NewReimportPaths.Num() == 1))
		return;
	importData->UpdateFilenameOnly(NewReimportPaths[0]);
}

EReimportResult::Type UInkpotStoryAssetFactory::Reimport(UObject* Obj)
{
	UInkpotStoryAsset* InkpotStoryAsset  = Cast<UInkpotStoryAsset>(Obj);
	if (InkpotStoryAsset  != nullptr)
	{
		UAssetImportData* importData = InkpotStoryAsset->GetAssetImportData();
		if ( importData )
		{
			const FString filename = importData->GetFirstFilename();
			if (FPaths::GetExtension(filename).Equals(TEXT("json"), ESearchCase::IgnoreCase))
			{
				const FString sidecar = FPaths::Combine(FPaths::GetPath(filename), FPaths::GetBaseFilename(filename) + TEXT(".prosetta.json"));
				FString error;
				if (!ApplyCompiledFiles(InkpotStoryAsset, filename, sidecar, error))
				{
					UE_LOG(LogTemp, Error, TEXT("Compiled Ink reimport failed: %s"), *error);
					return EReimportResult::Failed;
				}
				return EReimportResult::Succeeded;
			}
			bool bOperationCancelled = false;
			if (ImportObject(InkpotStoryAsset ->GetClass(), InkpotStoryAsset ->GetOuter(), *InkpotStoryAsset->GetName(), RF_Public | RF_Standalone, importData->GetFirstFilename(), nullptr, bOperationCancelled) != nullptr)
			{
				return EReimportResult::Succeeded;
			}
		}
	}

	return EReimportResult::Failed;
}

int32 UInkpotStoryAssetFactory::GetPriority() const
{
	return ImportPriority;
}

FProsettaStoryProvenance UInkpotStoryAssetFactory::CompiledProvenance(const FString& InFilename, const FString& InCompiledJSON)
{
	// Project name / compile time come from the Prosetta wrapper when the file has one;
	// the build id is the sha256 of the compiled story bytes, matching the sync manifest checksum.
	FProsettaStoryProvenance provenance;
	FString raw;
	if (FFileHelper::LoadFileToString(raw, *InFilename))
		FProsettaStoryProvenance::FromCompiledWrapper(raw, provenance);
	FTCHARToUTF8 utf8(*InCompiledJSON, InCompiledJSON.Len());
	provenance.BuildId = ProsettaChecksum::SHA256(utf8.Get(), utf8.Length()).ToLower();
	return provenance;
}

bool UInkpotStoryAssetFactory::DecodeCompiledJSON(const FString& InJSON, FString& OutCompiledJSON, FString& OutError)
{
	TSharedPtr<FJsonObject> root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InJSON), root) || !root.IsValid())
	{
		OutError = TEXT("Malformed or truncated compiled story JSON.");
		return false;
	}
	OutCompiledJSON = InJSON;
	if (const TSharedPtr<FJsonValue> wrapped = root->TryGetField(TEXT("story")))
	{
		if (wrapped->Type == EJson::String)
		{
			OutCompiledJSON = wrapped->AsString();
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OutCompiledJSON), root) || !root.IsValid())
			{
				OutError = TEXT("Malformed wrapped compiled story JSON.");
				return false;
			}
		}
		else if (wrapped->Type == EJson::Object)
		{
			root = wrapped->AsObject();
			OutCompiledJSON.Empty();
			FJsonSerializer::Serialize(root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutCompiledJSON));
		}
		else
		{
			OutError = TEXT("Compiled story wrapper must contain an object or JSON string.");
			return false;
		}
	}
	double version;
	const TArray<TSharedPtr<FJsonValue>>* content = nullptr;
	if (!root->TryGetNumberField(TEXT("inkVersion"), version) || version < 18 || version > 21 || version != FMath::FloorToDouble(version) ||
		!root->TryGetArrayField(TEXT("root"), content) || content->IsEmpty() ||
		(content->Last()->Type != EJson::Null && content->Last()->Type != EJson::Object))
	{
		OutError = TEXT("Expected compatible Ink JSON (inkVersion 18–21 and a terminated root container).");
		return false;
	}
	return true;
}

bool UInkpotStoryAssetFactory::ReadCompiledFiles(const FString& InFilename, const FString& InMetadataFilename, FString& OutCompiledJSON, FString& OutMetadataJSON, FString& OutError)
{
	FString json;
	if (!FFileHelper::LoadFileToString(json, *InFilename))
	{
		OutError = TEXT("Cannot read compiled story file.");
		return false;
	}
	if (!DecodeCompiledJSON(json, OutCompiledJSON, OutError))
		return false;
	OutMetadataJSON.Empty();
	if (!InMetadataFilename.IsEmpty() && FPaths::FileExists(InMetadataFilename))
	{
		if (!FFileHelper::LoadFileToString(OutMetadataJSON, *InMetadataFilename))
		{
			OutError = TEXT("Cannot read Prosetta metadata sidecar.");
			return false;
		}
		TSharedPtr<FJsonObject> metadata;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OutMetadataJSON), metadata) || !metadata.IsValid() || !metadata->HasField(TEXT("lines")))
		{
			OutError = TEXT("Malformed Prosetta line metadata sidecar.");
			return false;
		}
	}
	return true;
}

bool UInkpotStoryAssetFactory::ApplyCompiledFiles(UInkpotStoryAsset* InAsset, const FString& InFilename, const FString& InMetadataFilename, FString& OutError)
{
	FString compiled, metadata;
	if (!InAsset || !ReadCompiledFiles(InFilename, InMetadataFilename, compiled, metadata, OutError))
		return false;
	InAsset->Modify();
	InAsset->SetCompiledJSON(compiled);
	InAsset->SetProsettaJSON(metadata);
	InAsset->SetProsettaProvenance(CompiledProvenance(InFilename, compiled));
	InAsset->UpdateAssetInfo(InFilename);
	InAsset->MarkPackageDirty();
	InAsset->PostEditChange();
	return true;
}

#undef LOCTEXT_NAMESPACE

