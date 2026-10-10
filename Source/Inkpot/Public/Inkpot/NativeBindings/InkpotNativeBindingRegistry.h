#pragma once

#include "CoreMinimal.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingTypes.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingGlue.h"

/** One registered native function: its description plus the callable thunk. */
struct INKPOT_API FInkpotNativeFunction
{
	FInkpotNativeFunctionDescriptor Descriptor;
	InkpotNativeBinding::FThunk Thunk;

	template<typename R, typename... Args>
	static FInkpotNativeFunction Make(const FString& InName, R (*Fn)(Args...), bool bInLookaheadSafe)
	{
		FInkpotNativeFunction function;
		InkpotNativeBinding::Describe(InName, Fn, bInLookaheadSafe, function.Descriptor);
		function.Thunk = InkpotNativeBinding::MakeThunk(InName, Fn);
		return function;
	}
};

/** One native binding module: the functions declared in namespace Prosetta::Bindings::<Name>. */
struct INKPOT_API FInkpotNativeBindingModule
{
	FString Name;
	EInkpotNativeBindingSource Source = EInkpotNativeBindingSource::Local;
	/** SHA-256 of the authored module source when known ( recorded by the generated loader ). */
	FString SourceSha256;
	TArray<FInkpotNativeFunction> Functions;

	FInkpotNativeBindingModuleInfo ToInfo() const;
};

/**
 * FInkpotNativeBindingRegistry
 * Process-wide registry of native binding modules. Modules register themselves from
 * static initialisers ( see the INKPOT_NATIVE_BINDING_* macros ), so any module linked
 * into the game is known before the first story begins. Stories bind the registered
 * functions through UInkpotStory::BindRegisteredNativeFunctions.
 */
class INKPOT_API FInkpotNativeBindingRegistry
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);

	static FInkpotNativeBindingRegistry& Get();

	/**
	 * Registers ( or replaces ) a module. Returns a token identifying this registration;
	 * pass it to UnregisterModule so a stale registration ( hot reload ) cannot remove a newer one.
	 */
	uint64 RegisterModule(FInkpotNativeBindingModule&& InModule);
	void UnregisterModule(const FString& InModuleName, uint64 InToken);

	const FInkpotNativeBindingModule* FindModule(const FString& InModuleName) const;
	const FInkpotNativeFunction* FindFunction(const FString& InFunctionName, const FInkpotNativeBindingModule** OutModule = nullptr) const;

	/** All modules, sorted by name. */
	TArray<const FInkpotNativeBindingModule*> GetModules() const;
	TArray<FInkpotNativeBindingModuleInfo> GetModuleInfos() const;

	/** Function names implemented by more than one registered module. */
	TArray<FString> FindDuplicateFunctionNames() const;

	int32 NumModules() const { return Modules.Num(); }

	FOnChanged OnChanged;

private:
	struct FEntry
	{
		uint64 Token = 0;
		TSharedPtr<FInkpotNativeBindingModule> Module;
	};
	TMap<FString, FEntry> Modules;
	uint64 NextToken = 1;
};

/**
 * Loader macros. A loader is a small Inkpot-side file that forward declares the module's
 * functions and registers them; the Prosetta module itself stays engine agnostic.
 *
 *   namespace Prosetta::Bindings::Inventory { const char* describe_item(const char*, int); void play_chime(); }
 *
 *   INKPOT_NATIVE_BINDING_MODULE_BEGIN(Inventory, EInkpotNativeBindingSource::Prosetta, "<sha256 or empty>")
 *       INKPOT_NATIVE_BINDING(describe_item, false)
 *       INKPOT_NATIVE_BINDING(play_chime, true)
 *   INKPOT_NATIVE_BINDING_MODULE_END(Inventory)
 *
 * The editor can generate these files from the module source: Prosetta.NativeBindings.Generate.
 */
#define INKPOT_NATIVE_BINDING_MODULE_BEGIN(ModuleName, SourceKind, SourceSha256Literal) \
	namespace \
	{ \
		struct FInkpotNativeBindingRegistrar_##ModuleName \
		{ \
			uint64 Token = 0; \
			FInkpotNativeBindingRegistrar_##ModuleName() \
			{ \
				namespace InkpotBindingModule = Prosetta::Bindings::ModuleName; \
				FInkpotNativeBindingModule Module; \
				Module.Name = TEXT(#ModuleName); \
				Module.Source = SourceKind; \
				Module.SourceSha256 = TEXT(SourceSha256Literal);

#define INKPOT_NATIVE_BINDING(FunctionName, bLookaheadSafe) \
				Module.Functions.Add(FInkpotNativeFunction::Make(TEXT(#FunctionName), &InkpotBindingModule::FunctionName, bLookaheadSafe));

#define INKPOT_NATIVE_BINDING_MODULE_END(ModuleName) \
				Token = FInkpotNativeBindingRegistry::Get().RegisterModule(MoveTemp(Module)); \
			} \
			~FInkpotNativeBindingRegistrar_##ModuleName() \
			{ \
				FInkpotNativeBindingRegistry::Get().UnregisterModule(TEXT(#ModuleName), Token); \
			} \
		} GInkpotNativeBindingRegistrar_##ModuleName; \
	}
