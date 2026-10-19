#pragma once
#include "CoreMinimal.h"

/**
 * Editor-owned watch transport that the in-game debug menu shares during PIE. The editor module
 * installs one for its lifetime; runtime builds leave it null and keep their standalone CLI fallback.
 */
class INKPOT_API IProsettaEditorWatch
{
public:
	virtual ~IProsettaEditorWatch() = default;
	virtual void Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsRunning() const = 0;
	virtual FString Status() const = 0;

	/** The installed watch, or null when no editor owns one. */
	static IProsettaEditorWatch* Installed;
};
