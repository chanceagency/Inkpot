#include "ProsettaPython.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

bool FindProsettaPython(FString& OutPython, FString& OutError)
{
#if PLATFORM_MAC
	const TCHAR* Bundled = TEXT("Binaries/ThirdParty/Python3/Mac/bin/python3");
#elif PLATFORM_WINDOWS
	const TCHAR* Bundled = TEXT("Binaries/ThirdParty/Python3/Win64/python.exe");
#elif PLATFORM_LINUX
	const TCHAR* Bundled = TEXT("Binaries/ThirdParty/Python3/Linux/bin/python3");
#else
	const TCHAR* Bundled = nullptr;
#endif
	const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("PROSETTA_PYTHON"));
	if (!Override.IsEmpty())
	{
		OutPython = FPaths::ConvertRelativePathToFull(Override);
		if (FPaths::FileExists(OutPython)) { return true; }
		OutError = TEXT("PROSETTA_PYTHON does not name an existing Python executable.");
		return false;
	}
	if (!Bundled)
	{
		OutError = TEXT("Unreal bundles no Python for this editor platform; set PROSETTA_PYTHON.");
		return false;
	}
	OutPython = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(), Bundled));
	if (FPaths::FileExists(OutPython)) { return true; }
	OutError = TEXT("Unreal's bundled Python (Engine/Binaries/ThirdParty/Python3) is missing; restore it or set PROSETTA_PYTHON.");
	return false;
}

TArray<FString> ProsettaScriptCandidates(const FString& Name)
{
	TArray<FString> Candidates;
	Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts"), Name)));
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Inkpot")))
	{
		Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Scripts"), Name)));
	}
	return Candidates;
}

FString FindProsettaScript(const FString& Name)
{
	for (const FString& Candidate : ProsettaScriptCandidates(Name))
	{
		if (FPaths::FileExists(Candidate)) { return Candidate; }
	}
	return FString();
}

FString DisplayProsettaScriptPath(const FString& Path)
{
	FString Relative = Path;
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (FPaths::MakePathRelativeTo(Relative, *Project) && !Relative.StartsWith(TEXT("..")))
	{
		return Relative;
	}
	return Path;
}
