#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "ProsettaAudioDecoder.h"
#include "Inkpot/Prosetta/ProsettaEditorWatch.h"

class IConsoleObject;
class FProsettaReceiptWatch;
class UProsettaWatchSubsystem;

/**
 * The decoder resolver the project watch hands to the importer. It never waits on the network: it accepts
 * only the verified FFmpeg cache, and on a miss starts one background fetch of the pinned decoder and fails
 * the generation, which keeps the previous one. When the fetch succeeds, Tick offers the receipts again,
 * past the transport's de-dup, so that generation retries; the importer ignores ones it already applied.
 * Game thread only.
 */
class PROSETTADASHBOARD_API FProsettaWatchDecoder
{
public:
	using FCheckCache = TFunction<bool(FString& OutExecutable, FString& OutError)>;
	using FStartFetch = TFunction<TUniquePtr<IProsettaDecoderFetch>(FString& OutError)>;
	using FImporterGetter = TFunction<UProsettaWatchSubsystem*()>;
	FProsettaWatchDecoder(FString ReceiptDirectory, FImporterGetter GetImporter, FCheckCache CheckCache, FStartFetch StartFetch);
	/** An FProsettaDecoderResolver: the cached decoder, or false with an error naming the background fetch. */
	bool Resolve(FString& OutExecutable, FString& OutError);
	/** When the fetch finished: on success offers the receipts again once, on failure logs the reason once. */
	void Tick();
	bool IsFetching() const { return Fetch.IsValid(); }
private:
	FString ReceiptDirectory;
	FImporterGetter GetImporter;
	FCheckCache CheckCache;
	FStartFetch StartFetch;
	TUniquePtr<IProsettaDecoderFetch> Fetch;
	/** Set by a successful fetch until the cache is found, so a cache that still fails cannot refetch in a loop. */
	bool bFetched = false;
};

/**
 * Editor-owned processes asked to stop. The wrapper stops the CLI it started before exiting, which can take
 * its grace period, so they are drained and reaped from the ticker rather than waited for on the game
 * thread; destruction (editor shutdown) waits for them. Game thread only.
 */
class PROSETTADASHBOARD_API FProsettaStoppingProcesses
{
public:
	FProsettaStoppingProcesses() = default;
	UE_NONCOPYABLE(FProsettaStoppingProcesses);
	~FProsettaStoppingProcesses() { Reap(true); }
	/** Sends SIGTERM if Process is still running, and takes ownership of it and its pipes. */
	void Stop(FProcHandle Process, void* ReadPipe, void* WritePipe);
	/** Closes the processes that have exited; with bWait, waits for every one first. */
	void Reap(bool bWait);
	int32 Num() const { return Entries.Num(); }
private:
	struct FEntry { FProcHandle Process; void* ReadPipe; void* WritePipe; };
	TArray<FEntry> Entries;
};

/** Hands each complete typed status line in Buffer to Forward and keeps the rest; with bFlush, the
 *  unterminated tail too (a CLI's fatal --json result before it exits). */
PROSETTADASHBOARD_API void ForwardProsettaWatchStatusLines(FString& Buffer, bool bFlush, TFunctionRef<void(const FString&)> Forward);

/**
 * The editor's project watch. Completed-generation receipts in Saved/ProsettaSync/Watch, written by
 * the terminal wrapper or by the CLI this class starts, reach the plugin's verified, deferred importer
 * through the plugin's FProsettaReceiptWatch. This class adds what is specific to this checkout: the
 * pinned FFmpeg resolver (FProsettaWatchDecoder), the editor-owned prosetta.py wrapper process and its
 * status lines, and the Prosetta.Watch.Project.* commands. Installed as IProsettaEditorWatch so the
 * in-game debug menu shares it during PIE.
 */
/** What the editor-owned `prosetta.py watch` is started with. Built from the active sync profile; no credentials. */
struct FProsettaWatchOptions
{
	/** "live" or "latest-release". */
	FString Channel = TEXT("live");
	/** `--volumes` scope ("core,side@latest"); empty follows the CLI config. */
	FString Volumes;
	bool bNoMirror = false;
	/** Session overrides handed to the wrapper as --base-url / --project-id (environment, never config). */
	FString BaseUrl;
	FString ProjectId;
	/** FFmpeg override for the importer; empty uses the verified cache. */
	FString AudioDecoderPath;
	bool operator==(const FProsettaWatchOptions& Other) const
	{
		return Channel == Other.Channel && Volumes == Other.Volumes && bNoMirror == Other.bNoMirror && BaseUrl == Other.BaseUrl && ProjectId == Other.ProjectId && AudioDecoderPath == Other.AudioDecoderPath;
	}
	bool operator!=(const FProsettaWatchOptions& Other) const { return !(*this == Other); }
};

class PROSETTADASHBOARD_API FProsettaProjectWatch : public IProsettaEditorWatch
{
public:
	FProsettaProjectWatch();
	virtual ~FProsettaProjectWatch() override;
	/** Starts with OptionsProvider's options (the defaults when none is set). */
	virtual void Start() override;
	void Start(const FProsettaWatchOptions& Options);
	/** The wrapper arguments after the script: `watch --channel … [--volumes …] [--no-mirror] [--base-url …] [--project-id …]`. */
	static TArray<FString> BuildWatchArguments(const FProsettaWatchOptions& Options);
	/** Supplies the options for a parameterless Start (console command, auto-start, debug menu). */
	TFunction<FProsettaWatchOptions()> OptionsProvider;
	/** The options the running CLI was started with. */
	const FProsettaWatchOptions& GetActiveOptions() const { return ActiveOptions; }
	/** Last exit code of an editor-owned CLI that stopped by itself, or INDEX_NONE. */
	int32 LastExitCode() const { return LastExit; }
	virtual void Stop() override;
	virtual bool IsRunning() const override;
	virtual FString Status() const override;
private:
	bool Tick(float DeltaTime);
	void ConsumeOutput(bool bFlush = false);
	void CloseTransport();
	TSharedPtr<FProsettaWatchDecoder> Decoder;
	/** Read by the decoder's cache check; set from the options at Start. */
	TSharedPtr<FString> DecoderOverride;
	TUniquePtr<FProsettaReceiptWatch> Receipts;
	FProsettaWatchOptions ActiveOptions;
	int32 LastExit = INDEX_NONE;
	/** Transport facts only (watcher, editor-owned CLI); the importer reports its own status. */
	FString TransportStatus;
	FString OutputBuffer;
	mutable FProcHandle Process;
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	FProsettaStoppingProcesses Stopping;
	FTSTicker::FDelegateHandle TickHandle;
	TArray<IConsoleObject*> Commands;
};
