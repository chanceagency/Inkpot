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
	void Initialise(const FString &String);

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
	const TArray<TObjectPtr<UProsettaLine>>& GetProsettaSegments() const;

	/**
	 * HasProsettaTags
	 * Whether this line contained any <prosetta> markup.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	bool HasProsettaTags() const;

	UFUNCTION(BlueprintPure, Category="Inkpot|Line")
	const FText& GetText() const;

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

	UPROPERTY(VisibleAnywhere, Category = "Inkpot|Line")
	bool bIsDirty;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Line")
	TArray<TObjectPtr<UProsettaLine>> ProsettaSegments;
};
