#pragma once

#include "CoreMinimal.h"

namespace Ink
{
	// we can't write exceptions in this code, but the c# makes quite extensive use of it
	// this struct is an attempt to get closer to the source code we are copying
	class INKPLUSPLUS_API FStoryExceptionSubstitute
	{
	public:
		// Fired for every Throw. When bound, the message is handed to the listeners instead
		// of being logged here, so a host ( Inkpot ) can apply its own error policy.
		DECLARE_MULTICAST_DELEGATE_OneParam(FOnThrow, const FString&);
		FOnThrow OnThrow;

		bool hasException = false;
		bool useEndLineNumber = false;
		FString message = "";

		void Throw(const FString& InMessage, bool InUseEndLineNumber = false);
		void Reset();
	};
}
