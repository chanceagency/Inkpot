#if !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Asset/InkpotStoryAssetFactory.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"
#include "Ink/Story.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaCompiledImportTest, "Inkpot.Prosetta.CompiledImport",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaCompiledImportTest::RunTest(const FString& InParameters)
{
 const FString directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CompiledImportTest"));
 IFileManager::Get().MakeDirectory(*directory, true);
 const FString filename = directory / TEXT("story.ink.json");
 const FString sidecar = directory / TEXT("story.ink.prosetta.json");
 const FString compiled = TEXT("{\"inkVersion\":21,\"root\":[\"^<prosetta id='023A'>fallback</prosetta>\",\"\\n\",\"done\",null],\"listDefs\":{}}");
 FFileHelper::SaveStringToFile(compiled, *filename);
 FFileHelper::SaveStringToFile(TEXT("{\"lines\":{\"023A\":{\"string\":\"Authoritative\"}}}"), *sidecar);
 UInkpotStoryAssetFactory* factory = NewObject<UInkpotStoryAssetFactory>();
 TestTrue(TEXT("compiled extension recognized"), factory->FactoryCanImport(filename));
 TestTrue(TEXT("source extension retained"), factory->FactoryCanImport(TEXT("existing.ink")));
 TestFalse(TEXT("unrelated JSON not claimed"), factory->FactoryCanImport(TEXT("config.json")));
 bool canceled = false;
 UInkpotStoryAsset* asset = Cast<UInkpotStoryAsset>(factory->FactoryCreateFile(UInkpotStoryAsset::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInkpotStoryAsset::StaticClass()), RF_Transient, filename, nullptr, GWarn, canceled));
 if (!TestNotNull(TEXT("compiled JSON imported without source/inklecate"), asset)) return false;
 TestEqual(TEXT("exact compiled bytes"), asset->GetCompiledJSON(), compiled);
 TSoftObjectPtr<UInkpotStoryAsset> reference(asset);
 UProsettaMetadata* metadata = NewObject<UProsettaMetadata>();
 TestTrue(TEXT("sidecar loaded"), metadata->LoadFromJSON(asset->GetProsettaJSON()));
 FString line;
 TestTrue(TEXT("line references resolve"), metadata->ResolveText(FName(TEXT("023A")), line));
 TestEqual(TEXT("line text"), line, FString(TEXT("Authoritative")));
 const FString updated = TEXT("{\"inkVersion\":21,\"root\":[\"^Updated\",\"\\n\",\"done\",null],\"listDefs\":{}}");
 FFileHelper::SaveStringToFile(updated, *filename);
 TestEqual(TEXT("reimport succeeds"), factory->Reimport(asset), EReimportResult::Succeeded);
 TestEqual(TEXT("identity retained"), reference.Get(), asset);
 TestEqual(TEXT("updated exact bytes"), asset->GetCompiledJSON(), updated);
 Ink::FStory story(asset->GetCompiledJSON());
 TestEqual(TEXT("runtime sees updated story"), story.Continue(), FString(TEXT("Updated\n")));
 const FString oldMetadata = asset->GetProsettaJSON();
 FFileHelper::SaveStringToFile(TEXT("{\"inkVersion\":21,\"root\":["), *filename);
 AddExpectedError(TEXT("Compiled Ink reimport failed"), EAutomationExpectedErrorFlags::Contains, 1);
 TestEqual(TEXT("truncated reimport rejected"), factory->Reimport(asset), EReimportResult::Failed);
 TestEqual(TEXT("last story intact"), asset->GetCompiledJSON(), updated);
 TestEqual(TEXT("last metadata intact"), asset->GetProsettaJSON(), oldMetadata);
 FString decoded, error;
 TestFalse(TEXT("unsupported version rejected"), factory->DecodeCompiledJSON(TEXT("{\"inkVersion\":22,\"root\":[null]}"), decoded, error));
 TestTrue(TEXT("object wrapper accepted"), factory->DecodeCompiledJSON(TEXT("{\"story\":") + compiled + TEXT("}"), decoded, error));
 const FString sourcePath = directory / TEXT("source.ink");
 FFileHelper::SaveStringToFile(TEXT("Source import retained.\n-> END\n"), *sourcePath);
 UInkpotStoryAsset* sourceAsset = Cast<UInkpotStoryAsset>(factory->FactoryCreateFile(UInkpotStoryAsset::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInkpotStoryAsset::StaticClass()), RF_Transient, sourcePath, nullptr, GWarn, canceled));
 if (TestNotNull(TEXT("existing source importer still compiles"), sourceAsset))
 {
  Ink::FStory sourceStory(sourceAsset->GetCompiledJSON());
  TestEqual(TEXT("source import runtime control"), sourceStory.Continue(), FString(TEXT("Source import retained.\n")));
 }
 IFileManager::Get().DeleteDirectory(*directory, false, true);
 return true;
}
#endif
