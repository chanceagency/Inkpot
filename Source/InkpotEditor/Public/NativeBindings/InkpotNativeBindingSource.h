#pragma once

#include "CoreMinimal.h"

/**
 * Source-level model of one native binding module file, as authored in Prosetta or
 * locally, following the same convention:
 *
 *   namespace Prosetta::Bindings::<ModuleName> {
 *       // @lookaheadSafe
 *       int Fn(int a, const char* b) { ... }
 *   }
 *
 * The scanner mirrors Prosetta's C++ validator ( lib/native-bindings/signature-cpp.ts ):
 * one nested namespace named after the module, free functions with int / float / bool /
 * const char* parameters and those or void as return type, no overloads, variadics,
 * default arguments, entry points or engine SDK headers.
 */

struct INKPOTEDITOR_API FInkpotNativeBindingSourceParameter
{
	FString Type;
	FString Name;
};

struct INKPOTEDITOR_API FInkpotNativeBindingSourceFunction
{
	FString Name;
	FString ReturnType;
	TArray<FInkpotNativeBindingSourceParameter> Parameters;
	/** `// @lookaheadSafe` on the line above the signature. */
	bool bLookaheadSafe = false;
	/** Body is empty or only returns a zero literal ( a generated stub ). */
	bool bStub = false;
	/** 1-based line of the declaration. */
	int32 Line = 0;

	int32 Arity() const { return Parameters.Num(); }
	/** `const char* describe_item(const char* item_name, int quantity)` */
	FString Signature() const;
};

struct INKPOTEDITOR_API FInkpotNativeBindingSourceDiagnostic
{
	/** "error" or "warning". */
	FString Severity;
	/** Stable code, e.g. cpp-namespace-missing, cpp-return-type, cpp-overload. */
	FString Code;
	FString Message;
	/** 1-based line, or 0 when the diagnostic has no position. */
	int32 Line = 0;

	bool IsError() const { return Severity == TEXT("error"); }
	FString ToString() const;
};

/** The three-line provenance header Prosetta prepends to every shipped module file. */
struct INKPOTEDITOR_API FInkpotNativeBindingProvenance
{
	bool bPresent = false;
	FString ModuleName;
	FString Language;
	int32 ProjectId = 0;
	int32 ScriptId = 0;
	/** sha256 hex of the authored source, as recorded in the header. */
	FString SourceSha256;
};

struct INKPOTEDITOR_API FInkpotNativeBindingSourceModule
{
	FString ModuleName;
	/** Absolute path when scanned from disk. */
	FString Filename;
	TArray<FInkpotNativeBindingSourceFunction> Functions;
	TArray<FInkpotNativeBindingSourceDiagnostic> Diagnostics;
	FInkpotNativeBindingProvenance Provenance;
	/** sha256 of the authored source: the bytes after the provenance header, or the whole file without one. */
	FString AuthoredSha256;

	bool HasErrors() const;
	int32 ErrorCount() const;
	int32 WarningCount() const;
	/** The module namespace was found and no error diagnostic was raised. */
	bool IsValid() const { return !ModuleName.IsEmpty() && !HasErrors(); }
	const FInkpotNativeBindingSourceFunction* FindFunction(const FString& InName) const;
};

class INKPOTEDITOR_API FInkpotNativeBindingSourceScanner
{
public:
	static const TCHAR* NamespacePrefix() { return TEXT("Prosetta::Bindings::"); }

	/**
	 * Scans module source text. When InExpectedModuleName is empty the module name is
	 * taken from the Prosetta provenance header, else from the first
	 * `namespace Prosetta::Bindings::<Name>` found.
	 */
	static FInkpotNativeBindingSourceModule Scan(const FString& InContent, const FString& InExpectedModuleName = FString());

	/** Reads and scans a file; the expected module name defaults to the file's base name. */
	static bool ScanFile(const FString& InFilename, FInkpotNativeBindingSourceModule& OutModule, bool bUseFilenameAsModuleName = true);

	/** Cheap test used by directory discovery: does the text declare a Prosetta::Bindings namespace? */
	static bool LooksLikeModule(const FString& InContent);

	/** Blanks comments and string/char literal contents, preserving length and newlines. */
	static FString StripCommentsAndStrings(const FString& InContent);

	/** Parses the Prosetta provenance header, if present, and returns the authored source offset. */
	static bool ParseProvenance(const FString& InContent, FInkpotNativeBindingProvenance& OutProvenance, int32& OutAuthoredOffset);

	/** sha256 hex ( lowercase ) of the UTF-8 encoding of a string. */
	static FString Sha256(const FString& InText);

	/** Whether an identifier is a valid module name: ^[A-Za-z_][A-Za-z0-9_]*$ and at most 64 characters. */
	static bool IsValidModuleName(const FString& InName);
};
