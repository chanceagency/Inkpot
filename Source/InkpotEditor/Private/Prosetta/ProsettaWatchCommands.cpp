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
  TEXT("Start the sidecar: Prosetta.Watch.Start \"absolute CLI executable\" \"absolute config file\" [\"absolute FFmpeg executable\"]"),
  FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
  {
   if (UProsettaWatchSubsystem* watch = Watch())
   {
    if (Args.Num() < 2 || Args.Num() > 3) { UE_LOG(LogTemp, Warning, TEXT("Expected CLI executable, config file, and optional FFmpeg executable.")); return; }
    watch->StartWatch(Args[0], Args[1], Args.Num() == 3 ? Args[2] : FString());
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
