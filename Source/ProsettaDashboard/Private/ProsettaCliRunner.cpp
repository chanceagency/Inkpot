#include "ProsettaCliRunner.h"
#include "Inkpot/Prosetta/ProsettaSyncInspector.h"
#include "ProsettaSyncProfile.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaCli, Log, All);

namespace
{
	/** Overwrites and drops the init token; every path that releases a request calls it. */
	void WipeSecret(FProsettaCliRequest& Request)
	{
		for (TCHAR& C : Request.StdinSecret) { C = TEXT('*'); }
		Request.StdinSecret.Empty();
	}
}

FString FProsettaCliRequest::Describe() const
{
	return Command + (Args.IsEmpty() ? FString() : TEXT(" ") + FString::Join(Args, TEXT(" ")));
}

FProsettaCliRunner::FProsettaCliRunner(FString InPython, FString InScript, FString InWorkingDir)
	: Python(MoveTemp(InPython)), Script(MoveTemp(InScript)), WorkingDir(MoveTemp(InWorkingDir))
{
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProsettaCliRunner::Tick), 0.1f);
}

FProsettaCliRunner::~FProsettaCliRunner()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	for (FPending& Pending : Queue) { WipeSecret(Pending.Request); }
	Queue.Empty();
	if (Active)
	{
		if (FPlatformProcess::IsProcRunning(Active->Process)) { FPlatformProcess::TerminateProc(Active->Process, true); }
		FPlatformProcess::WaitForProc(Active->Process);
		CloseActive();
	}
}

int32 FProsettaCliRunner::ActiveRequestId() const
{
	return Active ? Active->Pending.Id : INDEX_NONE;
}

FString FProsettaCliRunner::BuildCommandLine(const FString& InScript, const FProsettaCliRequest& Request)
{
	TArray<FString> Argv = {TEXT("-u"), FString::Printf(TEXT("\"%s\""), *InScript), Request.Command};
	auto Quote = [](const FString& Arg)
	{
		return Arg.Contains(TEXT(" ")) || Arg.IsEmpty() ? FString::Printf(TEXT("\"%s\""), *Arg.Replace(TEXT("\""), TEXT("\\\""))) : Arg;
	};
	for (const FString& Arg : Request.Args) { Argv.Add(Quote(Arg)); }
	if (!Request.BaseUrl.IsEmpty()) { Argv.Append({TEXT("--base-url"), Quote(Request.BaseUrl)}); }
	if (!Request.ProjectId.IsEmpty()) { Argv.Append({TEXT("--project-id"), Quote(Request.ProjectId)}); }
	if (!Request.StdinSecret.IsEmpty()) { Argv.Add(TEXT("--token-stdin")); }
	return FString::Join(Argv, TEXT(" "));
}

bool FProsettaCliRunner::IsNoticeLine(const FString& Line)
{
	static const TCHAR* Prefixes[] = {TEXT("Prosetta wrapper failed:"), TEXT("Usage:"), TEXT("usage:"), TEXT("error:"), TEXT("Error:"), TEXT("warning:"), TEXT("Warning:"), TEXT("Watch no longer polls")};
	for (const TCHAR* Prefix : Prefixes)
	{
		if (Line.StartsWith(Prefix)) { return true; }
	}
	return false;
}

void FProsettaCliRunner::DescribeFailure(const FProsettaCliRequest& Request, FProsettaCliResult& Result)
{
	if (Result.Succeeded() && Request.bRequiresJsonObject && Result.JsonObjects.IsEmpty())
	{
		Result.bMissingJsonObject = true;
		Result.UserError = Result.Notices.IsEmpty() ? FString(TEXT("CLI returned no JSON object.")) : Result.Notices.Last();
		return;
	}
	if (Result.Succeeded()) { Result.UserError.Empty(); return; }
	if (!Result.bLaunched) { if (Result.UserError.IsEmpty()) { Result.UserError = TEXT("The Prosetta wrapper could not be launched."); } return; }
	if (Result.bCancelled) { Result.UserError = TEXT("Cancelled."); return; }
	if (Result.bTimedOut) { Result.UserError = FString::Printf(TEXT("`%s` did not finish within %.0f s and was stopped."), *Request.Describe(), Request.TimeoutSeconds); return; }
	FString Message;
	if (const TSharedPtr<FJsonObject> Last = Result.LastObject())
	{
		if (!Last->TryGetStringField(TEXT("message"), Message)) { Last->TryGetStringField(TEXT("error"), Message); }
	}
	const FString Notice = Result.Notices.IsEmpty() ? FString() : Result.Notices.Last();
	if (Result.ExitCode == 143 || Result.ExitCode == -15) { Result.UserError = TEXT("Stopped."); Result.bCancelled = true; return; }
	if (Result.ExitCode == 2)
	{
		Result.UserError = Notice.IsEmpty() ? FString::Printf(TEXT("The CLI rejected the arguments of `%s`."), *Request.Describe()) : Notice;
		return;
	}
	if (!Message.IsEmpty()) { Result.UserError = Message; return; }
	if (!Notice.IsEmpty()) { Result.UserError = Notice; return; }
	Result.UserError = FString::Printf(TEXT("`%s` exited with code %d and no diagnostic; run `prosetta.py %s` from the project directory in a terminal to see why."), *Request.Describe(), Result.ExitCode, *Request.Command);
}

int32 FProsettaCliRunner::Enqueue(FProsettaCliRequest Request, FProsettaCliCompleted OnComplete, FProsettaCliLine OnLine)
{
	const int32 Id = NextId++;
	Queue.Add({Id, MoveTemp(Request), MoveTemp(OnComplete), MoveTemp(OnLine)});
	if (!Active) { StartNext(); }
	return Id;
}

void FProsettaCliRunner::Cancel(int32 RequestId)
{
	const int32 Index = Queue.IndexOfByPredicate([RequestId](const FPending& P) { return P.Id == RequestId; });
	if (Index != INDEX_NONE)
	{
		FPending Pending = MoveTemp(Queue[Index]);
		Queue.RemoveAt(Index);
		WipeSecret(Pending.Request);
		FProsettaCliResult Result;
		Result.bCancelled = true;
		Result.UserError = TEXT("Cancelled.");
		Pending.OnComplete.ExecuteIfBound(Result);
		return;
	}
	if (Active && Active->Pending.Id == RequestId && !Active->bCancelRequested)
	{
		Active->bCancelRequested = true;
		if (FPlatformProcess::IsProcRunning(Active->Process)) { FPlatformProcess::TerminateProc(Active->Process, true); }
	}
}

void FProsettaCliRunner::CancelAll()
{
	while (!Queue.IsEmpty()) { Cancel(Queue[0].Id); }
	if (Active) { Cancel(Active->Pending.Id); }
}

void FProsettaCliRunner::StartNext()
{
	while (!Active && !Queue.IsEmpty())
	{
		TUniquePtr<FActive> Run = MakeUnique<FActive>();
		Run->Pending = MoveTemp(Queue[0]);
		Queue.RemoveAt(0);
		FProsettaCliRequest& Request = Run->Pending.Request;
		if (!FPaths::FileExists(Python) || !FPaths::FileExists(Script))
		{
			Run->Result.UserError = !FPaths::FileExists(Script) ? FString::Printf(TEXT("The Prosetta wrapper is missing: %s."), *Script) : FString::Printf(TEXT("Python not found at %s."), *Python);
			WipeSecret(Request);
			Run->Pending.OnComplete.ExecuteIfBound(Run->Result);
			continue;
		}
		const FString CommandLine = BuildCommandLine(Script, Request);
		void* StdinRead = nullptr;
		void* StdinWrite = nullptr;
		if (!FPlatformProcess::CreatePipe(Run->StdoutRead, Run->StdoutWrite))
		{
			Run->Result.UserError = TEXT("Could not create a pipe for the Prosetta wrapper.");
			WipeSecret(Request);
			Run->Pending.OnComplete.ExecuteIfBound(Run->Result);
			continue;
		}
		if (!Request.StdinSecret.IsEmpty() && !FPlatformProcess::CreatePipe(StdinRead, StdinWrite, true))
		{
			FPlatformProcess::ClosePipe(Run->StdoutRead, Run->StdoutWrite);
			Run->Result.UserError = TEXT("Could not create a stdin pipe for the Prosetta wrapper.");
			WipeSecret(Request);
			Run->Pending.OnComplete.ExecuteIfBound(Run->Result);
			continue;
		}
		Run->Process = FPlatformProcess::CreateProc(*Python, *CommandLine, false, true, true, nullptr, 0, *WorkingDir, Run->StdoutWrite, StdinRead);
		if (StdinWrite)
		{
			if (Run->Process.IsValid())
			{
				// Written as bytes we zero afterwards: the FString overload would leave an unwiped UTF-8 copy behind.
				FTCHARToUTF8 Utf8(*Request.StdinSecret, Request.StdinSecret.Len());
				TArray<uint8> Line(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
				Line.Add('\n');
				FPlatformProcess::WritePipe(StdinWrite, Line.GetData(), Line.Num());
				FPlatformMemory::Memzero(Line.GetData(), Line.Num());
				FPlatformMemory::Memzero(const_cast<void*>(static_cast<const void*>(Utf8.Get())), Utf8.Length());
			}
			// The child holds its own copy of the read end; closing ours sends EOF after the one line.
			FPlatformProcess::ClosePipe(StdinRead, StdinWrite);
		}
		// Wiped as soon as it is delivered: it must not sit in memory for the run or reach the result.
		WipeSecret(Request);
		// The command line never carries the secret, so this is the one line logged per run.
		UE_LOG(LogProsettaCli, Display, TEXT("prosetta.py %s"), *Request.Describe());
		if (!Run->Process.IsValid())
		{
			FPlatformProcess::ClosePipe(Run->StdoutRead, Run->StdoutWrite);
			Run->Result.UserError = TEXT("The Prosetta wrapper could not be launched; run prosetta.py from the project directory in a terminal to see why.");
			Run->Pending.OnComplete.ExecuteIfBound(Run->Result);
			continue;
		}
		Run->Result.bLaunched = true;
		Run->StartedAt = FPlatformTime::Seconds();
		Active = MoveTemp(Run);
	}
}

bool FProsettaCliRunner::CompletePendingJson(FActive& Run)
{
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Run.PendingJson), Object) || !Object.IsValid()) { return false; }
	// Keep the CLI's own text: re-serializing would round every number through double.
	Run.Result.JsonLines.Add(MoveTemp(Run.PendingJson));
	Run.PendingJson.Empty();
	Run.Result.JsonObjects.Add(Object);
	Run.Pending.OnLine.ExecuteIfBound(Object);
	return true;
}

void FProsettaCliRunner::DiscardPendingJson(FActive& Run)
{
	if (Run.PendingJson.IsEmpty()) { return; }
	Run.PendingJson.Empty();
	Run.Result.Notices.Add(TEXT("Error: CLI returned incomplete or malformed JSON."));
}

void FProsettaCliRunner::ConsumeLine(FActive& Run, const FString& RawLine)
{
	const FString Line = RawLine.TrimStartAndEnd();
	if (Line.IsEmpty()) { return; }
	// A top-level object opens and closes at column 0; everything between is indented. A new object
	// therefore abandons an unparseable fragment, and only a closing line is worth a full parse.
	const bool bOpens = RawLine.StartsWith(TEXT("{"));
	const bool bCloses = RawLine.StartsWith(TEXT("}"));
	if (bOpens) { DiscardPendingJson(Run); Run.bDiscardingJson = false; }
	if (Run.bDiscardingJson && (bCloses || FChar::IsWhitespace(RawLine[0]))) { Run.bDiscardingJson = !bCloses; return; }
	// stderr shares the pipe: any other column-0 line inside a pretty-printed object is interleaved text.
	const bool bInterleaved = !Run.PendingJson.IsEmpty() && !bCloses && !FChar::IsWhitespace(RawLine[0]);
	if (!bInterleaved && (!Run.PendingJson.IsEmpty() || Line.StartsWith(TEXT("{"))))
	{
		// One-shot CLI commands pretty-print JSON; watch emits one compact object per line. A JSON string
		// cannot span lines, so joining trimmed lines keeps the text valid and on one line.
		const bool bFirst = Run.PendingJson.IsEmpty();
		if (!bFirst) { Run.PendingJson += TEXT(' '); }
		Run.PendingJson += Line;
		// Bound unexpected output without retaining or logging raw potentially sensitive text. A complete
		// compact line has nothing left to discard.
		if (int64(Run.PendingJson.Len()) * sizeof(TCHAR) > MaxOutputBytes)
		{
			Run.PendingJson.Empty();
			Run.bDiscardingJson = !bFirst && !bCloses;
			Run.Result.Notices.Add(TEXT("Error: CLI JSON output exceeded the 16 MiB limit."));
			return;
		}
		if (bFirst || bCloses) { CompletePendingJson(Run); }
		return;
	}
	const FString Redacted = UProsettaSyncInspector::RedactCredentialLine(Line);
	if (IsNoticeLine(Line))
	{
		Run.Result.Notices.Add(Redacted);
		for (const TCHAR* Mark : { TEXT("INVALID_OR_EXPIRED_TOKEN"), TEXT("rejected the token"), TEXT("different project"), TEXT("HTTP 401"), TEXT("HTTP 403"), TEXT("No usable token") })
		{
			if (Line.Contains(Mark, ESearchCase::IgnoreCase)) { Run.Result.bCredentialRejection = true; }
		}
	}
	if (Run.Pending.Request.bCaptureText) { Run.Result.TextLines.Add(Redacted); }
}

void FProsettaCliRunner::ConsumeBufferedOutput(FActive& Run, bool bFlush)
{
	int32 Newline;
	while (Run.Buffer.FindChar(TEXT('\n'), Newline))
	{
		if (Run.bDiscardingLine) { Run.bDiscardingLine = false; }
		else { ConsumeLine(Run, Run.Buffer.Left(Newline)); }
		Run.Buffer.RightChopInline(Newline + 1);
	}
	if (int64(Run.Buffer.Len()) * sizeof(TCHAR) > MaxOutputBytes)
	{
		Run.Buffer.Empty();
		if (!Run.bDiscardingLine) { Run.Result.Notices.Add(TEXT("Error: CLI output line exceeded the 16 MiB limit.")); }
		Run.bDiscardingLine = true;
		// A dropped member line would leave a valid-looking object short of a field.
		if (!Run.PendingJson.IsEmpty()) { Run.PendingJson.Empty(); Run.bDiscardingJson = true; }
	}
	if (bFlush)
	{
		if (!Run.Buffer.IsEmpty() && !Run.bDiscardingLine) { ConsumeLine(Run, Run.Buffer); }
		Run.Buffer.Empty();
		Run.bDiscardingLine = false;
		// An object whose closing brace was indented still gets one complete parse at exit.
		if (!Run.PendingJson.IsEmpty() && !CompletePendingJson(Run)) { DiscardPendingJson(Run); }
		Run.bDiscardingJson = false;
	}
}

bool FProsettaCliRunner::Tick(float)
{
	if (!Active) { StartNext(); return true; }
	FActive& Run = *Active;
	Run.Buffer += FPlatformProcess::ReadPipe(Run.StdoutRead);
	ConsumeBufferedOutput(Run, false);
	const bool bRunning = FPlatformProcess::IsProcRunning(Run.Process);
	if (bRunning && !Run.bCancelRequested && FPlatformTime::Seconds() - Run.StartedAt > Run.Pending.Request.TimeoutSeconds)
	{
		Run.Result.bTimedOut = true;
		FPlatformProcess::TerminateProc(Run.Process, true);
		return true;
	}
	if (bRunning) { return true; }
	Run.Buffer += FPlatformProcess::ReadPipe(Run.StdoutRead);
	ConsumeBufferedOutput(Run, true);
	int32 ExitCode = -1;
	FPlatformProcess::GetProcReturnCode(Run.Process, &ExitCode);
	Run.Result.ExitCode = ExitCode;
	if (Run.bCancelRequested) { Run.Result.bCancelled = true; }
	DescribeFailure(Run.Pending.Request, Run.Result);
	FProsettaCliResult Result = MoveTemp(Run.Result);
	Finish(MoveTemp(Result));
	return true;
}

void FProsettaCliRunner::Finish(FProsettaCliResult Result)
{
	FPending Pending = MoveTemp(Active->Pending);
	CloseActive();
	UE_LOG(LogProsettaCli, Display, TEXT("prosetta.py %s finished: exit %d, %d JSON line(s)%s"), *Pending.Request.Describe(), Result.ExitCode, Result.JsonLines.Num(),
		Result.UserError.IsEmpty() ? TEXT("") : *(TEXT(" - ") + Result.UserError));
	Pending.OnComplete.ExecuteIfBound(Result);
	StartNext();
}

void FProsettaCliRunner::CloseActive()
{
	if (!Active) { return; }
	FPlatformProcess::CloseProc(Active->Process);
	FPlatformProcess::ClosePipe(Active->StdoutRead, Active->StdoutWrite);
	Active.Reset();
}

int32 FProsettaCliRunner::Credentials(FProsettaCliCompleted OnComplete)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("credentials");
	Request.Args = {TEXT("--json")};
	Request.bRequiresJsonObject = true;
	Request.TimeoutSeconds = 30.f;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete));
}

int32 FProsettaCliRunner::Whoami(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("whoami");
	Request.Args = {TEXT("--json")};
	Request.bRequiresJsonObject = true;
	Request.BaseUrl = Profile.BaseUrl;
	Request.ProjectId = Profile.ProjectId;
	Request.TimeoutSeconds = 60.f;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete));
}

int32 FProsettaCliRunner::Volumes(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("volumes");
	Request.Args = {TEXT("--json")};
	Request.bRequiresJsonObject = true;
	Request.BaseUrl = Profile.BaseUrl;
	Request.ProjectId = Profile.ProjectId;
	Request.TimeoutSeconds = 60.f;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete));
}

int32 FProsettaCliRunner::Version(FProsettaCliCompleted OnComplete)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("version");
	Request.bCaptureText = true;
	Request.TimeoutSeconds = 30.f;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete));
}

int32 FProsettaCliRunner::Verify(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("verify");
	Request.Args = {TEXT("--json")};
	Request.BaseUrl = Profile.BaseUrl;
	Request.ProjectId = Profile.ProjectId;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete));
}

int32 FProsettaCliRunner::Sync(const FProsettaSyncProfile& Profile, FProsettaCliCompleted OnComplete, FProsettaCliLine OnLine)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("sync");
	Request.Args = {TEXT("--json")};
	// --repair, --snapshot and the profile's connection overrides.
	Request.Args.Append(Profile.BuildSyncFlags());
	Request.TimeoutSeconds = 600.f;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete), MoveTemp(OnLine));
}

int32 FProsettaCliRunner::ConfigSetVolumes(const FString& Scope, FProsettaCliCompleted OnComplete)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("config");
	Request.Args = Scope.IsEmpty() ? TArray<FString>{TEXT("clear"), TEXT("volumes")} : TArray<FString>{TEXT("set"), TEXT("volumes"), Scope};
	Request.TimeoutSeconds = 30.f;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete));
}

int32 FProsettaCliRunner::Init(const FString& BaseUrl, const FString& ProjectId, FString Secret, FProsettaCliCompleted OnComplete)
{
	FProsettaCliRequest Request;
	Request.Command = TEXT("init");
	// init keeps --base-url/--project-id as its own options (the wrapper only strips them for other commands).
	Request.Args = {TEXT("--base-url"), BaseUrl};
	if (!ProjectId.IsEmpty()) { Request.Args.Append({TEXT("--project-id"), ProjectId}); }
	Request.StdinSecret = MoveTemp(Secret);
	Request.TimeoutSeconds = 60.f;
	return Enqueue(MoveTemp(Request), MoveTemp(OnComplete));
}
