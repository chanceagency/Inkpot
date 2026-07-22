#include "Inkpot/InkpotStoryFactory.h"
#include "Inkpot/InkpotStoryInternal.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/Prosetta/ProsettaMetadata.h"
#include "Asset/InkpotStoryAsset.h"
#include "Utility/InkpotLog.h"

namespace
{
	// Builds the Prosetta line metadata from the asset's sidecar JSON ( if any ) and
	// attaches it to the story so <prosetta> tags can be hydrated during playback.
	void AttachProsettaMetadata(UInkpotStoryAsset* InAsset, UInkpotStory* InStory)
	{
		if (!InAsset || !InStory)
			return;
		const FString& prosettaJSON = InAsset->GetProsettaJSON();
		if (prosettaJSON.IsEmpty())
			return;
		UProsettaMetadata* metadata = NewObject<UProsettaMetadata>(InStory);
		if (metadata->LoadFromJSON(prosettaJSON))
			InStory->SetProsettaMetadata(metadata);
	}
}

static FString BadInkJSON{ TEXT("{\"inkVersion\":21,\"root\":[[\"^This is BAD Ink. If you see this your Ink did not import correctly.\",\"\n\",\"end\",[\"done\",{\"#n\":\"g-0\"}],null],\"done\",null],\"listDefs\":{}}") };

void UInkpotStoryFactoryBase::Initialise()
{
	ABadStory = NewObject<UInkpotStory>(this);
	ABadStory->Initialise( MakeShared<FInkpotStoryInternal>(BadInkJSON, FInkpotStoryInternal::BadStoryHandle) );
}

TSharedPtr<FInkpotStoryInternal> UInkpotStoryFactoryBase::CreateInternalStory(UInkpotStoryAsset* InInkpotStoryAsset, int32 InHandle)
{
	TSharedPtr<FInkpotStoryInternal> storyInternal;
	if (InInkpotStoryAsset)
	{
		const FString& json = InInkpotStoryAsset->GetCompiledJSON();
		storyInternal = MakeShared<FInkpotStoryInternal>(json, InHandle);
		if (!storyInternal->IsValidStory())
		{
			INKPOT_ERROR("Story asset is not a valid Ink story.");
		}
	}
	else
	{
		INKPOT_ERROR("No story asset.");
	}
	return storyInternal;
}

UInkpotStory* UInkpotStoryFactoryBase::BadStory()
{
	return ABadStory;
}

void UInkpotStoryFactoryBase::Reset()
{
#if WITH_EDITOR
	ABadStory->OnDebugRefresh().Clear();
#endif 
}

UInkpotStory* UInkpotStoryFactory::CreateStory(UInkpotStoryAsset* InInkpotStoryAsset, int32 InHandle, UObject *InOwner )
{ 
	TSharedPtr<FInkpotStoryInternal> storyInternal = CreateInternalStory( InInkpotStoryAsset, InHandle );
	if (!storyInternal.IsValid() || !storyInternal->IsValidStory())
	{
		return ABadStory;
	}
	else
	{
		UInkpotStory* storyNew = NewObject<UInkpotStory>(InOwner);
		storyNew->Initialise(storyInternal);
		AttachProsettaMetadata(InInkpotStoryAsset, storyNew);
		return storyNew;
	}
}
