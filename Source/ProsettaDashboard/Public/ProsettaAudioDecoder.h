#pragma once
#include "CoreMinimal.h"

// Editor-only. Empty override acquires a checksum-pinned project-local decoder; bOffline accepts only the
// verified cache and never downloads. On failure OutError explains why; the caller decides how loudly to report it.
PROSETTADASHBOARD_API bool ResolveProsettaAudioDecoder(const FString& Override, FString& OutExecutable, FString& OutError, bool bOffline = false);

// A background run of ffmpeg.py that downloads the pinned decoder into the verified cache.
class IProsettaDecoderFetch
{
public:
	virtual ~IProsettaDecoderFetch() = default;
	// False while the fetch runs; then true with its exit code and output (the reason when it failed).
	virtual bool Finished(int32& OutExitCode, FString& OutOutput) = 0;
};

// Editor-only. Starts the pinned download as a detached process; null with OutError when it cannot start.
PROSETTADASHBOARD_API TUniquePtr<IProsettaDecoderFetch> StartProsettaAudioDecoderFetch(FString& OutError);
