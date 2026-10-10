#pragma once

#include "CoreMinimal.h"
#include "NativeBindings/InkpotNativeBindingSource.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingTypes.h"
#include "Inkpot/Prosetta/ProsettaNativeBindingsManifest.h"

/** One generated loader file ( <Module>.Bindings.cpp ) beside its module. */
struct INKPOTEDITOR_API FInkpotNativeBindingGeneratedLoader
{
	FString ModuleName;
	FString ModuleFile;
	FString LoaderFile;
	FString Content;
	EInkpotNativeBindingSource Source = EInkpotNativeBindingSource::Local;
	/** The content differs from what is on disk ( or the file is new ). */
	bool bChanged = false;
	bool bWritten = false;
};

struct INKPOTEDITOR_API FInkpotNativeBindingGenerationReport
{
	FString Directory;
	TArray<FInkpotNativeBindingSourceModule> Modules;
	TArray<FInkpotNativeBindingGeneratedLoader> Loaders;
	TArray<FString> Errors;
	TArray<FString> Warnings;
	bool bManifestUsed = false;
	/** No error was raised; every valid module has an up to date loader. */
	bool bOk = false;

	int32 NumWritten() const;
	FString ToString() const;
};

/**
 * FInkpotNativeBindingGenerator
 * Discovers native binding modules in a source directory, validates them, cross-checks
 * them against the Prosetta manifest when one is supplied, and writes the loader each
 * module needs to register with Inkpot:
 *
 *   Source/MyGame/Prosetta/NativeBindings/Inventory.cpp            synced from Prosetta ( never edited )
 *   Source/MyGame/Prosetta/NativeBindings/Inventory.Bindings.cpp   generated loader
 *   Source/MyGame/Prosetta/NativeBindings/Cheats.cpp               local module, same conventions
 *   Source/MyGame/Prosetta/NativeBindings/Cheats.Bindings.cpp      generated loader
 *
 * Loaders are deterministic: unchanged modules produce unchanged bytes.
 */
class INKPOTEDITOR_API FInkpotNativeBindingGenerator
{
public:
	static const TCHAR* LoaderSuffix() { return TEXT(".Bindings.cpp"); }
	static bool IsLoaderFile(const FString& InFilename);
	static FString LoaderFileFor(const FString& InModuleFile);

	/** Recursively finds *.cpp files that declare a Prosetta::Bindings namespace and are not loaders. Sorted. */
	static TArray<FString> DiscoverModuleFiles(const FString& InDirectory);

	/** Decides whether a scanned module is Prosetta-synced ( provenance header or manifest entry ) or local. */
	static EInkpotNativeBindingSource ClassifySource(const FInkpotNativeBindingSourceModule& InModule, const FProsettaNativeBindingsManifest* InManifest);

	/**
	 * Generates loader text for a valid module. When a manifest entry is given, its
	 * lookahead flags win over the source annotations and its sha256 is recorded.
	 */
	static FString GenerateLoader(const FInkpotNativeBindingSourceModule& InModule, EInkpotNativeBindingSource InSource, const FProsettaNativeBindingLanguageEntry* InManifestEntry);

	/**
	 * Compares a scanned module with the manifest. Errors mean the local copy must not be
	 * loaded ( excluded in Prosetta, stale bytes, different functions ); warnings are advisory.
	 */
	static void CompareWithManifest(const FInkpotNativeBindingSourceModule& InModule, const FProsettaNativeBindingsManifest& InManifest, TArray<FString>& OutErrors, TArray<FString>& OutWarnings);

	/**
	 * Scans a directory, validates every module, compares with the optional manifest and
	 * produces loaders; writes them when bWrite is true ( only files whose content changed ).
	 */
	static FInkpotNativeBindingGenerationReport Run(const FString& InDirectory, const FProsettaNativeBindingsManifest* InManifest, bool bWrite);
};
