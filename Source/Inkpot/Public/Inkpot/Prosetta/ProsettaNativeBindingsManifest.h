#pragma once

#include "CoreMinimal.h"
#include "ProsettaNativeBindingsManifest.generated.h"

class UProsettaSyncedArtifact;
class UProsettaGenerationAsset;

/**
 * Mirror of Prosetta's Engine Sync v1 `native-bindings/manifest.json` ( formatVersion 1 ).
 * Served by GET /api/engine-sync/v1/project/{projectId}/native-bindings and mirrored by
 * the sync CLI to native-bindings/manifest.json. One module file per shipped language is
 * mirrored beside it at native-bindings/{language}/{Module}.{ext}; C++ modules are
 * native-bindings/cpp/{Module}.cpp.
 */

UENUM(BlueprintType)
enum class EProsettaNativeBindingStatus : uint8
{
	Shipped,
	Excluded
};

USTRUCT(BlueprintType)
struct INKPOT_API FProsettaNativeBindingFunction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 Arity = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	bool bLookaheadSafe = false;
};

USTRUCT(BlueprintType)
struct INKPOT_API FProsettaNativeBindingDiagnostic
{
	GENERATED_BODY()

	/** "error" or "warning". */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Severity;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Message;

	/** 1-based line, or -1 when the diagnostic has no position. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 Line = -1;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Code;
};

/** One language implementation ( typescript, javascript, csharp, cpp ) of a module. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaNativeBindingLanguageEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	EProsettaNativeBindingStatus Status = EProsettaNativeBindingStatus::Excluded;

	/** Exclusion reason ( validation_errors, invalid_module_name, ... ); empty when shipped. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Reason;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 ScriptId = 0;

	/** SHA-256 of the authored source ( before the provenance header ). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString SourceSha256;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FProsettaNativeBindingFunction> Functions;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 ErrorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 WarningCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FProsettaNativeBindingDiagnostic> Diagnostics;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString UpdatedAt;

	/** Shipped only: native-binding:{language}/{Module}. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString ArtifactKey;

	/** Shipped only: native-bindings/{language}/{Module}.{ext}. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString RelativePath;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 ByteSize = 0;

	bool IsShipped() const { return Status == EProsettaNativeBindingStatus::Shipped; }
	const FProsettaNativeBindingFunction* FindFunction(const FString& InName) const;
};

USTRUCT(BlueprintType)
struct INKPOT_API FProsettaNativeBindingModule
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString ModuleName;

	/** Keyed by sync language: typescript, javascript, csharp, cpp. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TMap<FString, FProsettaNativeBindingLanguageEntry> Languages;

	const FProsettaNativeBindingLanguageEntry* FindLanguage(const FString& InLanguage) const { return Languages.Find(InLanguage); }
	const FProsettaNativeBindingLanguageEntry* FindCpp() const { return Languages.Find(TEXT("cpp")); }
};

USTRUCT(BlueprintType)
struct INKPOT_API FProsettaExternalDeclarationSite
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 InkScriptId = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString QualifiedName;
};

/** An EXTERNAL declaration found in the project's Ink. */
USTRUCT(BlueprintType)
struct INKPOT_API FProsettaExternalDeclaration
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 Arity = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FString> Params;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FProsettaExternalDeclarationSite> DeclaredIn;
};

USTRUCT(BlueprintType)
struct INKPOT_API FProsettaNativeBindingCoverage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 Bound = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 Missing = 0;
};

USTRUCT(BlueprintType)
struct INKPOT_API FProsettaNativeBindingsManifest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 FormatVersion = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	int32 ProjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString ProjectName;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	FString GeneratedAt;

	/** Informational: web, unity, unreal. Every authored language ships regardless. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FString> EngineTargets;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FProsettaExternalDeclaration> Externals;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FProsettaNativeBindingModule> Modules;

	/** EXTERNAL names no shipped module implements in any language. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TArray<FString> Unbound;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	TMap<FString, FProsettaNativeBindingCoverage> CoverageByLanguage;

	/** True once Parse has accepted a document. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|Prosetta")
	bool bValid = false;

	const FProsettaNativeBindingModule* FindModule(const FString& InModuleName) const;
	const FProsettaExternalDeclaration* FindExternal(const FString& InName) const;

	/** Shipped C++ modules, sorted by name. */
	TArray<const FProsettaNativeBindingModule*> GetShippedCppModules() const;

	/**
	 * Parses a manifest document. Rejects anything that is not formatVersion 1 or that is
	 * missing the required fields. A rejected parse leaves OutManifest invalid.
	 */
	static bool Parse(const FString& InJSON, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** Reads and parses native-bindings/manifest.json from disk. */
	static bool LoadFile(const FString& InFilename, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** Parses the manifest held in a synced artifact ( key "native-bindings" ) imported by the Prosetta watch. */
	static bool LoadFromArtifact(const UProsettaSyncedArtifact* InArtifact, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** Finds the "native-bindings" artifact in a watch generation and parses it. */
	static bool LoadFromGeneration(const UProsettaGenerationAsset* InGeneration, FProsettaNativeBindingsManifest& OutManifest, FString& OutError);

	/** The artifact key the manifest is imported under by the Prosetta watch. */
	static const TCHAR* ArtifactKey() { return TEXT("native-bindings"); }

	/** Relative path of the manifest inside a sync mirror or CLI generation. */
	static const TCHAR* RelativePath() { return TEXT("native-bindings/manifest.json"); }
};
