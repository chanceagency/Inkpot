#pragma once

#include "CoreMinimal.h"
#include "Ink/Value.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingTypes.h"
#include "Utility/InkpotLog.h"
#include <type_traits>
#include <utility>

/**
 * Template glue between Ink values and plain C++ functions, in the spirit of
 * inkcpp's `runner->bind("name", &Function)`: the glue derives the arity and the
 * argument conversions from the function signature, so a Prosetta C++ module
 * ( namespace Prosetta::Bindings::<Module> { int Fn(int, const char*) {...} } )
 * needs no engine-specific code of its own.
 *
 * Supported parameter types: int, float, bool, const char*.
 * Supported return types:    int, float, bool, const char*, void.
 * These are exactly the types Prosetta's C++ module validator accepts.
 *
 * A returned `const char*` is copied into an FString before the call returns,
 * so the pointer only needs to stay valid until then ( literals, statics ).
 */
namespace InkpotNativeBinding
{
	/** Thunk signature the Ink runtime calls. A null result means `void`. */
	using FThunk = TFunction<TSharedPtr<Ink::FValueType>(const TArray<Ink::FValueType>&)>;

	/** Keeps UTF-8 copies of string arguments alive for the duration of one call. */
	struct FCallScope
	{
		TArray<TArray<UTF8CHAR>> Strings;

		const char* Keep(const FString& InValue)
		{
			FTCHARToUTF8 converter(*InValue, InValue.Len());
			TArray<UTF8CHAR>& storage = Strings.AddDefaulted_GetRef();
			storage.Append(reinterpret_cast<const UTF8CHAR*>(converter.Get()), converter.Length());
			storage.Add(UTF8CHAR(0));
			return reinterpret_cast<const char*>(storage.GetData());
		}
	};

	/** Formats an Ink value for diagnostics / string coercion. */
	inline FString ValueToString(const Ink::FValueType& InValue)
	{
		if (InValue.HasSubtype<FString>())
			return InValue.GetSubtype<FString>();
		if (InValue.HasSubtype<int32>())
			return FString::FromInt(InValue.GetSubtype<int32>());
		if (InValue.HasSubtype<float>())
			return FString::Printf(TEXT("%g"), InValue.GetSubtype<float>());
		if (InValue.HasSubtype<bool>())
			return InValue.GetSubtype<bool>() ? TEXT("true") : TEXT("false");
		return TEXT("<unsupported>");
	}

	template<typename T> struct TArg;

	template<> struct TArg<int>
	{
		static const TCHAR* TypeName() { return TEXT("int"); }
		static int Get(FCallScope&, const Ink::FValueType& V, bool& bOk)
		{
			if (V.HasSubtype<int32>()) return V.GetSubtype<int32>();
			if (V.HasSubtype<bool>())  return V.GetSubtype<bool>() ? 1 : 0;
			if (V.HasSubtype<float>()) return static_cast<int>(V.GetSubtype<float>());
			bOk = false;
			return 0;
		}
	};

	template<> struct TArg<float>
	{
		static const TCHAR* TypeName() { return TEXT("float"); }
		static float Get(FCallScope&, const Ink::FValueType& V, bool& bOk)
		{
			if (V.HasSubtype<float>()) return V.GetSubtype<float>();
			if (V.HasSubtype<int32>()) return static_cast<float>(V.GetSubtype<int32>());
			if (V.HasSubtype<bool>())  return V.GetSubtype<bool>() ? 1.0f : 0.0f;
			bOk = false;
			return 0.0f;
		}
	};

	template<> struct TArg<bool>
	{
		static const TCHAR* TypeName() { return TEXT("bool"); }
		static bool Get(FCallScope&, const Ink::FValueType& V, bool& bOk)
		{
			if (V.HasSubtype<bool>())  return V.GetSubtype<bool>();
			if (V.HasSubtype<int32>()) return V.GetSubtype<int32>() != 0;
			if (V.HasSubtype<float>()) return V.GetSubtype<float>() != 0.0f;
			bOk = false;
			return false;
		}
	};

	template<> struct TArg<const char*>
	{
		static const TCHAR* TypeName() { return TEXT("const char*"); }
		static const char* Get(FCallScope& Scope, const Ink::FValueType& V, bool& bOk)
		{
			if (V.HasSubtype<FString>() || V.HasSubtype<int32>() || V.HasSubtype<float>() || V.HasSubtype<bool>())
				return Scope.Keep(ValueToString(V));
			bOk = false;
			return Scope.Keep(FString());
		}
	};

	template<typename T> struct TReturn;

	template<> struct TReturn<void>
	{
		static const TCHAR* TypeName() { return TEXT("void"); }
	};

	template<> struct TReturn<int>
	{
		static const TCHAR* TypeName() { return TEXT("int"); }
		static TSharedPtr<Ink::FValueType> Make(int V) { return MakeShared<Ink::FValueType>(static_cast<int32>(V)); }
	};

	template<> struct TReturn<float>
	{
		static const TCHAR* TypeName() { return TEXT("float"); }
		static TSharedPtr<Ink::FValueType> Make(float V) { return MakeShared<Ink::FValueType>(V); }
	};

	template<> struct TReturn<bool>
	{
		static const TCHAR* TypeName() { return TEXT("bool"); }
		static TSharedPtr<Ink::FValueType> Make(bool V) { return MakeShared<Ink::FValueType>(V); }
	};

	template<> struct TReturn<const char*>
	{
		static const TCHAR* TypeName() { return TEXT("const char*"); }
		static TSharedPtr<Ink::FValueType> Make(const char* V)
		{
			return MakeShared<Ink::FValueType>(V ? FString(UTF8_TO_TCHAR(V)) : FString());
		}
	};

	template<typename R, typename... Args, size_t... I>
	TSharedPtr<Ink::FValueType> Invoke(R (*Fn)(Args...), FCallScope& Scope, const TArray<Ink::FValueType>& In, bool& bOk, std::index_sequence<I...>)
	{
		if constexpr (std::is_void_v<R>)
		{
			Fn(TArg<Args>::Get(Scope, In[I], bOk)...);
			return nullptr;
		}
		else
		{
			return TReturn<R>::Make(Fn(TArg<Args>::Get(Scope, In[I], bOk)...));
		}
	}

	/** Builds a thunk and a descriptor for a free function. */
	template<typename R, typename... Args>
	void Describe(const FString& InName, R (*)(Args...), bool bInLookaheadSafe, FInkpotNativeFunctionDescriptor& OutDescriptor)
	{
		OutDescriptor.Name = InName;
		OutDescriptor.Arity = sizeof...(Args);
		OutDescriptor.bLookaheadSafe = bInLookaheadSafe;
		OutDescriptor.ParameterTypes = { FString(TArg<Args>::TypeName())... };
		OutDescriptor.ReturnType = TReturn<R>::TypeName();
	}

	template<typename R, typename... Args>
	FThunk MakeThunk(const FString& InName, R (*Fn)(Args...))
	{
		return [Fn, InName](const TArray<Ink::FValueType>& In) -> TSharedPtr<Ink::FValueType>
		{
			constexpr int32 expected = sizeof...(Args);
			if (In.Num() != expected)
			{
				INKPOT_ERROR("Native binding '%s' expects %d argument(s) but Ink passed %d.", *InName, expected, In.Num());
				return nullptr;
			}
			FCallScope scope;
			bool bOk = true;
			TSharedPtr<Ink::FValueType> result = Invoke(Fn, scope, In, bOk, std::index_sequence_for<Args...>{});
			if (!bOk)
				INKPOT_ERROR("Native binding '%s' received an argument of an unsupported Ink type ( list or divert target ).", *InName);
			return result;
		};
	}
}
