#pragma once

#include "CoreMinimal.h"
#include "Ink/Story.h"

class INKPOT_API FInkpotStoryInternal : public Ink::FStory
{
public:
	FInkpotStoryInternal( const FString &InJSON, int32 InStoryID );

	int32 GetID() const;
	bool IsValidStory() const;

	bool HasChoices() const;
	void ResetContent( TSharedPtr<FInkpotStoryInternal> InStory );

	// Root container of the compiled story, for content walks ( EXTERNAL discovery etc ).
	TSharedPtr<Ink::FContainer> GetMainContent() const { return MainContentContainer; }

	// Whether an unbound EXTERNAL may fall back to an Ink function of the same name.
	bool GetAllowExternalFunctionFallbacks() const { return allowExternalFunctionFallbacks; }
	void SetAllowExternalFunctionFallbacks( bool bInAllow ) { allowExternalFunctionFallbacks = bInAllow; }

public:
	static constexpr int32 BadStoryHandle {-1};

protected:
	int32	StoryID;
};
