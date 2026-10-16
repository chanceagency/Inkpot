#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"
#include "ProsettaWatchSubsystem.generated.h"

class UProsettaGenerationAsset;

/** Fired on the game thread once per newly accepted generation, after its registry is saved and retired assets are cleaned up. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnProsettaGenerationAccepted, const UProsettaGenerationAsset& /*Generation*/);
/** Finds an audio decoder executable when a generation first needs one; false with OutError when none is available. */
using FProsettaDecoderResolver = TFunction<bool(FString& OutExecutable, FString& OutError)>;

/**
 * Editor-only consumer of `prosetta-sync watch` ( Prosetta Sync CLI 0.10.0+ ).
 * Downloads continue in PIE; imports do not. Only generation-completed events import;
 * error, blocked and status events carry a code that is surfaced through GetStatus.
 */
UCLASS()
class INKPOTEDITOR_API UProsettaWatchSubsystem : public UEditorSubsystem
{
 GENERATED_BODY()
public:
 /**
  * Hands transport ownership to an external CLI ( events arrive through ConsumeEvent, on the game
  * thread ), keeping the same verified, deferred importer until StartWatch takes it back.
  * ResolveDecoder runs synchronously on the game thread, mid-import, the first time a generation
  * carries compressed audio, so a decoder is only located when needed: it must return an existing
  * absolute path without blocking ( fetch ahead of time ). A failed or invalid resolution, or a
  * resolved decoder that later goes missing, is retried by the next generation.
  */
 void ConfigureExternalWatch(FProsettaDecoderResolver ResolveDecoder);
 /** True from ConfigureExternalWatch until StartWatch takes the transport back ( or the subsystem shuts down ). */
 bool HasExternalWatch() const { return bExternalWatch; }
 /**
  * True when EventJSON is a completed generation this importer already has: the same or a newer
  * revision is imported for its namespace, or queued. Such an event would be a no-op in ConsumeEvent.
  * Must be called on the game thread.
  */
 bool IsGenerationCurrent(const FString& EventJSON) const;
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 /**
  * Launches `prosetta-sync watch`. Channel is live or latest-release; Volumes is an optional
  * comma separated scope ( one volume only for latest-release, fixed tags must use sync );
  * bMirror=false passes --no-mirror. Returns false on a usage error or when the CLI cannot be run.
  * The CLI version is then checked without blocking the editor ( `--version` is polled from the
  * ticker, bounded by VersionCheckTimeoutSeconds ) and watch launches once it passes; GetStatus
  * reports the outcome.
  */
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool StartWatch(const FString& CLIExecutable, const FString& ConfigFile, const FString& AudioDecoder,
  const FString& Channel = TEXT("live"), const FString& Volumes = TEXT(""), bool bMirror = true);
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") void StopWatch();
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool ConsumeEvent(const FString& EventJSON);
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool TryApplyPending();
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") int32 CleanupRetiredAssets(const FString& Namespace);
 /** Copies staged native-bindings/cpp sources to Destination after checksum verification; optionally validates them against the generation manifest and writes <Module>.Bindings.cpp loaders. */
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool ImportNativeSources(const FString& Namespace, const FString& Destination, bool bGenerateLoaders = false);
 UFUNCTION(BlueprintPure, Category="Prosetta|Watch") FString GetStatus() const { return Status; }
 UFUNCTION(BlueprintPure, Category="Prosetta|Watch") int32 GetPendingCount() const { return Pending.Num(); }
 /** True while StartWatch's `--version` check is still running. */
 UFUNCTION(BlueprintPure, Category="Prosetta|Watch") bool IsCheckingVersion() const { return VersionProcess.IsValid(); }
 static bool IsSafeToApply();
 /**
  * Broadcast when a generation is accepted ( never for a replayed or older event ), after its
  * retired assets are cleaned up and while it is still safe to import. Static so editor modules
  * can subscribe before the subsystem exists.
  */
 static FOnProsettaGenerationAccepted OnGenerationAccepted;
 /** Validates watch options as the CLI does, building its argument list. Returns false with OutError on a usage error. */
 static bool BuildWatchArguments(const FString& ConfigFile, const FString& Channel, const FString& Volumes, bool bMirror, FString& OutArguments, FString& OutError);
 /** Parses `prosetta-sync --version` output ( e.g. "0.10.0" or "0.10.0+abc" ) and checks it is at least 0.10.0, the first release with watch. */
 static bool IsSupportedCLIVersion(const FString& VersionOutput);
 /**
  * The diagnostic to keep after the CLI printed RawLine ( a non-NDJSON line, untrimmed ). An error
  * block keeps its first line: indented continuations and .NET stack frames ( "   at ..." ) never
  * replace a diagnostic, so an unhandled exception reports its message rather than its last frame.
  */
 static FString SelectDiagnostic(const FString& Current, const FString& RawLine);
 /** Seconds StartWatch waits for `--version` before giving up ( the .NET CLI may cold start slowly ). */
 float VersionCheckTimeoutSeconds = 30.0f;
private:
 bool Tick(float DeltaTime);
 /** OutAccepted is the registry of a newly accepted generation; null for a replay, an older revision or a failure. */
 bool ApplyGeneration(const FString& EventJSON, FString& OutError, UProsettaGenerationAsset*& OutAccepted);
 void ConsumeOutput(bool bFlush);
 void PollVersionCheck();
 void CancelVersionCheck();
 void LaunchWatch(const FString& Version);
 FTSTicker::FDelegateHandle Ticker;
 FProcHandle Process;
 void* ReadPipe = nullptr;
 void* WritePipe = nullptr;
 FString Buffer;
 FString Decoder;
 /** Set by ConfigureExternalWatch, cleared by StartWatch; fills Decoder whenever it is missing. */
 FProsettaDecoderResolver DecoderResolver;
 /** See HasExternalWatch; a null resolver ( WAV-only project ) is still an external transport. */
 bool bExternalWatch = false;
 /** `--version` check started by StartWatch, and the watch it launches once the check passes. */
 FProcHandle VersionProcess;
 void* VersionReadPipe = nullptr;
 void* VersionWritePipe = nullptr;
 FString VersionOutput;
 double VersionStarted = 0.0;
 FString LaunchExecutable, LaunchArguments, LaunchChannel, LaunchDecoder;
 /** Last meaningful non-NDJSON line the CLI printed ( stderr shares the pipe ), reported if it exits. See SelectDiagnostic. */
 FString LastDiagnostic;
 FString Status = TEXT("Stopped");
 TMap<FString, FString> Pending;
 TMap<FString, FString> NativeGenerationPaths;
};
