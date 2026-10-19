#pragma once
#include "CoreMinimal.h"

// Editor-only. The Python that runs the Prosetta scripts: PROSETTA_PYTHON when set, else Unreal's
// bundled interpreter. False with OutError when that interpreter is missing.
PROSETTADASHBOARD_API bool FindProsettaPython(FString& OutPython, FString& OutError);

// Editor-only. Where a Prosetta helper script (prosetta.py, ffmpeg.py) lives: <Project>/Scripts/<Name> when
// the project ships its own copy, else the Inkpot plugin's Scripts/<Name>. Empty when neither exists.
PROSETTADASHBOARD_API FString FindProsettaScript(const FString& Name);

// The places FindProsettaScript looks, in order, for messages that explain a missing script.
PROSETTADASHBOARD_API TArray<FString> ProsettaScriptCandidates(const FString& Name);

// A script path for messages and terminal hints: project-relative when it is inside the project.
PROSETTADASHBOARD_API FString DisplayProsettaScriptPath(const FString& Path);
