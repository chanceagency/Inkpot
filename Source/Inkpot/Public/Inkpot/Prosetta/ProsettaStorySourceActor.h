#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Inkpot/Prosetta/ProsettaStorySource.h"
#include "ProsettaStorySourceActor.generated.h"

/**
 * Minimal level-wide story source host: one per level, holding the FProsettaStorySource that
 * ProsettaStorySource::ForWorld returns. The Prosetta Dashboard spawns it when a level without a host
 * is re-pointed at a volume and the project names no other host class. Projects that already have a
 * level setup actor implement IProsettaStorySourceHost on it instead.
 */
UCLASS(Blueprintable, meta=(DisplayName="Prosetta Story Source"))
class INKPOT_API AProsettaStorySourceActor : public AInfo, public IProsettaStorySourceHost
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Prosetta|Story")
	FProsettaStorySource StorySource;

	virtual FProsettaStorySource GetProsettaStorySource() const override { return StorySource; }
	virtual bool SetProsettaStorySource(const FProsettaStorySource& NewSource) override;
};
