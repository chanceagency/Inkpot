#include "Settings/InkpotSettings.h"
#include "Inkpot/Inkpot.h"
#include "Utility/InkpotLog.h"
#include "Misc/ConfigCacheIni.h"

bool UInkpotSettings::ApplyLegacyStoryErrorConfig(const FString& Section, const FString& ConfigFilename)
{
	if (!GConfig)
		return false;

	bool bLogStoryErrors = true;
	if (!GConfig->GetBool(*Section, TEXT("bLogStoryErrors"), bLogStoryErrors, ConfigFilename))
		return false;

	if (!bLogStoryErrors)
	{
		FString explicitValue;
		if (!GConfig->GetString(*Section, GET_MEMBER_NAME_STRING_CHECKED(UInkpotSettings, StoryErrorAction), explicitValue, ConfigFilename))
			StoryErrorAction = EInkpotStoryErrorAction::BroadcastOnly;
		if (!GConfig->GetString(*Section, GET_MEMBER_NAME_STRING_CHECKED(UInkpotSettings, StoryWarningAction), explicitValue, ConfigFilename))
			StoryWarningAction = EInkpotStoryErrorAction::BroadcastOnly;
	}
	return true;
}

void UInkpotSettings::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject))
		return;

	const FString section = GetClass()->GetPathName();
	if (ApplyLegacyStoryErrorConfig(section, GetClass()->GetConfigName()))
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(Inkpot, Warning, TEXT("[%s] bLogStoryErrors is deprecated and will be ignored in a future version; replace it with StoryErrorAction / StoryWarningAction ( BroadcastOnly, Log, Ensure or Fatal ). Migrated as StoryErrorAction=%s, StoryWarningAction=%s."),
				*section, *StaticEnum<EInkpotStoryErrorAction>()->GetNameStringByValue((int64)StoryErrorAction), *StaticEnum<EInkpotStoryErrorAction>()->GetNameStringByValue((int64)StoryWarningAction));
		}
	}
}

FName UInkpotSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UInkpotSettings::GetSectionText() const
{
	

	return NSLOCTEXT("InkpotPlugin", "InkpotSettingsSection", "Inkpot");
}
#endif


#if WITH_EDITOR
void UInkpotSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	const FName propertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	
	if (propertyName == GET_MEMBER_NAME_CHECKED(UInkpotSettings, StoryFactoryClass))
	{
		UInkpot *inkpot = GEngine->GetEngineSubsystem<UInkpot>();
		inkpot->InitializeStoryFactory();
	}
}
#endif
