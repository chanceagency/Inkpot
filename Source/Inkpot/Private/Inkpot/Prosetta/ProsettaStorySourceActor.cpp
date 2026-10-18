#include "Inkpot/Prosetta/ProsettaStorySourceActor.h"

bool AProsettaStorySourceActor::SetProsettaStorySource(const FProsettaStorySource& NewSource)
{
	if (StorySource == NewSource) { return false; }
	Modify();
	StorySource = NewSource;
#if WITH_EDITOR
	FProperty* Property = FindFProperty<FProperty>(StaticClass(), GET_MEMBER_NAME_CHECKED(AProsettaStorySourceActor, StorySource));
	FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
	PostEditChangeProperty(Event);
#endif
	MarkPackageDirty();
	return true;
}
