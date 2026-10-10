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
 FAutoConsoleCommand Native(TEXT("Prosetta.Watch.ImportNative"), TEXT("Explicit C++ source import: Prosetta.Watch.ImportNative \"namespace\" \"absolute destination\" [generate]; 'generate' also validates the modules against the manifest and writes <Module>.Bindings.cpp loaders. Review and rebuild afterward."),
  FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
  {
   if (auto* watch = Watch())
   {
    if (Args.Num() < 2 || Args.Num() > 3 || (Args.Num() == 3 && !Args[2].Equals(TEXT("generate"), ESearchCase::IgnoreCase))) { UE_LOG(LogTemp, Warning, TEXT("Expected applied namespace, absolute C++ source destination, and optional 'generate'.")); return; }
    watch->ImportNativeSources(Args[0], Args[1], Args.Num() == 3);
    UE_LOG(LogTemp, Display, TEXT("Prosetta: %s"), *watch->GetStatus());
   }
  }));
}
