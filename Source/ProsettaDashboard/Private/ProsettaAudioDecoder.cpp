#include "ProsettaAudioDecoder.h"
#include "ProsettaPython.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogProsettaAudioDecoder, Log, All);

namespace
{
	// The project's Python and the ffmpeg.py arguments that acquire the pinned decoder.
	bool DecoderScript(FString& OutPython, FString& OutArgs, FString& OutError)
	{
		if (!FindProsettaPython(OutPython, OutError))
		{
			OutError += TEXT(" Or supply -AudioDecoder.");
			return false;
		}
		const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		const FString Script = FindProsettaScript(TEXT("ffmpeg.py"));
		const FString Certificates = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Certificates/ThirdParty/cacert.pem")));
		if (Script.IsEmpty())
		{
			OutError = TEXT("Automatic decoder setup needs ffmpeg.py (Plugins/Inkpot/Scripts or the project's Scripts). Restore it or supply -AudioDecoder.");
			return false;
		}
		OutArgs = FString::Printf(TEXT("\"%s\" --project \"%s\" --ca-file \"%s\""), *Script, *Project, *Certificates);
		return true;
	}

	class FProcessDecoderFetch final : public IProsettaDecoderFetch
	{
	public:
		FProcessDecoderFetch(FProcHandle InProcess, void* InReadPipe, void* InWritePipe)
			: Process(InProcess), ReadPipe(InReadPipe), WritePipe(InWritePipe) {}

		virtual ~FProcessDecoderFetch() override
		{
			// Not terminated: a download still running when the editor closes installs for the next import.
			FPlatformProcess::CloseProc(Process);
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		}

		virtual bool Finished(int32& OutExitCode, FString& OutOutput) override
		{
			const bool bRunning = FPlatformProcess::IsProcRunning(Process);
			Output += FPlatformProcess::ReadPipe(ReadPipe);
			if (bRunning) { return false; }
			OutExitCode = -1;
			FPlatformProcess::GetProcReturnCode(Process, &OutExitCode);
			OutOutput = Output.TrimStartAndEnd();
			return true;
		}

	private:
		FProcHandle Process;
		void* ReadPipe;
		void* WritePipe;
		FString Output;
	};
}

bool ResolveProsettaAudioDecoder(const FString& Override, FString& OutExecutable, FString& OutError, bool bOffline)
{
	int32 ExitCode = -1;
	FString StdOut, StdErr;
	if (!Override.IsEmpty())
	{
		OutExecutable = Override;
		if (!FPaths::FileExists(OutExecutable) && FPaths::IsRelative(OutExecutable))
		{
			TArray<FString> SearchPaths;
#if PLATFORM_WINDOWS
			FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(SearchPaths, TEXT(";"), true);
			if (FPaths::GetExtension(OutExecutable).IsEmpty()) { OutExecutable += TEXT(".exe"); }
#else
			FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(SearchPaths, TEXT(":"), true);
#endif
			for (const FString& SearchPath : SearchPaths)
			{
				const FString Candidate = FPaths::Combine(SearchPath, OutExecutable);
				if (FPaths::FileExists(Candidate)) { OutExecutable = Candidate; break; }
			}
		}
		OutExecutable = FPaths::ConvertRelativePathToFull(OutExecutable);
		if (!FPlatformProcess::ExecProcess(*OutExecutable, TEXT("-version"), &ExitCode, &StdOut, &StdErr) || ExitCode != 0 || !StdOut.StartsWith(TEXT("ffmpeg version ")))
		{
			OutError = FString::Printf(TEXT("Explicit -AudioDecoder cannot run: %s. %s"), *OutExecutable, *StdErr);
			return false;
		}
		return true;
	}
	FString Python, Args;
	if (!DecoderScript(Python, Args, OutError)) { return false; }
	if (bOffline) { Args += TEXT(" --offline"); }
	else { UE_LOG(LogProsettaAudioDecoder, Display, TEXT("Checking project-managed FFmpeg; first use downloads a pinned decoder.")); }
	if (!FPlatformProcess::ExecProcess(*Python, *Args, &ExitCode, &StdOut, &StdErr) || ExitCode != 0)
	{
		OutError = bOffline ? StdErr.TrimStartAndEnd()
			: FString::Printf(TEXT("FFmpeg setup failed: %s. For offline imports, prewarm Saved/Tools/FFmpeg or supply -AudioDecoder."), *StdErr);
		return false;
	}
	OutExecutable = StdOut.TrimStartAndEnd();
	if (!FPaths::FileExists(OutExecutable))
	{
		OutError = FString::Printf(TEXT("FFmpeg setup did not print a usable decoder path: '%s'. Supply -AudioDecoder."), *OutExecutable);
		return false;
	}
	return true;
}

TUniquePtr<IProsettaDecoderFetch> StartProsettaAudioDecoderFetch(FString& OutError)
{
	FString Python, Args;
	if (!DecoderScript(Python, Args, OutError)) { return nullptr; }
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		OutError = TEXT("Could not create a pipe for the FFmpeg fetch.");
		return nullptr;
	}
	// stdout and stderr share the pipe: the last line is the decoder path or why the fetch failed.
	const FProcHandle Process = FPlatformProcess::CreateProc(*Python, *Args, true, true, true, nullptr, 0,
		*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), WritePipe, nullptr, WritePipe);
	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		OutError = TEXT("Could not launch ffmpeg.py.");
		return nullptr;
	}
	UE_LOG(LogProsettaAudioDecoder, Display, TEXT("Fetching the pinned FFmpeg in the background."));
	return MakeUnique<FProcessDecoderFetch>(Process, ReadPipe, WritePipe);
}
