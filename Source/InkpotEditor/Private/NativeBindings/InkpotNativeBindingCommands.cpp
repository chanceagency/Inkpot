#include "NativeBindings/InkpotNativeBindingGenerator.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

namespace
{
	bool LoadOptionalManifest(const TArray<FString>& Args, int32 Index, FProsettaNativeBindingsManifest& OutManifest)
	{
		FString path;
		if (Args.IsValidIndex(Index))
			path = Args[Index];
		else
		{
			// Fall back to the sync mirror manifest when it exists; a missing default is not an error.
			path = UInkpotNativeBindingLibrary::GetDefaultMirrorManifestPath();
			if (!FPaths::FileExists(path))
				return false;
		}
		FString error;
		if (!FProsettaNativeBindingsManifest::LoadFile(path, OutManifest, error))
		{
			UE_LOG(LogTemp, Error, TEXT("Prosetta native bindings: %s"), *error);
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("Prosetta native bindings: using manifest %s ( project %d '%s' )."), *path, OutManifest.ProjectId, *OutManifest.ProjectName);
		return true;
	}

	void RunGenerator(const TArray<FString>& Args, bool bWrite)
	{
		if (Args.Num() < 1 || Args.Num() > 2 || FPaths::IsRelative(Args[0]))
		{
			UE_LOG(LogTemp, Warning, TEXT("Expected an absolute module source directory and an optional absolute native-bindings/manifest.json."));
			return;
		}
		FProsettaNativeBindingsManifest manifest;
		const bool bManifest = LoadOptionalManifest(Args, 1, manifest);
		const FInkpotNativeBindingGenerationReport report = FInkpotNativeBindingGenerator::Run(Args[0], bManifest ? &manifest : nullptr, bWrite);
		UE_LOG(LogTemp, Display, TEXT("%s"), *report.ToString());
		if (!report.bOk)
		{
			UE_LOG(LogTemp, Error, TEXT("Prosetta native bindings: %d error(s); see above."), report.Errors.Num());
		}
		else if (bWrite && report.NumWritten() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("Prosetta native bindings: %d loader(s) written; rebuild the game module to register them."), report.NumWritten());
		}
	}

	FAutoConsoleCommand Scan(TEXT("Prosetta.NativeBindings.Scan"),
		TEXT("Validate native binding modules without writing: Prosetta.NativeBindings.Scan \"absolute source dir\" [\"absolute manifest.json\"]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { RunGenerator(Args, false); }));

	FAutoConsoleCommand Generate(TEXT("Prosetta.NativeBindings.Generate"),
		TEXT("Validate native binding modules and write <Module>.Bindings.cpp loaders: Prosetta.NativeBindings.Generate \"absolute source dir\" [\"absolute manifest.json\"]; rebuild afterward."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { RunGenerator(Args, true); }));
}
