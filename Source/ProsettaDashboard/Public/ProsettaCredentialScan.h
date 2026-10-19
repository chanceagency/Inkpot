#pragma once
#include "CoreMinimal.h"

/** One place a token may live. Only existence and permissions are inspected; the file is never read. */
struct FProsettaCredentialCandidate
{
	/** env, local, home or native. */
	FString Kind;
	FString Path;
	bool bExists = false;
	/** True when the file is readable by group or others (a token file should be 0600). */
	bool bWorldReadable = false;
	FString Warning;
};

struct FProsettaCredentialScan
{
	/** The first candidate that exists, in CLI precedence: env, local, home, native; "none" otherwise. */
	FString Source = TEXT("none");
	TArray<FProsettaCredentialCandidate> Candidates;
	bool HasAny() const { return Source != TEXT("none"); }
	FString Describe() const;
};

/**
 * Offline, read-free scan of where the CLI would find credentials, mirroring prosetta.py's precedence
 * (PROSETTA_PROJECT_TOKEN, .prosetta-sync/config.json, the legacy Saved config, ~/.prosetta-sync/config.json,
 * the native CLI user config). Used for the first-run checklist before the wrapper has been run.
 */
PROSETTADASHBOARD_API FProsettaCredentialScan ScanProsettaCredentialSources(const FString& ProjectDir, const FString& HomeDir, const TMap<FString, FString>& Env);
/** Same, from the running process's environment and this project. */
PROSETTADASHBOARD_API FProsettaCredentialScan ScanProsettaCredentialSources();
