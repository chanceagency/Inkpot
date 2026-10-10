#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"
#include "ProsettaWatchSubsystem.generated.h"

class UProsettaGenerationAsset;

/** Editor-only CLI sidecar consumer. Downloads continue in PIE; imports do not. */
UCLASS()
class INKPOTEDITOR_API UProsettaWatchSubsystem : public UEditorSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool StartWatch(const FString& CLIExecutable, const FString& ConfigFile, const FString& AudioDecoder);
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") void StopWatch();
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool ConsumeEvent(const FString& EventJSON);
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool TryApplyPending();
 UFUNCTION(BlueprintCallable, Category="Prosetta|Watch") bool ImportNativeSources(const FString& Namespace, const FString& Destination);
 UFUNCTION(BlueprintPure, Category="Prosetta|Watch") FString GetStatus() const { return Status; }
 UFUNCTION(BlueprintPure, Category="Prosetta|Watch") int32 GetPendingCount() const { return Pending.Num(); }
 static bool IsSafeToApply();
private:
 bool Tick(float DeltaTime);
 bool ApplyGeneration(const FString& EventJSON, FString& OutError);
 FTSTicker::FDelegateHandle Ticker;
 FProcHandle Process;
 void* ReadPipe = nullptr;
 void* WritePipe = nullptr;
 FString Buffer;
 FString Decoder;
 FString Status = TEXT("Stopped");
 TMap<FString, FString> Pending;
 TMap<FString, FString> NativeGenerationPaths;
};
