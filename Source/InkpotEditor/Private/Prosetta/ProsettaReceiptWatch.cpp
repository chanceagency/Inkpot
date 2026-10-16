#include "Prosetta/ProsettaReceiptWatch.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
 UProsettaWatchSubsystem* EditorImporter()
 {
  return GEditor ? GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>() : nullptr;
 }
}

FProsettaReceiptWatch::FProsettaReceiptWatch(const FString& InDirectory, FProsettaDecoderResolver InResolveDecoder, FImporterGetter InGetImporter)
 : Directory(FPaths::ConvertRelativePathToFull(InDirectory))
 , ResolveDecoder(MoveTemp(InResolveDecoder))
 , GetImporter(InGetImporter ? MoveTemp(InGetImporter) : FImporterGetter(&EditorImporter))
{
 IFileManager::Get().MakeDirectory(*Directory, true);
 IDirectoryWatcher* watcher = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get();
 if (!watcher || !watcher->RegisterDirectoryChangedCallback_Handle(Directory,
     IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FProsettaReceiptWatch::OnDirectoryChanged), WatchHandle))
 {
  WatchHandle.Reset();
  Error = TEXT("DirectoryWatcher unavailable; completed generations cannot reach the editor importer.");
  UE_LOG(LogTemp, Warning, TEXT("Prosetta: %s"), *Error);
 }
 // Ticks even without a watcher, so receipts already present are still handed off once.
 Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProsettaReceiptWatch::Tick), 0.5f);
}

FProsettaReceiptWatch::~FProsettaReceiptWatch()
{
 FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
 if (!WatchHandle.IsValid()) return;
 if (FDirectoryWatcherModule* module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
  if (IDirectoryWatcher* watcher = module->Get()) watcher->UnregisterDirectoryChangedCallback_Handle(Directory, WatchHandle);
}

bool FProsettaReceiptWatch::IsCompletionReceipt(const FString& JSON)
{
 TSharedPtr<FJsonObject> object;
 int32 version = 0;
 FString type;
 return JSON.StartsWith(TEXT("{")) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JSON), object) && object.IsValid()
  && object->TryGetNumberField(TEXT("schemaVersion"), version) && version == 1
  && object->TryGetStringField(TEXT("type"), type) && type == TEXT("generation-completed");
}

int32 FProsettaReceiptWatch::HandOffReceipts(const FString& Directory, TMap<FString, FString>& Seen, TFunctionRef<bool(const FString& JSON)> Consume)
{
 TArray<FString> files;
 IFileManager::Get().FindFiles(files, *(Directory / TEXT("*.json")), true, false);
 files.Sort();
 int32 accepted = 0;
 for (const FString& file : files)
 {
  FString json;
  if (!FFileHelper::LoadFileToString(json, *(Directory / file)) || !IsCompletionReceipt(json)) continue;
  // Compared by content: a receipt replaced within the file system's timestamp resolution is still new.
  const FString* previous = Seen.Find(file);
  if ((previous && *previous == json) || !Consume(json)) continue;
  Seen.Add(file, MoveTemp(json));
  ++accepted;
 }
 return accepted;
}

void FProsettaReceiptWatch::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
 // DirectoryWatcher may call back off the game thread; the importer only runs from Tick.
 bChangesPending = true;
}

void FProsettaReceiptWatch::ConnectImporter(UProsettaWatchSubsystem& Importer)
{
 if (Connected.Get() == &Importer) return;
 Importer.ConfigureExternalWatch(ResolveDecoder);
 Connected = &Importer;
}

bool FProsettaReceiptWatch::Connect()
{
 UProsettaWatchSubsystem* importer = GetImporter();
 if (!importer) return false;
 Connected.Reset();
 ConnectImporter(*importer);
 bChangesPending = true;
 return true;
}

int32 FProsettaReceiptWatch::Poll()
{
 UProsettaWatchSubsystem* importer = GetImporter();
 if (!importer) return 0;
 const int32 accepted = HandOffReceipts(Directory, Seen, [this, importer](const FString& JSON)
 {
  ConnectImporter(*importer);
  return importer->ConsumeEvent(JSON);
 });
 if (accepted > 0) UE_LOG(LogTemp, Display, TEXT("Prosetta: Handed %d completed generation(s) to the editor importer."), accepted);
 return accepted;
}

bool FProsettaReceiptWatch::Tick(float DeltaTime)
{
 // The flag stays raised until an importer exists, so nothing written before then is missed.
 if (GetImporter() && bChangesPending.exchange(false)) Poll();
 return true;
}
