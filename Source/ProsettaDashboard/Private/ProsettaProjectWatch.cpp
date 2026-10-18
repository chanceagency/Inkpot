#include "ProsettaProjectWatch.h"
#include "ProsettaAudioDecoder.h"
#include "ProsettaPython.h"
#include "Prosetta/ProsettaReceiptWatch.h"
#include "Prosetta/ProsettaWatchSubsystem.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaProjectWatch, Log, All);

namespace
{
	UProsettaWatchSubsystem* WatchSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UProsettaWatchSubsystem>() : nullptr;
	}

	// The terminal command that fetches the pinned decoder by hand, for messages.
	FString FfmpegCommand()
	{
		const FString Script = FindProsettaScript(TEXT("ffmpeg.py"));
		return FString::Printf(TEXT("python3 %s"), Script.IsEmpty() ? TEXT("Plugins/Inkpot/Scripts/ffmpeg.py") : *DisplayProsettaScriptPath(Script));
	}

	// A typed CLI line other than a completion (blocked, error, reconnecting, a fatal --json result).
	// Completions arrive as receipts instead, so each generation reaches the importer exactly once.
	bool IsStatusLine(const FString& Line)
	{
		TSharedPtr<FJsonObject> Object;
		FString Type, Result;
		if (!Line.StartsWith(TEXT("{")) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Object) || !Object.IsValid()) { return false; }
		if (Object->HasField(TEXT("schemaVersion"))) { return Object->TryGetStringField(TEXT("type"), Type) && Type != TEXT("generation-completed"); }
		return Object->TryGetStringField(TEXT("status"), Result) && Result == TEXT("error");
	}
}

void ForwardProsettaWatchStatusLines(FString& Buffer, bool bFlush, TFunctionRef<void(const FString&)> Forward)
{
	int32 Newline;
	while (Buffer.FindChar(TEXT('\n'), Newline))
	{
		const FString Line = Buffer.Left(Newline).TrimStartAndEnd();
		Buffer.RightChopInline(Newline + 1);
		// Typed events reach the importer, whose status explains them; other output may echo credentials.
		if (IsStatusLine(Line)) { Forward(Line); }
	}
	if (bFlush)
	{
		const FString Line = Buffer.TrimStartAndEnd();
		Buffer.Empty();
		if (IsStatusLine(Line)) { Forward(Line); }
	}
	// An unexpected unterminated line must not grow without bound.
	else if (Buffer.Len() > 1024 * 1024) { Buffer.Empty(); }
}

void FProsettaStoppingProcesses::Stop(FProcHandle Process, void* ReadPipe, void* WritePipe)
{
	// The wrapper handles SIGTERM by stopping the CLI it started before exiting.
	if (FPlatformProcess::IsProcRunning(Process)) { FPlatformProcess::TerminateProc(Process, true); }
	Entries.Add({Process, ReadPipe, WritePipe});
	Reap(false);
}

void FProsettaStoppingProcesses::Reap(bool bWait)
{
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Entries[Index];
		// Drained and discarded, so a stopping wrapper never blocks on a full pipe.
		FPlatformProcess::ReadPipe(Entry.ReadPipe);
		while (bWait && FPlatformProcess::IsProcRunning(Entry.Process))
		{
			FPlatformProcess::Sleep(0.05f);
			FPlatformProcess::ReadPipe(Entry.ReadPipe);
		}
		if (FPlatformProcess::IsProcRunning(Entry.Process)) { continue; }
		FPlatformProcess::CloseProc(Entry.Process);
		FPlatformProcess::ClosePipe(Entry.ReadPipe, Entry.WritePipe);
		Entries.RemoveAt(Index);
	}
}

FProsettaWatchDecoder::FProsettaWatchDecoder(FString InReceiptDirectory, FImporterGetter InGetImporter, FCheckCache InCheckCache, FStartFetch InStartFetch)
	: ReceiptDirectory(MoveTemp(InReceiptDirectory))
	, GetImporter(MoveTemp(InGetImporter))
	, CheckCache(MoveTemp(InCheckCache))
	, StartFetch(MoveTemp(InStartFetch))
{
}

bool FProsettaWatchDecoder::Resolve(FString& OutExecutable, FString& OutError)
{
	FString CacheError;
	if (CheckCache(OutExecutable, CacheError))
	{
		bFetched = false;
		return true;
	}
	const FString Prewarm = FString::Printf(TEXT("To prewarm it, run %s."), *FfmpegCommand());
	if (bFetched)
	{
		OutError = FString::Printf(TEXT("The pinned FFmpeg was fetched, but the cache check still fails: %s %s"), *CacheError, *Prewarm);
		return false;
	}
	if (!Fetch)
	{
		FString StartError;
		Fetch = StartFetch(StartError);
		if (!Fetch)
		{
			OutError = FString::Printf(TEXT("The pinned FFmpeg could not be fetched in the background: %s %s"), *StartError, *Prewarm);
			return false;
		}
	}
	OutError = FString::Printf(TEXT("Fetching the pinned FFmpeg in the background; this generation retries when it finishes. %s"), *Prewarm);
	return false;
}

void FProsettaWatchDecoder::Tick()
{
	int32 ExitCode = -1;
	FString Output;
	if (!Fetch || !Fetch->Finished(ExitCode, Output)) { return; }
	Fetch.Reset();
	if (ExitCode != 0)
	{
		// Logged once per fetch; the next generation with compressed audio starts another.
		FString Reason = Output.IsEmpty() ? FString(TEXT("no output.")) : Output;
		Reason.Split(TEXT("\n"), nullptr, &Reason, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		UE_LOG(LogProsettaProjectWatch, Warning, TEXT("Background FFmpeg fetch failed (exit %d; %s fetches it by hand): %s"), ExitCode, *FfmpegCommand(), *Reason.TrimStartAndEnd().Left(512));
		return;
	}
	bFetched = true;
	UProsettaWatchSubsystem* Importer = GetImporter();
	if (!Importer) { return; }
	// The transport's content de-dup already recorded the receipt whose import failed, so a fresh map
	// offers every receipt again.
	TMap<FString, FString> Unseen;
	const int32 Offered = FProsettaReceiptWatch::HandOffReceipts(ReceiptDirectory, Unseen,
		[Importer](const FString& JSON) { return Importer->ConsumeEvent(JSON); });
	UE_LOG(LogProsettaProjectWatch, Display, TEXT("Pinned FFmpeg fetched; offered %d completed generation(s) to the importer again."), Offered);
}

FProsettaProjectWatch::FProsettaProjectWatch()
{
	// The prosetta.py wrapper writes receipts here. The decoder is looked up only when a generation first
	// carries compressed audio, and never downloaded on the game thread.
	const FString ReceiptDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ProsettaSync/Watch"));
	DecoderOverride = MakeShared<FString>();
	Decoder = MakeShared<FProsettaWatchDecoder>(ReceiptDirectory, &WatchSubsystem,
		[Override = DecoderOverride](FString& OutExecutable, FString& OutError) { return ResolveProsettaAudioDecoder(*Override, OutExecutable, OutError, true); },
		&StartProsettaAudioDecoderFetch);
	// The importer keeps the resolver; it must not outlive this watch.
	Receipts = MakeUnique<FProsettaReceiptWatch>(ReceiptDirectory,
		[WeakDecoder = TWeakPtr<FProsettaWatchDecoder>(Decoder)](FString& OutExecutable, FString& OutError)
		{
			const TSharedPtr<FProsettaWatchDecoder> Pinned = WeakDecoder.Pin();
			if (!Pinned)
			{
				OutError = TEXT("The project watch has shut down.");
				return false;
			}
			return Pinned->Resolve(OutExecutable, OutError);
		});
	TransportStatus = Receipts->GetError();
	IProsettaEditorWatch::Installed = this;
	auto Register = [this](const TCHAR* Name, const TCHAR* Help, TFunction<void()> Action)
	{
		Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(Name, Help,
			FConsoleCommandDelegate::CreateLambda(MoveTemp(Action)), ECVF_Default));
	};
	auto Report = [this] { UE_LOG(LogProsettaProjectWatch, Display, TEXT("%s"), *Status()); };
	Register(TEXT("Prosetta.Watch.Project.Start"), TEXT("Start this checkout's CLI wrapper watch; no token or config argument needed."), [this, Report] { Start(); Report(); });
	Register(TEXT("Prosetta.Watch.Project.Stop"), TEXT("Stop the editor-owned CLI; terminal watches remain owned by their terminal."), [this, Report] { Stop(); Report(); });
	Register(TEXT("Prosetta.Watch.Project.Status"), TEXT("Show project CLI transport and verified importer status."), Report);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProsettaProjectWatch::Tick), 0.5f);
}

FProsettaProjectWatch::~FProsettaProjectWatch()
{
	if (IProsettaEditorWatch::Installed == this) { IProsettaEditorWatch::Installed = nullptr; }
	CloseTransport();
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	for (IConsoleObject* Command : Commands) { IConsoleManager::Get().UnregisterConsoleObject(Command); }
}

TArray<FString> FProsettaProjectWatch::BuildWatchArguments(const FProsettaWatchOptions& Options)
{
	TArray<FString> Args = {TEXT("watch"), TEXT("--channel"), Options.Channel.IsEmpty() ? FString(TEXT("live")) : Options.Channel};
	if (!Options.Volumes.IsEmpty()) { Args.Append({TEXT("--volumes"), Options.Volumes}); }
	if (Options.bNoMirror) { Args.Add(TEXT("--no-mirror")); }
	if (!Options.BaseUrl.IsEmpty()) { Args.Append({TEXT("--base-url"), Options.BaseUrl}); }
	if (!Options.ProjectId.IsEmpty()) { Args.Append({TEXT("--project-id"), Options.ProjectId}); }
	return Args;
}

void FProsettaProjectWatch::Start()
{
	Start(OptionsProvider ? OptionsProvider() : FProsettaWatchOptions());
}

void FProsettaProjectWatch::Start(const FProsettaWatchOptions& Options)
{
	if (IsRunning()) { return; }
	Stopping.Reap(false);
	if (Stopping.Num() > 0)
	{
		TransportStatus = TEXT("The previous editor-owned project CLI is still stopping; Start watch again in a moment.");
		UE_LOG(LogProsettaProjectWatch, Warning, TEXT("%s"), *TransportStatus);
		return;
	}
	if (!Receipts->IsWatching())
	{
		UE_LOG(LogProsettaProjectWatch, Warning, TEXT("Project CLI not started: %s"), *Receipts->GetError());
		return;
	}
	UProsettaWatchSubsystem* Subsystem = WatchSubsystem();
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Script = FindProsettaScript(TEXT("prosetta.py"));
	FString Python, Error;
	if (!Subsystem) { Error = TEXT("the Prosetta watch subsystem is unavailable."); }
	else if (Script.IsEmpty()) { Error = FString::Printf(TEXT("the prosetta.py wrapper is missing (looked in %s)."), *FString::Join(ProsettaScriptCandidates(TEXT("prosetta.py")), TEXT(", "))); }
	else { FindProsettaPython(Python, Error); }
	if (!Error.IsEmpty())
	{
		TransportStatus = TEXT("Project CLI not started: ") + Error;
		UE_LOG(LogProsettaProjectWatch, Warning, TEXT("%s"), *TransportStatus);
		return;
	}
	// Starting takes transport ownership again, stopping any plugin-owned CLI watch begun since.
	Receipts->Connect();
	CloseTransport();
	*DecoderOverride = Options.AudioDecoderPath;
	ActiveOptions = Options;
	LastExit = INDEX_NONE;
	TArray<FString> Args = BuildWatchArguments(Options);
	for (FString& Arg : Args)
	{
		if (Arg.Contains(TEXT(" "))) { Arg = FString::Printf(TEXT("\"%s\""), *Arg); }
	}
	if (FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		// No token on the command line: the wrapper reads the ignored local config or PROSETTA_PROJECT_TOKEN.
		Process = FPlatformProcess::CreateProc(*Python, *FString::Printf(TEXT("-u \"%s\" %s"), *Script, *FString::Join(Args, TEXT(" "))), false, true, true, nullptr, 0, *Project, WritePipe);
	}
	if (!Process.IsValid())
	{
		CloseTransport();
		TransportStatus = FString::Printf(TEXT("Project CLI could not be launched; run python3 %s watch in a terminal to see why."), *DisplayProsettaScriptPath(Script));
		UE_LOG(LogProsettaProjectWatch, Warning, TEXT("%s"), *TransportStatus);
		return;
	}
	TransportStatus = TEXT("Editor-owned project CLI running; imports wait until Play stops.");
}

void FProsettaProjectWatch::Stop()
{
	if (!Process.IsValid()) { return; }
	CloseTransport();
	TransportStatus = TEXT("Editor-owned CLI stopped; receipts from a terminal watch are still imported.");
}

bool FProsettaProjectWatch::IsRunning() const
{
	return Process.IsValid() && FPlatformProcess::IsProcRunning(Process);
}

FString FProsettaProjectWatch::Status() const
{
	const FString Transport = TransportStatus.IsEmpty() ? FString(TEXT("Waiting for completed generations from a terminal watch.")) : TransportStatus;
	const UProsettaWatchSubsystem* Subsystem = WatchSubsystem();
	return Subsystem ? Transport + TEXT(" Importer: ") + Subsystem->GetStatus() : Transport;
}

bool FProsettaProjectWatch::Tick(float DeltaTime)
{
	Decoder->Tick();
	Stopping.Reap(false);
	ConsumeOutput();
	if (Process.IsValid() && !FPlatformProcess::IsProcRunning(Process))
	{
		int32 ExitCode = -1;
		FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
		LastExit = ExitCode;
		ConsumeOutput(true);
		CloseTransport();
		// CLI diagnostics are not echoed here: they may carry credentials.
		TransportStatus = FString::Printf(TEXT("Editor-owned project CLI exited (%d); run python3 %s watch in a terminal to see why, then Start watch to retry."), ExitCode, *DisplayProsettaScriptPath(FindProsettaScript(TEXT("prosetta.py"))));
		UE_LOG(LogProsettaProjectWatch, Warning, TEXT("%s"), *TransportStatus);
	}
	return true;
}

void FProsettaProjectWatch::CloseTransport()
{
	if (Process.IsValid())
	{
		Stopping.Stop(Process, ReadPipe, WritePipe);
		Process.Reset();
	}
	else { FPlatformProcess::ClosePipe(ReadPipe, WritePipe); }
	ReadPipe = WritePipe = nullptr;
	OutputBuffer.Empty();
}

void FProsettaProjectWatch::ConsumeOutput(bool bFlush)
{
	if (!ReadPipe) { return; }
	OutputBuffer += FPlatformProcess::ReadPipe(ReadPipe);
	UProsettaWatchSubsystem* Subsystem = WatchSubsystem();
	ForwardProsettaWatchStatusLines(OutputBuffer, bFlush, [Subsystem](const FString& Line)
	{
		if (Subsystem) { Subsystem->ConsumeEvent(Line); }
	});
}
