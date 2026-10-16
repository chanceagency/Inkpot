#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Prosetta/ProsettaWatchSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include <atomic>

struct FFileChangeData;

/**
 * Directory transport for `prosetta-sync watch` completions produced outside the editor, for example
 * by a terminal wrapper. Each `*.json` file in the directory is one receipt: a `generation-completed`
 * NDJSON event of the CLI ( schemaVersion 1 ) as a single JSON object. Producers write a temporary
 * name that does not end in `.json` and rename it into place, one file per project, scope and channel,
 * so a newer completion replaces the older one.
 *
 * Unreal's DirectoryWatcher callback only raises a flag; receipts are read on the core ticker and
 * passed to UProsettaWatchSubsystem::ConsumeEvent on the game thread, so manifest verification, Play
 * deferral and replay rejection stay in the importer. Each receipt is handed off once per content,
 * a receipt ConsumeEvent rejects is offered again on the next change, and receipts already present
 * when the watch is created are handed off once. A receipt whose generation the importer already has
 * ( IsGenerationCurrent ) is recorded without a hand-off. Handing off a new generation calls
 * ConfigureExternalWatch with ResolveDecoder whenever the importer does not have it yet, including
 * after StartWatch took the transport back, so it stops a plugin-owned CLI watch. A receipt accepted
 * here whose import later fails ( for example compressed audio without a decoder ) is not offered
 * again until its content changes; pass a fresh map to HandOffReceipts to offer every receipt again.
 *
 * Create it on the game thread in an interactive editor, never in a commandlet or unattended run that
 * must not replay receipts, and destroy it before the DirectoryWatcher module shuts down.
 */
class INKPOTEDITOR_API FProsettaReceiptWatch
{
public:
 using FImporterGetter = TFunction<UProsettaWatchSubsystem*()>;
 /**
  * Watches Directory, creating it when missing. ResolveDecoder is handed to ConfigureExternalWatch
  * ( null for WAV-only projects ). GetImporter defaults to the editor's UProsettaWatchSubsystem.
  */
 FProsettaReceiptWatch(const FString& Directory, FProsettaDecoderResolver ResolveDecoder, FImporterGetter GetImporter = nullptr);
 ~FProsettaReceiptWatch();
 FProsettaReceiptWatch(const FProsettaReceiptWatch&) = delete;
 FProsettaReceiptWatch& operator=(const FProsettaReceiptWatch&) = delete;
 /** False when DirectoryWatcher cannot observe the directory; GetError says why. */
 bool IsWatching() const { return WatchHandle.IsValid(); }
 /** Why receipts cannot reach the importer; empty while watching. */
 const FString& GetError() const { return Error; }
 const FString& GetDirectory() const { return Directory; }
 /**
  * Connects the importer now ( ConfigureExternalWatch with the resolver ), stopping a plugin-owned CLI
  * watch begun since the last connection, and rescans the directory on the next tick. Call it when the
  * project starts a producer it owns. Returns false when the importer is unavailable.
  */
 bool Connect();
 /** Scans the directory now, without waiting for a change notification; returns how many new generations the importer accepted. */
 int32 Poll();
 /** True for a schemaVersion 1 `generation-completed` event; other events and malformed JSON never import. */
 static bool IsCompletionReceipt(const FString& JSON);
 /**
  * Passes each completion receipt in Directory whose content differs from Seen to Consume, in file
  * name order, and returns how many it accepted. Seen maps receipt names to the content last accepted;
  * a receipt is recorded only when Consume returns true, so a rejected one is offered again next time.
  */
 static int32 HandOffReceipts(const FString& Directory, TMap<FString, FString>& Seen, TFunctionRef<bool(const FString& JSON)> Consume);
private:
 void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);
 bool Tick(float DeltaTime);
 void ConnectImporter(UProsettaWatchSubsystem& Importer);
 FString Directory;
 FProsettaDecoderResolver ResolveDecoder;
 FImporterGetter GetImporter;
 FString Error;
 TMap<FString, FString> Seen;
 /** Starts raised so receipts written before the watch existed are handed off once. */
 std::atomic<bool> bChangesPending{true};
 FDelegateHandle WatchHandle;
 FTSTicker::FDelegateHandle Ticker;
 TWeakObjectPtr<UProsettaWatchSubsystem> Connected;
};
