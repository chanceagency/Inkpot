#pragma once 

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InkpotSettings.generated.h"

// Runtime settings for Inkpot
UCLASS(Config=Inkpot, defaultconfig, meta = (DisplayName="Inkpot Settings"))
class INKPOT_API UInkpotSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/*
	Factory for creating an Inkpot story
	*/
	UPROPERTY(config, noclear, EditAnywhere, Category = "Setup", meta = (MetaClass = "/Script/Inkpot.InkpotStoryFactoryBase"))
	FSoftClassPath StoryFactoryClass {"/Script/Inkpot.InkpotStoryFactory"};

	/*
	Custom import, a editor utility object to manage ink script import pipeline
	*/
	UPROPERTY(config, EditAnywhere, Category = "Setup", meta = (MetaClass = "/Script/InkpotEditor.InkpotImportPipeline"))
	FSoftClassPath CustomImportClass;

	/*
	Bind the C++ native binding modules registered with Inkpot ( Prosetta Native Bound
	Functions and local modules ) to every story's EXTERNALs when the story begins.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Native Bindings")
	bool bAutoBindNativeFunctions {true};

	/*
	On a story's first Continue, check that every EXTERNAL it calls is bound and log an
	error for each one that is not. The Ink VM aborts when it reaches an unbound EXTERNAL.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Native Bindings")
	bool bValidateExternalBindingsOnFirstContinue {true};

	/*
	Let an unbound EXTERNAL fall back to an Ink function of the same name, when the
	story defines one. Matches Ink's allowExternalFunctionFallbacks.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Native Bindings")
	bool bAllowExternalFunctionFallbacks {false};

	/*
	Log runtime errors and warnings reported by the Ink VM through the Inkpot log
	category, in addition to broadcasting the story's OnStoryError event.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Diagnostics")
	bool bLogStoryErrors {true};

	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

};

