#pragma once 

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingTypes.h"
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
	What Inkpot does with an error the Ink VM reports while running a story ( runtime
	errors, unbound EXTERNALs, native binding mismatches ). The story's OnStoryError
	event is broadcast first in every case, so game code can add its own handling.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Diagnostics")
	EInkpotStoryErrorAction StoryErrorAction {EInkpotStoryErrorAction::Log};

	/*
	What Inkpot does with a warning the Ink VM reports while running a story.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Diagnostics")
	EInkpotStoryErrorAction StoryWarningAction {EInkpotStoryErrorAction::Log};

	/*
	Also send story errors and warnings to the editor Message Log ( Window > Developer
	Tools > Message Log > Inkpot ), where they persist and can be reviewed after Play.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Diagnostics")
	bool bStoryErrorsToMessageLog {true};

	/*
	Also show story errors on screen while playing ( GEngine debug messages ).
	*/
	UPROPERTY(config, EditAnywhere, Category = "Diagnostics")
	bool bStoryErrorsOnScreen {false};

	UPROPERTY(config, EditAnywhere, Category = "Diagnostics", meta = (EditCondition = "bStoryErrorsOnScreen", ClampMin = "1.0"))
	float StoryErrorOnScreenSeconds {8.0f};

	/*
	Prefix story log messages with the story's Prosetta provenance ( project id, channel,
	volume, revision, generation and build hash ) when the story asset carries it.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Diagnostics")
	bool bIncludeProsettaProvenanceInLogs {true};

	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

};

