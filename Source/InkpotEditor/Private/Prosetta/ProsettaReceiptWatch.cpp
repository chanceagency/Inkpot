#include "Prosetta/ProsettaReceiptWatch.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
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
  if (!FFileHelper::LoadFileToString(json, *(Directory / file))) continue;
  // Compared by content: a receipt replaced within the file system's timestamp resolution is still new.
  // Seen holds only completion receipts, so an unchanged one is skipped before it is parsed again.
  const FString* previous = Seen.Find(file);
  if ((previous && *previous == json) || !IsCompletionReceipt(json) || !Consume(json)) continue;
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
 // StartWatch takes the transport back from an earlier connection; a new receipt retakes it.
 if (Connected.Get() == &Importer && Importer.HasExternalWatch()) return;
 Importer.ConfigureExternalWatch(ResolveDecoder);
 Connected = &Importer;
}

bool FProsettaReceiptWatch::Connect()
{
 UProsettaWatchSubsystem* importer = GetImporter();
 if (!importer) return false;
 Connected.Reset();
 ConnectImporter(*importer);
 Rejected.Reset();
 bChangesPending = true;
 return true;
}

int32 FProsettaReceiptWatch::Poll()
{
 UProsettaWatchSubsystem* importer = GetImporter();
 return importer ? PollImporter(*importer) : 0;
}

int32 FProsettaReceiptWatch::PollImporter(UProsettaWatchSubsystem& Importer)
{
 int32 accepted = 0;
 TSet<FString> rejected;
 HandOffReceipts(Directory, Seen, [this, &Importer, &accepted, &rejected](const FString& JSON)
 {
  if (Rejected.Contains(JSON)) { rejected.Add(JSON); return false; }
  // A receipt the importer already has ( e.g. left from an earlier session ) is recorded without
  // taking the transport, so it never stops a CLI the plugin owns or reports an idle producer.
  if (Importer.IsGenerationCurrent(JSON)) return true;
  ConnectImporter(Importer);
  if (!Importer.ConsumeEvent(JSON)) { rejected.Add(JSON); return false; }
  ++accepted;
  return true;
 });
 // Only rejected receipts still in the directory are remembered.
 Rejected = MoveTemp(rejected);
 if (accepted > 0) UE_LOG(LogTemp, Display, TEXT("Prosetta: Handed %d completed generation(s) to the editor importer."), accepted);
 return accepted;
}

bool FProsettaReceiptWatch::Tick(float DeltaTime)
{
 // Notifications are only hints: atomic receipt replacement can be missed/coalesced
 // by the OS. Reconcile the small receipt directory even without a callback. The
 // content de-dup and verified importer still own acceptance, PIE deferral and saves.
 UProsettaWatchSubsystem* importer = GetImporter();
 const double Now = FPlatformTime::Seconds();
 if (importer && (bChangesPending.exchange(false) || Now - LastReceiptScanSeconds >= ReceiptScanIntervalSeconds))
 {
  LastReceiptScanSeconds = Now;
  PollImporter(*importer);
 }
 return true;
}
