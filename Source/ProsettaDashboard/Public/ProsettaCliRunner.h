#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"

class FJsonObject;
struct FProsettaSyncProfile;

/** One wrapper invocation: `python3 -u prosetta.py <Command> <Args...> [--base-url …] [--project-id …] [--token-stdin]`. */
struct FProsettaCliRequest
{
	FString Command;
	TArray<FString> Args;
	/** Session overrides; the wrapper turns them into PROSETTA_BASE_URL / PROSETTA_PROJECT_ID, never config. */
	FString BaseUrl;
	FString ProjectId;
	/** Written to the process's stdin as one line and wiped; never part of the command line. Init only. */
	FString StdinSecret;
	float TimeoutSeconds = 120.f;
	/** Keep every non-JSON stdout line (redacted) in TextLines; off keeps JSON objects and notices only. */
	bool bCaptureText = false;
	/** Fail the request when the Python or script path is missing instead of trying to launch. */
	FString Describe() const;
};

struct FProsettaCliResult
{
	bool bLaunched = false;
	bool bCancelled = false;
	bool bTimedOut = false;
	int32 ExitCode = -1;
	/** Parsed stdout lines that were JSON objects, in order. */
	TArray<TSharedPtr<FJsonObject>> JsonObjects;
	TArray<FString> JsonLines;
	/** Whitelisted, redacted wrapper/CLI diagnostics (usage errors, wrapper failures). */
	TArray<FString> Notices;
	/** Redacted plain stdout when bCaptureText was set. */
	TArray<FString> TextLines;
	/** What to tell the user when the run did not succeed. */
	FString UserError;
	bool Succeeded() const { return bLaunched && !bCancelled && !bTimedOut && ExitCode == 0; }
	TSharedPtr<FJsonObject> LastObject() const { return JsonObjects.IsEmpty() ? nullptr : JsonObjects.Last(); }
};

DECLARE_DELEGATE_OneParam(FProsettaCliCompleted, const FProsettaCliResult&);
DECLARE_DELEGATE_OneParam(FProsettaCliLine, const TSharedPtr<FJsonObject>&);

/**
 * Runs prosetta.py wrapper commands (FindProsettaScript: the project's Scripts or the plugin's) for the editor, one at a time in order, without blocking the game
 * thread: stdout is drained from a 0.1 s ticker and the completion delegate fires on the game thread.
 * Only JSON object lines and whitelisted notices are retained; anything else from the CLI may echo a
 * credential and is dropped. One redacted summary line is logged per run.
 */
class PROSETTADASHBOARD_API FProsettaCliRunner
{
public:
	FProsettaCliRunner(FString Python, FString Script, FString WorkingDir);
	virtual ~FProsettaCliRunner();
	UE_NONCOPYABLE(FProsettaCliRunner);

	/** Queues a run; returns its id. OnLine receives each JSON object as it streams in. */
	int32 Enqueue(FProsettaCliRequest Request, FProsettaCliCompleted OnComplete, FProsettaCliLine OnLine = FProsettaCliLine());
	/** Cancels a queued or running request (its delegate fires with bCancelled). */
	void Cancel(int32 RequestId);
	void CancelAll();
	bool IsBusy() const { return Active.IsValid(); }
	int32 NumPending() const { return Queue.Num() + (Active.IsValid() ? 1 : 0); }
	/** The id of the running request, or INDEX_NONE. */
	int32 ActiveRequestId() const;
	const FString& GetPython() const { return Python; }
	const FString& GetScript() const { return Script; }

	/** The argv after the interpreter; never contains StdinSecret. */
	static FString BuildCommandLine(const FString& Script, const FProsettaCliRequest& Request);
	/** Lines the result keeps verbatim (after redaction): wrapper failures, usage and CLI errors. */
	static bool IsNoticeLine(const FString& Line);
	/** Fills UserError from the exit code, notices and the last JSON object. */
	static void DescribeFailure(const FProsettaCliRequest& Request, FProsettaCliResult& Result);

	// Typed helpers. Each returns the request id.
	int32 Credentials(FProsettaCliCompleted OnComplete);
	int32 Whoami(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete);
	int32 Volumes(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete);
	int32 Version(FProsettaCliCompleted OnComplete);
	int32 Verify(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete);
	virtual int32 Sync(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete, FProsettaCliLine OnLine = FProsettaCliLine());
	/** `config set volumes <scope>` (or `config clear volumes` when empty). Local config only; no remote write. */
	virtual int32 ConfigSetVolumes(const FString& Scope, FProsettaCliCompleted OnComplete);
	/** `init --base-url … --project-id … --token-stdin`: writes the ignored local config. The secret is wiped after launch. */
	int32 Init(const FString& BaseUrl, const FString& ProjectId, FString Secret, FProsettaCliCompleted OnComplete);

private:
	struct FPending
	{
		int32 Id;
		FProsettaCliRequest Request;
		FProsettaCliCompleted OnComplete;
		FProsettaCliLine OnLine;
	};
	struct FActive
	{
		FPending Pending;
		FProcHandle Process;
		void* StdoutRead = nullptr;
		void* StdoutWrite = nullptr;
		FString Buffer;
		double StartedAt = 0;
		FProsettaCliResult Result;
		bool bCancelRequested = false;
	};
	bool Tick(float);
	void StartNext();
	void ConsumeLine(FActive& Run, const FString& Line);
	void Finish(FProsettaCliResult Result);
	void CloseActive();

	FString Python;
	FString Script;
	FString WorkingDir;
	TArray<FPending> Queue;
	TUniquePtr<FActive> Active;
	int32 NextId = 1;
	FTSTicker::FDelegateHandle TickHandle;
};
