#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProsettaProjectWatch.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaProjectWatchOutputTest,
	"Inkpot.Prosetta.ImportCache.WatchProjectOutput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaProjectWatchOutputTest::RunTest(const FString& Parameters)
{
	const FString Blocked = TEXT(R"({"schemaVersion":1,"type":"blocked","code":"REST_409"})");
	const FString Fatal = TEXT(R"({"status":"error","message":"Project token rejected."})");
	TArray<FString> Forwarded;
	auto Forward = [&Forwarded](const FString& Line) { Forwarded.Add(Line); };

	FString Buffer = FString::Printf(TEXT("Watching project...\n%s\n{\"schemaVersion\":1,\"type\":\"generation-completed\"}\n%s"), *Blocked, *Fatal);
	ForwardProsettaWatchStatusLines(Buffer, false, Forward);
	TestTrue(TEXT("only typed status lines are forwarded; completions arrive as receipts"), Forwarded == TArray<FString>{Blocked});
	TestEqual(TEXT("an unterminated line waits for the rest of it"), Buffer, Fatal);

	ForwardProsettaWatchStatusLines(Buffer, true, Forward);
	TestTrue(TEXT("on exit the CLI's unterminated fatal result is forwarded"), Forwarded == TArray<FString>{Blocked, Fatal});
	TestTrue(TEXT("a flush empties the buffer"), Buffer.IsEmpty());

	Buffer = TEXT("token=SECRET");
	ForwardProsettaWatchStatusLines(Buffer, true, Forward);
	TestEqual(TEXT("untyped output is never forwarded, even on exit"), Forwarded.Num(), 2);
	return true;
}

#if PLATFORM_MAC || PLATFORM_LINUX
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProsettaProjectWatchStopTest,
	"Inkpot.Prosetta.ImportCache.WatchProjectStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProsettaProjectWatchStopTest::RunTest(const FString& Parameters)
{
	// Stands in for a wrapper that takes a while to stop its CLI after SIGTERM.
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!TestTrue(TEXT("a pipe is created"), FPlatformProcess::CreatePipe(ReadPipe, WritePipe))) { return false; }
	FProcHandle Process = FPlatformProcess::CreateProc(TEXT("/bin/sh"), TEXT("-c \"trap '' TERM; echo ready; sleep 1\""),
		false, true, true, nullptr, 0, nullptr, WritePipe);
	if (!TestTrue(TEXT("the slow wrapper launches"), Process.IsValid()))
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return false;
	}
	FString Output;
	for (const double Deadline = FPlatformTime::Seconds() + 5.0; !Output.Contains(TEXT("ready")) && FPlatformTime::Seconds() < Deadline;)
	{
		FPlatformProcess::Sleep(0.01f);
		Output += FPlatformProcess::ReadPipe(ReadPipe);
	}
	TestTrue(TEXT("the wrapper is ignoring SIGTERM"), Output.Contains(TEXT("ready")));

	FProsettaStoppingProcesses Stopping;
	const double Started = FPlatformTime::Seconds();
	Stopping.Stop(Process, ReadPipe, WritePipe);
	TestTrue(TEXT("Stop returns without waiting for the wrapper"), FPlatformTime::Seconds() - Started < 0.5);
	TestEqual(TEXT("a wrapper still stopping is kept to reap later"), Stopping.Num(), 1);
	Stopping.Reap(false);
	TestEqual(TEXT("a tick does not wait for it either"), Stopping.Num(), 1);
	Stopping.Reap(true);
	TestEqual(TEXT("shutdown waits for it and closes it"), Stopping.Num(), 0);
	return true;
}
#endif

#endif
