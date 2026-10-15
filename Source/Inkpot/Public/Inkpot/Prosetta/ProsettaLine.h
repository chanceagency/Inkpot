#pragma once

#include "CoreMinimal.h"
#include "Inkpot/Prosetta/ProsettaTypes.h"
#include "Inkpot/Prosetta/ProsettaInlineTemplate.h"
#include "ProsettaLine.generated.h"

/**
 * UProsettaLine
 * A single hydrated segment parsed out of a fragment of story text. Each segment
 * corresponds to one <prosetta>...</prosetta> tag. It carries:
 *  - the line id and the data attributes declared on the tag,
 *  - the resolved authored metadata for that id (from the sidecar JSON),
 *  - the player-facing display text and where it sits within the cleaned fragment.
 *
 * The UI layer can render the cleaned fragment text and use the segment list to
 * drive per-line behaviour ( audio, animation cues, speaker attribution etc. ).
 *
 * Inline Ink expressions ( e.g. "{2+2}" ) in the authored strings are filled with the
 * values the Ink runtime emitted for this line, recovered from GetRuntimeText(). Use
 * RenderTemplate / GetLocalizedText to inject the same values into a translation.
 */
UCLASS(BlueprintType)
class INKPOT_API UProsettaLine : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Initialise
	 * @param InLineId      the value of the tag's 'id' attribute.
	 * @param InText        the player-facing text for this segment ( already resolved ).
	 * @param InAttributes  the non-id data attributes declared on the tag.
	 * @param InStart       offset of this segment's text within the cleaned fragment.
	 * @param InLength      length of this segment's text within the cleaned fragment.
	 */
	void Initialise(FName InLineId, const FString& InText, const TMap<FName, FString>& InAttributes, int32 InStart, int32 InLength);

	/** Apply the authored metadata resolved for this line's id. */
	void SetMetadata(const FProsettaLineMetadata& InMetadata, bool bInResolved);

	/**
	 * SetRuntimeText
	 * Record the Ink-evaluated text for this segment and the inline expression values
	 * recovered from it.
	 */
	void SetRuntimeText(const FString& InRuntimeText, FProsettaTemplateValues&& InValues);

	/**
	 * CaptureVariables
	 * Snapshot the current value of every variable placeholder ( e.g. "{hero}" ) in the
	 * authored string and its localizations, including variables referenced by string
	 * values that are themselves templates. Call at emission, after SetMetadata, so a
	 * placeholder the runtime text could not recover ( "{hero}{title}", or a variable only
	 * a translation uses ) still renders with emission-time values after the story moves on.
	 */
	void CaptureVariables(FProsettaInlineTemplate::FVariableResolver InResolver);

	/**
	 * FindLocalization
	 * The sidecar translation for a language code, matched case-insensitively with '_'
	 * and '-' equivalent ( "pt_BR" finds "pt-br" ), falling back to the base language
	 * ( "fr-CA" finds "fr" ). Returns nullptr when there is no non-empty translation.
	 */
	const FString* FindLocalization(const FString& InLanguageCode) const;

	/** Lowercases a language code, trims it and uses '-' as the region separator. */
	static FString NormalizeLanguageCode(const FString& InLanguageCode);

	/** The 'id' attribute of the source tag (e.g. "023A"). */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	FName GetLineId() const;

	/** The player-facing text for this segment. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	const FString& GetText() const;

	/** All non-id data attributes declared on the tag ( e.g. animate, sfx ). */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	const TMap<FName, FString>& GetAttributes() const;

	/** Look up a single data attribute by name. Returns true if present. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool GetAttribute(FName InName, FString& OutValue) const;

	/** Whether authored metadata was resolved for this line's id from the sidecar. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool IsResolved() const;

	/** The resolved authored metadata for this line ( valid only when IsResolved() ). */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	const FProsettaLineMetadata& GetMetadata() const;

	/** The text the Ink runtime emitted for this segment, inline expressions evaluated. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	const FString& GetRuntimeText() const;

	/**
	 * GetTemplateValue
	 * The value the runtime emitted for an inline expression of this line's authored
	 * string ( e.g. "GREAT_MEALS" or "2+2" ). Returns true when one was recovered.
	 */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	bool GetTemplateValue(const FString& InExpression, FString& OutValue) const;

	/** All inline expression values recovered for this line. */
	const FProsettaTemplateValues& GetTemplateValues() const;

	/**
	 * RenderTemplate
	 * Render a template for this line ( typically a translation of its authored string )
	 * with each {expression} replaced by the value the runtime emitted for it, else the
	 * variable value captured at emission ( CaptureVariables ), else its live value. Returns true when every expression was resolved;
	 * unresolved expressions are left verbatim in OutText.
	 */
	UFUNCTION(BlueprintCallable, Category="Inkpot|Prosetta")
	bool RenderTemplate(const FString& InTemplate, FString& OutText) const;

	/**
	 * GetLocalizedText
	 * The player-facing text for this line in the given language, from the sidecar
	 * localizations ( see FindLocalization ) with inline expressions rendered. Falls back to GetText() and
	 * returns false when no translation exists or one of its expressions is unresolved.
	 */
	UFUNCTION(BlueprintCallable, Category="Inkpot|Prosetta")
	bool GetLocalizedText(const FString& InLanguageCode, FString& OutText) const;

	/** Offset of this segment's text within the cleaned fragment. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	int32 GetStartOffset() const;

	/** Length of this segment's text within the cleaned fragment. */
	UFUNCTION(BlueprintPure, Category="Inkpot|Prosetta")
	int32 GetLength() const;

private:
	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	FName LineId;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	FString Text;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	FString RuntimeText;

	FProsettaTemplateValues TemplateValues;

	/** Emission-time values of variable placeholders, keyed by variable name. */
	TMap<FString, FString> VariableSnapshot;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	TMap<FName, FString> Attributes;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	FProsettaLineMetadata Metadata;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	bool bResolved = false;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	int32 StartOffset = 0;

	UPROPERTY(VisibleAnywhere, Category="Inkpot|Prosetta")
	int32 Length = 0;
};
