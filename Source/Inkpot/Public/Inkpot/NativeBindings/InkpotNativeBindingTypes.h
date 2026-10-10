#pragma once

#include "CoreMinimal.h"
#include "InkpotNativeBindingTypes.generated.h"

/**
 * Where a native binding module came from.
 *  - Prosetta: synced from Prosetta Native Bound Functions ( native-bindings/cpp/<Module>.cpp ).
 *  - Local:    authored in the game project, following the same conventions.
 */
UENUM(BlueprintType)
enum class EInkpotNativeBindingSource : uint8
{
	Prosetta,
	Local
};

/**
 * FInkpotNativeFunctionDescriptor
 * Describes one C++ function registered as an Ink EXTERNAL implementation.
 * The parameter and return type names use Prosetta's C++ spelling:
 * int, float, bool, const char* and void.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FInkpotNativeFunctionDescriptor
{
	GENERATED_BODY()

	/** The EXTERNAL name as declared in Ink. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString Name;

	/** Module the function belongs to ( namespace Prosetta::Bindings::<Module> ). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString ModuleName;

	/** Number of parameters. Must match the Ink EXTERNAL declaration. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	int32 Arity = 0;

	/** True when the Ink VM may call the function while looking ahead ( no side effects ). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	bool bLookaheadSafe = false;

	/** C++ parameter types, in order. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FString> ParameterTypes;

	/** C++ return type ( void when nothing is returned ). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString ReturnType;
};

/**
 * FInkpotNativeBindingModuleInfo
 * Blueprint-visible summary of a registered native binding module.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FInkpotNativeBindingModuleInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	EInkpotNativeBindingSource Source = EInkpotNativeBindingSource::Local;

	/** SHA-256 of the authored module source recorded by the loader, empty when unknown. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString SourceSha256;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FInkpotNativeFunctionDescriptor> Functions;
};

/**
 * FInkpotExternalDeclaration
 * An EXTERNAL function the compiled story calls, recovered from the story content.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FInkpotExternalDeclaration
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	int32 Arity = 0;

	/** True when the story also defines an Ink fallback function of the same name. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	bool bHasFallback = false;
};

/**
 * FInkpotNativeBindingReport
 * Result of binding the registered native functions into a story.
 */
USTRUCT(BlueprintType)
struct INKPOT_API FInkpotNativeBindingReport
{
	GENERATED_BODY()

	/** Functions bound into the story by this call. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FString> Bound;

	/** Functions skipped because something was already bound under that name. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FString> AlreadyBound;

	/** Registered functions whose arity differs from the story's EXTERNAL declaration. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FString> ArityMismatch;

	/** EXTERNAL declarations in the story that remain unbound after this call. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FString> Unbound;

	/** Human readable notes, one per problem. */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	TArray<FString> Messages;

	/** True when every EXTERNAL the story calls is bound ( or has an allowed fallback ). */
	UPROPERTY(BlueprintReadOnly, Category="Inkpot|NativeBindings")
	bool bComplete = false;
};

/** Severity reported by the Ink runtime for a story error. */
UENUM(BlueprintType)
enum class EInkpotStoryErrorType : uint8
{
	Author,
	Warning,
	Error
};

/**
 * What Inkpot does with a story error or warning after broadcasting OnStoryError.
 * Chosen per severity in UInkpotSettings.
 */
UENUM(BlueprintType)
enum class EInkpotStoryErrorAction : uint8
{
	/** Only broadcast the OnStoryError event; nothing is logged. */
	BroadcastOnly,
	/** Log through the Inkpot log category ( Output Log; Message Log and screen are separate switches ). */
	Log,
	/** Log, then trip an ensure so a debugger breaks and a non-fatal crash report is written. */
	Ensure,
	/** Log, then fail fatally. The closest equivalent to an uncaught C# ink exception. */
	Fatal
};
