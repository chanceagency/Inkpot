#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"
#include "ProsettaWatchSubsystem.generated.h"

class UProsettaGenerationAsset;

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
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 /**
  * Launches `prosetta-sync watch`. Channel is live or latest-release; Volumes is an optional
  * comma separated scope ( one volume only for latest-release, fixed tags must use sync );
  * bMirror=false passes --no-mirror. The CLI version is checked before launching.
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
 static bool IsSafeToApply();
 /** Validates watch options as the CLI does, building its argument list. Returns false with OutError on a usage error. */
 static bool BuildWatchArguments(const FString& ConfigFile, const FString& Channel, const FString& Volumes, bool bMirror, FString& OutArguments, FString& OutError);
 /** Parses `prosetta-sync --version` output ( e.g. "0.10.0" or "0.10.0+abc" ) and checks it is at least 0.10.0, the first release with watch. */
 static bool IsSupportedCLIVersion(const FString& VersionOutput);
private:
 bool Tick(float DeltaTime);
 bool ApplyGeneration(const FString& EventJSON, FString& OutError);
 void ConsumeOutput(bool bFlush);
 FTSTicker::FDelegateHandle Ticker;
 FProcHandle Process;
 void* ReadPipe = nullptr;
 void* WritePipe = nullptr;
 FString Buffer;
 FString Decoder;
 /** Last non-NDJSON line the CLI printed ( stderr shares the pipe ), reported if it exits. */
 FString LastDiagnostic;
 FString Status = TEXT("Stopped");
 TMap<FString, FString> Pending;
 TMap<FString, FString> NativeGenerationPaths;
};
