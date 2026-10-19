#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProsettaProjectWatch.h"
#include "Prosetta/ProsettaWatchSubsystem.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FFakeFetchState
	{
		int32 Launches = 0;
		bool bFinished = false;
		int32 ExitCode = 0;
		FString Output;
	};

	// Stands in for the ffmpeg.py process, so the test never touches the network.
	class FFakeFetch final : public IProsettaDecoderFetch
	{
	public:
		explicit FFakeFetch(TSharedRef<FFakeFetchState> InState) : State(InState) {}
		virtual bool Finished(int32& OutExitCode, FString& OutOutput) override
		{
			if (!State->bFinished) { return false; }
			OutExitCode = State->ExitCode;
			OutOutput = State->Output;
			return true;
		}
	private:
		TSharedRef<FFakeFetchState> State;
	};

	FString CompletionReceipt(const FString& Scope, const FString& Root)
	{
		return FString::Printf(TEXT(R"({"schemaVersion":1,"type":"generation-completed","projectId":199999,"scope":"%s","channel":"live","generationId":"%s","generationPath":"%s","manifestPath":"%s/manifest.json"})"),
			*Scope, *FString::ChrN(64, TEXT('a')), *Root, *Root);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaWatchDecoderFetchTest,
	"Inkpot.Prosetta.ImportCache.WatchDecoderFetch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaWatchDecoderFetchTest::RunTest(const FString& Parameters)
{
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::AutomationTransientDir() / TEXT("ProsettaWatchDecoder") / FGuid::NewGuid().ToString());
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	const FString Root = Directory / TEXT("Generation");
	FFileHelper::SaveStringToFile(CompletionReceipt(TEXT("story"), Root), *(Directory / TEXT("receipt-1.json")));

	// Never initialized, so nothing is applied: Pending shows what the decoder handed it.
	TStrongObjectPtr<UProsettaWatchSubsystem> Importer(NewObject<UProsettaWatchSubsystem>());
	FString Cached;
	const TSharedRef<FFakeFetchState> State = MakeShared<FFakeFetchState>();
	FProsettaWatchDecoder Decoder(Directory, [&Importer] { return Importer.Get(); },
		[&Cached](FString& OutExecutable, FString& OutError)
		{
			if (Cached.IsEmpty()) { OutError = TEXT("The pinned FFmpeg is not cached."); return false; }
			OutExecutable = Cached;
			return true;
		},
		[State](FString& OutError) -> TUniquePtr<IProsettaDecoderFetch>
		{
			++State->Launches;
			State->bFinished = false;
			return MakeUnique<FFakeFetch>(State);
		});

	FString Executable, Error;
	TestFalse(TEXT("an offline cache miss fails the generation"), Decoder.Resolve(Executable, Error));
	TestEqual(TEXT("the miss starts one background fetch"), State->Launches, 1);
	TestTrue(TEXT("the error names the background fetch"), Error.Contains(TEXT("in the background")));
	TestTrue(TEXT("the error names the prewarm command"), Error.Contains(TEXT("ffmpeg.py")));
	TestTrue(TEXT("the fetch is running"), Decoder.IsFetching());

	Decoder.Tick();
	TestFalse(TEXT("a second miss while the fetch runs fails too"), Decoder.Resolve(Executable, Error));
	TestEqual(TEXT("a second miss while the fetch runs does not relaunch"), State->Launches, 1);
	TestEqual(TEXT("nothing is offered while the fetch runs"), Importer->GetPendingCount(), 0);

	State->bFinished = true;
	State->ExitCode = 0;
	Decoder.Tick();
	TestFalse(TEXT("a finished fetch is released"), Decoder.IsFetching());
	TestEqual(TEXT("success offers the failed generation again"), Importer->GetPendingCount(), 1);

	TestFalse(TEXT("a cache that still misses after a fetch fails"), Decoder.Resolve(Executable, Error));
	TestEqual(TEXT("a cache that still misses after a fetch does not refetch in a loop"), State->Launches, 1);
	TestTrue(TEXT("that error says the fetch already ran"), Error.Contains(TEXT("fetched")));

	Cached = Root / TEXT("ffmpeg");
	TestTrue(TEXT("a cache hit resolves"), Decoder.Resolve(Executable, Error));
	TestEqual(TEXT("a cache hit returns the cached decoder"), Executable, Cached);

	FFileHelper::SaveStringToFile(CompletionReceipt(TEXT("other"), Root), *(Directory / TEXT("receipt-2.json")));
	Decoder.Tick();
	TestEqual(TEXT("receipts are offered again only once per fetch"), Importer->GetPendingCount(), 1);

	Cached.Empty();
	State->ExitCode = 1;
	State->Output = TEXT("Downloading FFmpeg\nFFmpeg setup failed: test outage");
	AddExpectedMessage(TEXT("FFmpeg setup failed: test outage"), EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("a miss after a cache hit fails"), Decoder.Resolve(Executable, Error));
	TestEqual(TEXT("a miss after a cache hit fetches again"), State->Launches, 2);
	State->bFinished = true;
	Decoder.Tick();
	Decoder.Tick();
	TestFalse(TEXT("a failed fetch is released"), Decoder.IsFetching());
	TestEqual(TEXT("a failed fetch offers nothing"), Importer->GetPendingCount(), 1);

	TestFalse(TEXT("a miss after a failed fetch fails"), Decoder.Resolve(Executable, Error));
	TestEqual(TEXT("a miss after a failed fetch tries again"), State->Launches, 3);
	return true;
}

#endif
