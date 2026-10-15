#pragma once

#include "CoreMinimal.h"
#include "InkpotLine.generated.h"

class UInkpotStory;
class UProsettaLine;

UCLASS(BlueprintType)
class INKPOT_API UInkpotLine : public UObject
{
	GENERATED_BODY()

public:
	UInkpotLine();

	/**
	 * Initialise
	 * @param String The line's text as the runtime emitted it.
	 * @param Tags The line's tags, copied so the line keeps them after the story moves on.
	 */
	void Initialise(const FString &String, const TArray<FString> &Tags);

	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	const FString& GetString() const;

	/**
	 * GetRawString
	 * The original story text for this line, before any <prosetta> markup was stripped.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	const FString& GetRawString() const;

	/**
	 * GetProsettaSegments
	 * The hydrated Prosetta segments parsed from this line, in order of appearance.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	TArray<UProsettaLine*> GetProsettaSegments() const;

	/**
	 * HasProsettaTags
	 * Whether this line contained any <prosetta> markup.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	bool HasProsettaTags() const;

	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	const FText& GetText() const;

	/**
	 * GetTags
	 * The tags of this line, as they were when the line was created. They are a copy, not
	 * the story's current tags: a line kept after the story continues, switches flow or runs
	 * ContinueMaximally still reports its own tags ( e.g. its #pause ), not the newer line's.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	const TArray<FString> &GetTags() const;

	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	UInkpotStory* GetStory() const;

	UFUNCTION(BlueprintPure, Category = "Inkpot|Line")
	bool IsDirty();

	UFUNCTION(BlueprintCallable, Category = "Inkpot|Line")
	void SetDirty(bool bIsDirty );

private:
	virtual const TArray<FString> &GetTagsInternal() const;

private:
	UPROPERTY(VisibleAnywhere, Category="Inkpot|Line")
	FString String;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Line")
	FString RawString;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Line")
	FText Text;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Line")
	TArray<FString> Tags;

	UPROPERTY(VisibleAnywhere, Category = "Inkpot|Line")
	bool bIsDirty;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Line")
	TArray<TObjectPtr<UProsettaLine>> ProsettaSegments;
};
