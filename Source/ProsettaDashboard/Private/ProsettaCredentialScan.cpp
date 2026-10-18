#include "ProsettaCredentialScan.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/stat.h>
#endif

namespace
{
	bool IsWorldReadable(const FString& Path)
	{
#if PLATFORM_MAC || PLATFORM_LINUX
		struct stat Info;
		if (stat(TCHAR_TO_UTF8(*Path), &Info) == 0) { return (Info.st_mode & (S_IRGRP | S_IROTH)) != 0; }
#endif
		return false;
	}

	FProsettaCredentialCandidate FileCandidate(const TCHAR* Kind, const FString& Path)
	{
		FProsettaCredentialCandidate C;
		C.Kind = Kind;
		C.Path = Path;
		C.bExists = FPaths::FileExists(Path);
		if (C.bExists && IsWorldReadable(Path))
		{
			C.bWorldReadable = true;
			C.Warning = TEXT("readable by other users; chmod 600 it");
		}
		return C;
	}
}

FString FProsettaCredentialScan::Describe() const
{
	if (Source == TEXT("env")) { return TEXT("PROSETTA_PROJECT_TOKEN in the editor's environment"); }
	for (const FProsettaCredentialCandidate& C : Candidates)
	{
		if (C.Kind == Source && C.bExists) { return C.Path; }
	}
	return TEXT("no credentials found");
}

FProsettaCredentialScan ScanProsettaCredentialSources(const FString& ProjectDir, const FString& HomeDir, const TMap<FString, FString>& Env)
{
	FProsettaCredentialScan Scan;
	FProsettaCredentialCandidate EnvCandidate;
	EnvCandidate.Kind = TEXT("env");
	EnvCandidate.Path = TEXT("PROSETTA_PROJECT_TOKEN");
	const FString* Token = Env.Find(TEXT("PROSETTA_PROJECT_TOKEN"));
	EnvCandidate.bExists = Token && !Token->IsEmpty();
	Scan.Candidates.Add(EnvCandidate);
	Scan.Candidates.Add(FileCandidate(TEXT("local"), FPaths::Combine(ProjectDir, TEXT(".prosetta-sync"), TEXT("config.json"))));
	Scan.Candidates.Add(FileCandidate(TEXT("local"), FPaths::Combine(ProjectDir, TEXT("Saved"), TEXT("ProsettaSync"), TEXT("prosetta-sync.config.json"))));
	Scan.Candidates.Add(FileCandidate(TEXT("home"), FPaths::Combine(HomeDir, TEXT(".prosetta-sync"), TEXT("config.json"))));
	const FString* Xdg = Env.Find(TEXT("XDG_CONFIG_HOME"));
	const FString ConfigHome = Xdg && !Xdg->IsEmpty() ? *Xdg : FPaths::Combine(HomeDir, TEXT(".config"));
	Scan.Candidates.Add(FileCandidate(TEXT("native"), FPaths::Combine(ConfigHome, TEXT("Prosetta"), TEXT("SyncTool"), TEXT("config.json"))));
	for (const FProsettaCredentialCandidate& C : Scan.Candidates)
	{
		if (C.bExists) { Scan.Source = C.Kind; break; }
	}
	return Scan;
}

FProsettaCredentialScan ScanProsettaCredentialSources()
{
	TMap<FString, FString> Env;
	for (const TCHAR* Key : {TEXT("PROSETTA_PROJECT_TOKEN"), TEXT("XDG_CONFIG_HOME")})
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Key);
		if (!Value.IsEmpty()) { Env.Add(Key, Value); }
	}
	return ScanProsettaCredentialSources(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), FPlatformProcess::UserHomeDir(), Env);
}
