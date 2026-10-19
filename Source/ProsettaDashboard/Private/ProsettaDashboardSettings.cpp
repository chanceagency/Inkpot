#include "ProsettaDashboardSettings.h"
#include "Inkpot/Prosetta/ProsettaStorySourceActor.h"
#include "Misc/Paths.h"

UProsettaSyncProjectSettings::UProsettaSyncProjectSettings()
{
	ProfilesDirectory.Path = TEXT("Config/ProsettaSync/Profiles");
	StorySourceHostClass = AProsettaStorySourceActor::StaticClass();
}

FString UProsettaSyncProjectSettings::ResolveProfilesDirectory() const
{
	const FString Path = ProfilesDirectory.Path.IsEmpty() ? FString(TEXT("Config/ProsettaSync/Profiles")) : ProfilesDirectory.Path;
	return FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Path) ? FPaths::Combine(FPaths::ProjectDir(), Path) : Path);
}

FString FProsettaRecentConnection::Describe() const
{
	const FString Project = ProjectName.IsEmpty() ? FString::Printf(TEXT("project %s"), *ProjectId) : ProjectName;
	return FString::Printf(TEXT("%s (%s)"), *Project, *BaseUrl);
}

void UProsettaDashboardUserSettings::PushRecentConnection(const FString& BaseUrl, const FString& ProjectId, const FString& ProjectName, const FDateTime& Now)
{
	RecentConnections.RemoveAll([&](const FProsettaRecentConnection& C) { return C.SameTarget(BaseUrl, ProjectId); });
	FProsettaRecentConnection Entry;
	Entry.BaseUrl = BaseUrl;
	Entry.ProjectId = ProjectId;
	Entry.ProjectName = ProjectName;
	Entry.LastUsed = Now;
	RecentConnections.Insert(MoveTemp(Entry), 0);
	if (RecentConnections.Num() > MaxRecentConnections) { RecentConnections.SetNum(MaxRecentConnections); }
}

UProsettaDashboardUserSettings* UProsettaDashboardUserSettings::Get()
{
	return GetMutableDefault<UProsettaDashboardUserSettings>();
}
