#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingTypes.h"
#include "Inkpot/Prosetta/ProsettaNativeBindingsManifest.h"
#include "InkpotNativeBindingLibrary.generated.h"

class UInkpotStory;
class UProsettaSyncedArtifact;
class UProsettaGenerationAsset;

UENUM(BlueprintType)
enum class EInkpotNativeBindingAuditSeverity : uint8
{
	Info,
	Warning,
	Error
};

/** One finding from comparing the registered native modules with a Prosetta manifest. */
USTRUCT(BlueprintType)
struct INKPOT_API FInkpotNativeBindingAuditFinding
{
	GENERATED_BODY()

	/**
	 * Stable code: module_not_registered, module_not_in_manifest, module_not_shipped,
	 * function_missing, function_extra, arity_mismatch, lookahead_mismatch,
	 * source_sha_mismatch, external_unbound, duplicate_function.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString Code;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	EInkpotNativeBindingAuditSeverity Severity = EInkpotNativeBindingAuditSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString ModuleName;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString FunctionName;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString Message;
};

USTRUCT(BlueprintType)
struct INKPOT_API FInkpotNativeBindingAudit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FInkpotNativeBindingAuditFinding> Findings;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	int32 ErrorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	int32 WarningCount = 0;

	/** True when no error-level finding was raised. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	bool bPassed = false;

	void Add(const FString& InCode, EInkpotNativeBindingAuditSeverity InSeverity, const FString& InModule, const FString& InFunction, const FString& InMessage);
	void Finalise();
	FString ToString() const;
};

/**
 * UInkpotNativeBindingLibrary
 * Blueprint and C++ entry points for Prosetta Native Bound Functions in Inkpot:
 * inspect the registered C++ modules, bind them into a story, load the synced
 * manifest and audit the registry against it.
 */
UCLASS()
class INKPOT_API UInkpotNativeBindingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Every native binding module registered in this process, sorted by name. */
	UFUNCTION(BlueprintPure, Category="Inkpot|NativeBindings")
	static TArray<FInkpotNativeBindingModuleInfo> GetRegisteredModules();

	/** Looks up one registered function by its Ink EXTERNAL name. */
	UFUNCTION(BlueprintPure, Category="Inkpot|NativeBindings")
	static bool FindRegisteredFunction(const FString& FunctionName, FInkpotNativeFunctionDescriptor& OutFunction);

	/** Function names implemented by more than one registered module ( first registered wins when binding ). */
	UFUNCTION(BlueprintPure, Category="Inkpot|NativeBindings")
	static TArray<FString> GetDuplicateFunctionNames();

	/**
	 * Binds every registered native function into the story. Equivalent to
	 * UInkpotStory::BindRegisteredNativeFunctions with no module filter.
	 */
	UFUNCTION(BlueprintCallable, Category="Inkpot|NativeBindings")
	static FInkpotNativeBindingReport BindRegisteredNativeFunctions(UInkpotStory* Story);

	/** Parses a native-bindings/manifest.json document. */
	UFUNCTION(BlueprintCallable, Category="Inkpot|NativeBindings")
	static bool ParseManifest(const FString& JSON, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** Reads native-bindings/manifest.json from an absolute path ( e.g. a Saved/Prosetta sync mirror ). */
	UFUNCTION(BlueprintCallable, Category="Inkpot|NativeBindings")
	static bool LoadManifestFile(const FString& Filename, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** Loads the manifest from the "native-bindings" artifact imported by the Prosetta watch. */
	UFUNCTION(BlueprintCallable, Category="Inkpot|NativeBindings")
	static bool LoadManifestFromArtifact(const UProsettaSyncedArtifact* Artifact, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** Finds and loads the manifest held by a Prosetta watch generation asset. */
	UFUNCTION(BlueprintCallable, Category="Inkpot|NativeBindings")
	static bool LoadManifestFromGeneration(const UProsettaGenerationAsset* Generation, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** The default sync mirror manifest path: <Project>/Saved/Prosetta/native-bindings/manifest.json. */
	UFUNCTION(BlueprintPure, Category="Inkpot|NativeBindings")
	static FString GetDefaultMirrorManifestPath();

	/**
	 * Compares the registered modules with the manifest: every shipped C++ module must be
	 * registered with the same functions, arities and lookahead flags, and a loader that
	 * recorded a source SHA-256 must match the shipped source. Local modules are reported
	 * only when they collide with Prosetta functions or cover an unbound external.
	 */
	UFUNCTION(BlueprintCallable, Category="Inkpot|NativeBindings")
	static FInkpotNativeBindingAudit AuditRegistryAgainstManifest(const FProsettaNativeBindingsManifest& Manifest);

	/**
	 * Audits a running story: compares the EXTERNALs it calls with what is bound, and
	 * when a manifest is supplied, with the manifest's shipped C++ coverage.
	 */
	UFUNCTION(BlueprintCallable, Category="Inkpot|NativeBindings")
	static FInkpotNativeBindingAudit AuditStory(UInkpotStory* Story, const FProsettaNativeBindingsManifest& Manifest, bool bUseManifest = true);

	/** Formats an audit as one line per finding. */
	UFUNCTION(BlueprintPure, Category="Inkpot|NativeBindings")
	static FString AuditToString(const FInkpotNativeBindingAudit& Audit);
};
