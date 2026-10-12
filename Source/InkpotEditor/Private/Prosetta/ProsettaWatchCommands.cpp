#include "Prosetta/ProsettaWatchSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"

namespace
{
 UProsettaWatchSubsystem* Watch()
 {
  return GEditor ? GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>() : nullptr;
 }
 FAutoConsoleCommand Start(TEXT("Prosetta.Watch.Start"),
  TEXT("Start `prosetta-sync watch` (CLI 0.10.0+): Prosetta.Watch.Start \"absolute CLI executable\" \"absolute config file\" [\"absolute FFmpeg executable\"] [--channel=live|latest-release] [--volumes=core] [--no-mirror]"),
  FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
  {
   if (UProsettaWatchSubsystem* watch = Watch())
   {
    TArray<FString> paths;
    FString channel = TEXT("live"), volumes;
    bool bMirror = true;
    for (const FString& arg : Args)
    {
     if (arg.StartsWith(TEXT("--channel="), ESearchCase::CaseSensitive)) channel = arg.RightChop(10);
     else if (arg.StartsWith(TEXT("--volumes="), ESearchCase::CaseSensitive)) volumes = arg.RightChop(10);
     else if (arg.Equals(TEXT("--no-mirror"), ESearchCase::CaseSensitive)) bMirror = false;
     else if (arg.StartsWith(TEXT("--"))) { UE_LOG(LogTemp, Warning, TEXT("Unknown watch option %s."), *arg); return; }
     else paths.Add(arg);
    }
    if (paths.Num() < 2 || paths.Num() > 3) { UE_LOG(LogTemp, Warning, TEXT("Expected CLI executable, config file, and optional FFmpeg executable.")); return; }
    watch->StartWatch(paths[0], paths[1], paths.Num() == 3 ? paths[2] : FString(), channel, volumes, bMirror);
    UE_LOG(LogTemp, Display, TEXT("Prosetta: %s"), *watch->GetStatus());
   }
  }));
 FAutoConsoleCommand Stop(TEXT("Prosetta.Watch.Stop"), TEXT("Stop CLI watch and preserve queued/completed assets."),
  FConsoleCommandDelegate::CreateLambda([]() { if (auto* watch = Watch()) watch->StopWatch(); }));
 FAutoConsoleCommand Status(TEXT("Prosetta.Watch.Status"), TEXT("Show watch/import status and pending namespace count."),
  FConsoleCommandDelegate::CreateLambda([]()
  {
   if (auto* watch = Watch()) UE_LOG(LogTemp, Display, TEXT("Prosetta: %s; pending namespaces=%d"), *watch->GetStatus(), watch->GetPendingCount());
  }));
 FAutoConsoleCommand Cleanup(TEXT("Prosetta.Watch.Cleanup"), TEXT("Recheck retired owned assets: Prosetta.Watch.Cleanup \"namespace\"; referenced assets are preserved."),
  FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
  {
   if (auto* watch = Watch())
   {
    if (Args.Num() != 1) { UE_LOG(LogTemp, Warning, TEXT("Expected applied namespace.")); return; }
    watch->CleanupRetiredAssets(Args[0]);
    UE_LOG(LogTemp, Display, TEXT("Prosetta: %s"), *watch->GetStatus());
   }
  }));
 FAutoConsoleCommand Native(TEXT("Prosetta.Watch.ImportNative"), TEXT("Explicit C++ source import: Prosetta.Watch.ImportNative \"namespace\" \"absolute destination\"; review and rebuild afterward."),
  FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
  {
   if (auto* watch = Watch())
   {
    if (Args.Num() != 2) { UE_LOG(LogTemp, Warning, TEXT("Expected applied namespace and absolute C++ source destination.")); return; }
    watch->ImportNativeSources(Args[0], Args[1]);
    UE_LOG(LogTemp, Display, TEXT("Prosetta: %s"), *watch->GetStatus());
   }
  }));
}
