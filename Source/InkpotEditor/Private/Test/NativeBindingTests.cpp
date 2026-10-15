#if !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Inkpot/NativeBindings/InkpotNativeBinding.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingLibrary.h"
#include "Inkpot/Prosetta/ProsettaNativeBindingsManifest.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/InkpotStoryInternal.h"
#include "Inkpot/InkpotValueLibrary.h"
#include "NativeBindings/InkpotNativeBindingSource.h"
#include "NativeBindings/InkpotNativeBindingGenerator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"

/* ---------------------------------------------------------------------------
 * A native binding module written exactly the way Prosetta ships one ( and the way a
 * local module is expected to be written ): plain C++ inside Prosetta::Bindings::<Module>,
 * no engine headers. The loader below is what Prosetta.NativeBindings.Generate writes.
 * ------------------------------------------------------------------------- */
namespace Prosetta::Bindings::InkpotNativeTest
{
	static int PingCount = 0;
	static char EchoBuffer[256];

	int inkpot_test_add(int a, int b) { return a + b; }

	const char* inkpot_test_echo(const char* text)
	{
		int index = 0;
		for (; text && text[index] && index < 255; ++index)
			EchoBuffer[index] = text[index];
		EchoBuffer[index] = 0;
		return EchoBuffer;
	}

	float inkpot_test_scale(float value, int times) { return value * times; }

	bool inkpot_test_flip(bool value) { return !value; }

	void inkpot_test_ping() { ++PingCount; }
}

INKPOT_NATIVE_BINDING_MODULE_BEGIN(InkpotNativeTest, EInkpotNativeBindingSource::Local, "")
	INKPOT_NATIVE_BINDING(inkpot_test_add, true)
	INKPOT_NATIVE_BINDING(inkpot_test_echo, true)
	INKPOT_NATIVE_BINDING(inkpot_test_scale, true)
	INKPOT_NATIVE_BINDING(inkpot_test_flip, true)
	INKPOT_NATIVE_BINDING(inkpot_test_ping, false)
INKPOT_NATIVE_BINDING_MODULE_END(InkpotNativeTest)

namespace
{
	// Compiled form of:
	//   EXTERNAL inkpot_test_add(a,b) ... VAR counter = 0
	//   {inkpot_test_add(2,3)}
	//   {inkpot_test_echo("hi")}
	//   {inkpot_test_scale(1.5,3)}
	//   {inkpot_test_flip(true)}
	//   ~ inkpot_test_ping()
	//   ~ counter = 1
	//   done
	const TCHAR* TestStoryJSON = TEXT(
		"{\"inkVersion\":21,\"root\":[["
		"\"ev\",2,3,{\"x()\":\"inkpot_test_add\",\"exArgs\":2},\"out\",\"/ev\",\"\\n\","
		"\"ev\",\"str\",\"^hi\",\"/str\",{\"x()\":\"inkpot_test_echo\",\"exArgs\":1},\"out\",\"/ev\",\"\\n\","
		"\"ev\",1.5,3,{\"x()\":\"inkpot_test_scale\",\"exArgs\":2},\"out\",\"/ev\",\"\\n\","
		"\"ev\",true,{\"x()\":\"inkpot_test_flip\",\"exArgs\":1},\"out\",\"/ev\",\"\\n\","
		"\"ev\",{\"x()\":\"inkpot_test_ping\",\"exArgs\":0},\"pop\",\"/ev\","
		"\"ev\",1,\"/ev\",{\"VAR=\":\"counter\",\"re\":true},"
		"\"^done\",\"\\n\",\"end\",null],"
		"\"done\",{\"global decl\":[\"ev\",0,{\"VAR=\":\"counter\"},\"/ev\",\"end\",null]}],\"listDefs\":{}}");

	FString ManifestJSON(const FString& InSha, int32 InAddArity, const FString& InExtraModule = FString())
	{
		return FString::Printf(TEXT(
			"{\"formatVersion\":1,\"projectId\":42,\"projectName\":\"Sandbox\",\"engineTargets\":[\"web\",\"unreal\"],"
			"\"externals\":["
			"{\"name\":\"inkpot_test_add\",\"arity\":2,\"params\":[\"a\",\"b\"],\"declaredIn\":[{\"inkScriptId\":7,\"qualifiedName\":\"Story/Main\"}]},"
			"{\"name\":\"inkpot_test_echo\",\"arity\":1,\"params\":[\"text\"],\"declaredIn\":[]},"
			"{\"name\":\"inkpot_test_flip\",\"arity\":1,\"params\":[\"value\"],\"declaredIn\":[]},"
			"{\"name\":\"inkpot_test_ping\",\"arity\":0,\"params\":[],\"declaredIn\":[]},"
			"{\"name\":\"inkpot_test_scale\",\"arity\":2,\"params\":[\"value\",\"times\"],\"declaredIn\":[]},"
			"{\"name\":\"unbound_external\",\"arity\":1,\"params\":[\"x\"],\"declaredIn\":[]}],"
			"\"modules\":[%s{\"moduleName\":\"InkpotNativeTest\",\"languages\":{"
			"\"cpp\":{\"status\":\"shipped\",\"scriptId\":12,\"sourceSha256\":\"%s\",\"functions\":["
			"{\"name\":\"inkpot_test_add\",\"arity\":%d,\"lookaheadSafe\":true},"
			"{\"name\":\"inkpot_test_echo\",\"arity\":1,\"lookaheadSafe\":true},"
			"{\"name\":\"inkpot_test_flip\",\"arity\":1,\"lookaheadSafe\":true},"
			"{\"name\":\"inkpot_test_ping\",\"arity\":0,\"lookaheadSafe\":false},"
			"{\"name\":\"inkpot_test_scale\",\"arity\":2,\"lookaheadSafe\":true}],"
			"\"errorCount\":0,\"warningCount\":0,\"diagnostics\":[],\"updatedAt\":\"2026-10-01T00:00:00.000Z\","
			"\"artifactKey\":\"native-binding:cpp/InkpotNativeTest\",\"relativePath\":\"native-bindings/cpp/InkpotNativeTest.cpp\",\"byteSize\":512},"
			"\"typescript\":{\"status\":\"excluded\",\"reason\":\"validation_errors\",\"scriptId\":13,\"sourceSha256\":\"%s\",\"functions\":[],"
			"\"errorCount\":1,\"warningCount\":0,\"diagnostics\":[{\"severity\":\"error\",\"message\":\"Unexpected token\",\"line\":3,\"code\":\"ts-syntax\"}],\"updatedAt\":\"2026-10-01T00:00:00.000Z\"}}}],"
			"\"coverage\":{\"unbound\":[\"unbound_external\"],\"byLanguage\":{\"cpp\":{\"bound\":5,\"missing\":1},\"typescript\":{\"bound\":0,\"missing\":6}}}}"),
			*InExtraModule, *InSha, InAddArity, *InSha);
	}

	const TCHAR* ScaffoldSource = TEXT(
		"// Native Bound Functions module \"Inventory\" (C++).\n"
		"// Each function in this namespace implements the Ink EXTERNAL of the same name.\n"
		"namespace Prosetta::Bindings::Inventory {\n"
		"    // Describe an item.\n"
		"    // Ink: describe_item(item_name: string, quantity: int) -> string\n"
		"    const char* describe_item(const char* item_name, int quantity) {\n"
		"        // TODO: implement.\n"
		"        return \"\";\n"
		"    }\n"
		"\n"
		"    // @lookaheadSafe\n"
		"    void play_chime() {\n"
		"    }\n"
		"\n"
		"    float scale_price(float price, int count) {\n"
		"        const char* unused = \"} not a brace {\"; // comment with } brace\n"
		"        return price * count;\n"
		"    }\n"
		"}  // namespace Prosetta::Bindings::Inventory\n");

	FString Shipped(const FString& InAuthored, const FString& InModule, int32 InScript = 12)
	{
		return FString::Printf(TEXT("// Generated by Prosetta. Do not edit; changes are overwritten on sync.\n// prosetta-native-binding module=%s language=cpp project=42 scriptId=%d\n// prosetta-source-sha256=%s\n\n%s"),
			*InModule, InScript, *FInkpotNativeBindingSourceScanner::Sha256(InAuthored), *InAuthored);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotNativeBindingRegistryTest, "Inkpot.Prosetta.NativeBindings.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotNativeBindingRegistryTest::RunTest(const FString& InParameters)
{
	const FInkpotNativeBindingRegistry& registry = FInkpotNativeBindingRegistry::Get();
	const FInkpotNativeBindingModule* module = registry.FindModule(TEXT("InkpotNativeTest"));
	if (!TestNotNull(TEXT("test module registered from static initialiser"), module)) return false;
	TestEqual(TEXT("module source"), module->Source, EInkpotNativeBindingSource::Local);
	TestEqual(TEXT("function count"), module->Functions.Num(), 5);

	const FInkpotNativeBindingModule* owner = nullptr;
	const FInkpotNativeFunction* add = registry.FindFunction(TEXT("inkpot_test_add"), &owner);
	if (TestNotNull(TEXT("add found"), add))
	{
		TestEqual(TEXT("add arity"), add->Descriptor.Arity, 2);
		TestEqual(TEXT("add return"), add->Descriptor.ReturnType, FString(TEXT("int")));
		TestEqual(TEXT("add parameters"), add->Descriptor.ParameterTypes, TArray<FString>({ TEXT("int"), TEXT("int") }));
		TestTrue(TEXT("add lookahead"), add->Descriptor.bLookaheadSafe);
		TestEqual(TEXT("add module"), add->Descriptor.ModuleName, FString(TEXT("InkpotNativeTest")));
		TestTrue(TEXT("owner"), owner == module);
	}
	const FInkpotNativeFunction* echo = registry.FindFunction(TEXT("inkpot_test_echo"));
	if (TestNotNull(TEXT("echo found"), echo))
	{
		TestEqual(TEXT("echo return"), echo->Descriptor.ReturnType, FString(TEXT("const char*")));
		TestEqual(TEXT("echo parameters"), echo->Descriptor.ParameterTypes, TArray<FString>({ TEXT("const char*") }));
	}
	const FInkpotNativeFunction* ping = registry.FindFunction(TEXT("inkpot_test_ping"));
	if (TestNotNull(TEXT("ping found"), ping))
	{
		TestEqual(TEXT("ping arity"), ping->Descriptor.Arity, 0);
		TestEqual(TEXT("ping return"), ping->Descriptor.ReturnType, FString(TEXT("void")));
		TestFalse(TEXT("ping lookahead"), ping->Descriptor.bLookaheadSafe);
	}
	TestNull(TEXT("unknown function"), registry.FindFunction(TEXT("not_a_function")));
	TestFalse(TEXT("no duplicates among test functions"), registry.FindDuplicateFunctionNames().Contains(TEXT("inkpot_test_add")));

	// Direct glue: coercion and unsupported argument reporting.
	{
		TArray<Ink::FValueType> arguments;
		arguments.Add(Ink::FValueType(2));
		arguments.Add(Ink::FValueType(true));
		TSharedPtr<Ink::FValueType> result = add->Thunk(arguments);
		TestTrue(TEXT("bool coerced to int"), result.IsValid() && result->HasSubtype<int32>() && result->GetSubtype<int32>() == 3);
		AddExpectedError(TEXT("expects 2 argument(s) but Ink passed 1"), EAutomationExpectedErrorFlags::Contains, 1, false);
		arguments.RemoveAt(1);
		TestFalse(TEXT("wrong arity rejected"), add->Thunk(arguments).IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotNativeBindingRuntimeTest, "Inkpot.Prosetta.NativeBindings.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotNativeBindingRuntimeTest::RunTest(const FString& InParameters)
{
	TSharedPtr<FInkpotStoryInternal> internal = MakeShared<FInkpotStoryInternal>(FString(TestStoryJSON), 777);
	if (!TestTrue(TEXT("test story JSON loads"), internal->IsValidStory())) return false;
	TStrongObjectPtr<UInkpotStory> story(NewObject<UInkpotStory>(GetTransientPackage()));
	story->Initialise(internal);

	// EXTERNAL discovery from compiled content.
	const TArray<FInkpotExternalDeclaration> externals = story->GetDeclaredExternalFunctions();
	if (TestEqual(TEXT("five externals discovered"), externals.Num(), 5))
	{
		TestEqual(TEXT("sorted first"), externals[0].Name, FString(TEXT("inkpot_test_add")));
		TestEqual(TEXT("add arity"), externals[0].Arity, 2);
		TestEqual(TEXT("ping arity"), externals[3].Arity, 0);
		TestFalse(TEXT("no fallback"), externals[0].bHasFallback);
	}

	// Nothing bound yet: validation reports every external and the report lists them unbound.
	TArray<FString> unbound;
	AddExpectedError(TEXT("but nothing is bound to it"), EAutomationExpectedErrorFlags::Contains, 5, false);
	TestFalse(TEXT("validation fails while unbound"), story->ValidateExternalBindings(unbound, true));
	TestEqual(TEXT("five unbound"), unbound.Num(), 5);
	TestFalse(TEXT("not bound"), story->IsExternalFunctionBound(TEXT("inkpot_test_add")));

	// Filtering to an unrelated module binds nothing.
	FInkpotNativeBindingReport filtered = story->BindRegisteredNativeFunctions({ TEXT("NoSuchModule") }, false);
	TestEqual(TEXT("filtered binds nothing"), filtered.Bound.Num(), 0);
	TestEqual(TEXT("filtered reports unbound"), filtered.Unbound.Num(), 5);
	TestFalse(TEXT("filtered incomplete"), filtered.bComplete);

	// Bind the registered module.
	FInkpotNativeBindingReport report = story->BindRegisteredNativeFunctions(TArray<FString>(), false);
	TestEqual(TEXT("five bound"), report.Bound.Num(), 5);
	TestEqual(TEXT("none unbound"), report.Unbound.Num(), 0);
	TestTrue(TEXT("complete"), report.bComplete);
	TestTrue(TEXT("validation passes"), story->ValidateExternalBindings(unbound, true));
	TestTrue(TEXT("bound"), story->IsExternalFunctionBound(TEXT("inkpot_test_add")));

	// Binding again skips everything.
	FInkpotNativeBindingReport again = story->BindRegisteredNativeFunctions(TArray<FString>(), false);
	TestEqual(TEXT("already bound"), again.AlreadyBound.Num(), 5);
	TestEqual(TEXT("nothing rebound"), again.Bound.Num(), 0);

	// Observers: one variable, all variables.
	int32 counterChanges = 0;
	int32 anyChanges = 0;
	FString lastVariable;
	FInkpotVariableObserverHandle counterHandle = story->ObserveVariableNative(TEXT("counter"), [&counterChanges](UInkpotStory*, const FString&, const FInkpotValue& Value)
	{
		if (UInkpotValueLibrary::InkpotValueAsInt(Value) == 1) ++counterChanges;
	});
	FInkpotVariableObserverHandle allHandle = story->ObserveAllVariablesNative([&anyChanges, &lastVariable](UInkpotStory*, const FString& Variable, const FInkpotValue&)
	{
		++anyChanges;
		lastVariable = Variable;
	});
	TestTrue(TEXT("observer handles valid"), counterHandle.IsValid() && allHandle.IsValid() && counterHandle.Id != allHandle.Id);
	TestEqual(TEXT("global variable names"), story->GetGlobalVariableNames(), TArray<FString>({ TEXT("counter") }));

	// Run the story through the native functions.
	Prosetta::Bindings::InkpotNativeTest::PingCount = 0;
	const FString output = story->ContinueMaximally();
	TestEqual(TEXT("native functions produce the expected output"), output, FString(TEXT("5\nhi\n4.5\nfalse\ndone\n")));
	TestEqual(TEXT("lookahead-unsafe function called exactly once"), Prosetta::Bindings::InkpotNativeTest::PingCount, 1);
	TestEqual(TEXT("variable observer fired"), counterChanges, 1);
	TestEqual(TEXT("all-variables observer fired"), anyChanges, 1);
	TestEqual(TEXT("observed variable name"), lastVariable, FString(TEXT("counter")));

	TestTrue(TEXT("remove observer"), story->RemoveVariableObserver(counterHandle));
	TestFalse(TEXT("remove twice fails"), story->RemoveVariableObserver(counterHandle));

	// Direct C++ binding, inkcpp style. The registered module bound this name above, so
	// clear it first: a plain bind then succeeds, and the refused duplicate is the one
	// "already bound" warning this test expects.
	story->UnbindExternalFunction(TEXT("inkpot_test_add"));
	TestFalse(TEXT("unbound"), story->IsExternalFunctionBound(TEXT("inkpot_test_add")));
	TestTrue(TEXT("direct binding"), story->BindNativeFunction(TEXT("inkpot_test_add"), &Prosetta::Bindings::InkpotNativeTest::inkpot_test_add, true));
	TestTrue(TEXT("override binding"), story->BindNativeFunction(TEXT("inkpot_test_add"), &Prosetta::Bindings::InkpotNativeTest::inkpot_test_add, true, true));
	AddExpectedMessagePlain(TEXT("'inkpot_test_add' is already bound"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("duplicate binding refused without override"), story->BindNativeFunction(TEXT("inkpot_test_add"), &Prosetta::Bindings::InkpotNativeTest::inkpot_test_add, true, false));
	TestTrue(TEXT("still bound"), story->IsExternalFunctionBound(TEXT("inkpot_test_add")));

	// Story audit against a manifest.
	FProsettaNativeBindingsManifest manifest;
	FString error;
	TestTrue(TEXT("manifest parses"), FProsettaNativeBindingsManifest::Parse(ManifestJSON(FInkpotNativeBindingSourceScanner::Sha256(TEXT("x")), 2), manifest, error));
	const FInkpotNativeBindingAudit audit = UInkpotNativeBindingLibrary::AuditStory(story.Get(), manifest, true);
	TestEqual(TEXT("story audit has no errors"), audit.ErrorCount, 0);
	TestTrue(TEXT("story audit passed"), audit.bPassed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotNativeBindingManifestTest, "Inkpot.Prosetta.NativeBindings.Manifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotNativeBindingManifestTest::RunTest(const FString& InParameters)
{
	const FString sha = FInkpotNativeBindingSourceScanner::Sha256(TEXT("authored"));
	FProsettaNativeBindingsManifest manifest;
	FString error;
	if (!TestTrue(TEXT("manifest parses"), FProsettaNativeBindingsManifest::Parse(ManifestJSON(sha, 2), manifest, error))) { AddError(error); return false; }
	TestTrue(TEXT("valid"), manifest.bValid);
	TestEqual(TEXT("project id"), manifest.ProjectId, 42);
	TestEqual(TEXT("project name"), manifest.ProjectName, FString(TEXT("Sandbox")));
	TestEqual(TEXT("engine targets"), manifest.EngineTargets, TArray<FString>({ TEXT("web"), TEXT("unreal") }));
	TestEqual(TEXT("externals"), manifest.Externals.Num(), 6);
	TestEqual(TEXT("unbound"), manifest.Unbound, TArray<FString>({ TEXT("unbound_external") }));
	TestEqual(TEXT("cpp coverage"), manifest.CoverageByLanguage.FindRef(TEXT("cpp")).Missing, 1);
	const FProsettaExternalDeclaration* add = manifest.FindExternal(TEXT("inkpot_test_add"));
	if (TestNotNull(TEXT("external found"), add))
	{
		TestEqual(TEXT("external params"), add->Params, TArray<FString>({ TEXT("a"), TEXT("b") }));
		TestEqual(TEXT("declared in"), add->DeclaredIn.Num(), 1);
		TestEqual(TEXT("declared script"), add->DeclaredIn[0].InkScriptId, 7);
	}
	const FProsettaNativeBindingModule* module = manifest.FindModule(TEXT("inkpotnativetest"));
	if (TestNotNull(TEXT("module found case-insensitively"), module))
	{
		const FProsettaNativeBindingLanguageEntry* cpp = module->FindCpp();
		if (TestNotNull(TEXT("cpp entry"), cpp))
		{
			TestTrue(TEXT("shipped"), cpp->IsShipped());
			TestEqual(TEXT("sha"), cpp->SourceSha256, sha);
			TestEqual(TEXT("relative path"), cpp->RelativePath, FString(TEXT("native-bindings/cpp/InkpotNativeTest.cpp")));
			TestEqual(TEXT("functions"), cpp->Functions.Num(), 5);
			TestEqual(TEXT("byte size"), cpp->ByteSize, 512);
		}
		const FProsettaNativeBindingLanguageEntry* ts = module->FindLanguage(TEXT("typescript"));
		if (TestNotNull(TEXT("typescript entry"), ts))
		{
			TestFalse(TEXT("excluded"), ts->IsShipped());
			TestEqual(TEXT("reason"), ts->Reason, FString(TEXT("validation_errors")));
			TestEqual(TEXT("diagnostic line"), ts->Diagnostics.Num() == 1 ? ts->Diagnostics[0].Line : -2, 3);
		}
	}
	TestEqual(TEXT("one shipped cpp module"), manifest.GetShippedCppModules().Num(), 1);

	// Rejections.
	FProsettaNativeBindingsManifest rejected;
	TestFalse(TEXT("malformed rejected"), FProsettaNativeBindingsManifest::Parse(TEXT("{\"formatVersion\":1,"), rejected, error));
	TestFalse(TEXT("rejected invalid"), rejected.bValid);
	TestFalse(TEXT("format version 2 rejected"), FProsettaNativeBindingsManifest::Parse(TEXT("{\"formatVersion\":2,\"projectId\":1,\"projectName\":\"x\",\"externals\":[],\"modules\":[],\"coverage\":{\"unbound\":[],\"byLanguage\":{}}}"), rejected, error));
	TestTrue(TEXT("version error message"), error.Contains(TEXT("formatVersion")));
	TestFalse(TEXT("missing modules rejected"), FProsettaNativeBindingsManifest::Parse(TEXT("{\"formatVersion\":1,\"projectId\":1,\"projectName\":\"x\",\"externals\":[]}"), rejected, error));

	// Registry audit: the registered test module matches the manifest.
	FInkpotNativeBindingAudit audit = UInkpotNativeBindingLibrary::AuditRegistryAgainstManifest(manifest);
	TestEqual(TEXT("audit errors"), audit.ErrorCount, 0);
	TestTrue(TEXT("audit passed"), audit.bPassed);
	TestTrue(TEXT("unbound external reported as warning"), audit.Findings.ContainsByPredicate([](const FInkpotNativeBindingAuditFinding& F) { return F.Code == TEXT("external_unbound") && F.FunctionName == TEXT("unbound_external") && F.Severity == EInkpotNativeBindingAuditSeverity::Warning; }));
	TestFalse(TEXT("module in the manifest is not reported as local"), audit.Findings.ContainsByPredicate([](const FInkpotNativeBindingAuditFinding& F) { return F.Code == TEXT("local_module") && F.ModuleName == TEXT("InkpotNativeTest"); }));

	// Arity drift and a module that was never imported are errors.
	FProsettaNativeBindingsManifest drifted;
	TestTrue(TEXT("drifted manifest parses"), FProsettaNativeBindingsManifest::Parse(ManifestJSON(sha, 3, TEXT("{\"moduleName\":\"Missing\",\"languages\":{\"cpp\":{\"status\":\"shipped\",\"scriptId\":1,\"sourceSha256\":\"") + sha + TEXT("\",\"functions\":[{\"name\":\"missing_fn\",\"arity\":0,\"lookaheadSafe\":false}],\"errorCount\":0,\"warningCount\":0,\"diagnostics\":[],\"updatedAt\":\"2026-10-01T00:00:00.000Z\",\"artifactKey\":\"native-binding:cpp/Missing\",\"relativePath\":\"native-bindings/cpp/Missing.cpp\",\"byteSize\":10}}},")), drifted, error));
	audit = UInkpotNativeBindingLibrary::AuditRegistryAgainstManifest(drifted);
	TestFalse(TEXT("drifted audit fails"), audit.bPassed);
	TestTrue(TEXT("arity mismatch found"), audit.Findings.ContainsByPredicate([](const FInkpotNativeBindingAuditFinding& F) { return F.Code == TEXT("arity_mismatch") && F.FunctionName == TEXT("inkpot_test_add"); }));
	TestTrue(TEXT("missing module found"), audit.Findings.ContainsByPredicate([](const FInkpotNativeBindingAuditFinding& F) { return F.Code == TEXT("module_not_registered") && F.ModuleName == TEXT("Missing"); }));
	TestTrue(TEXT("errors sorted first"), audit.Findings.Num() > 0 && audit.Findings[0].Severity == EInkpotNativeBindingAuditSeverity::Error);
	TestTrue(TEXT("audit text"), audit.ToString().Contains(TEXT("FAILED")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotNativeBindingScannerTest, "Inkpot.Prosetta.NativeBindings.Scanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotNativeBindingScannerTest::RunTest(const FString& InParameters)
{
	// A local module ( no provenance header ).
	{
		const FInkpotNativeBindingSourceModule module = FInkpotNativeBindingSourceScanner::Scan(ScaffoldSource);
		TestTrue(TEXT("valid"), module.IsValid());
		TestEqual(TEXT("module name from namespace"), module.ModuleName, FString(TEXT("Inventory")));
		TestFalse(TEXT("no provenance"), module.Provenance.bPresent);
		TestEqual(TEXT("sha of whole file"), module.AuthoredSha256, FInkpotNativeBindingSourceScanner::Sha256(ScaffoldSource));
		if (TestEqual(TEXT("three functions"), module.Functions.Num(), 3))
		{
			const FInkpotNativeBindingSourceFunction& describe = module.Functions[0];
			TestEqual(TEXT("describe name"), describe.Name, FString(TEXT("describe_item")));
			TestEqual(TEXT("describe return"), describe.ReturnType, FString(TEXT("const char*")));
			TestEqual(TEXT("describe arity"), describe.Arity(), 2);
			TestEqual(TEXT("describe param type"), describe.Parameters[0].Type, FString(TEXT("const char*")));
			TestEqual(TEXT("describe param name"), describe.Parameters[0].Name, FString(TEXT("item_name")));
			TestEqual(TEXT("describe signature"), describe.Signature(), FString(TEXT("const char* describe_item(const char* item_name, int quantity)")));
			TestTrue(TEXT("describe is a stub"), describe.bStub);
			TestFalse(TEXT("describe not lookahead safe"), describe.bLookaheadSafe);
			TestEqual(TEXT("describe line"), describe.Line, 6);
			const FInkpotNativeBindingSourceFunction& chime = module.Functions[1];
			TestEqual(TEXT("chime signature"), chime.Signature(), FString(TEXT("void play_chime()")));
			TestTrue(TEXT("chime lookahead safe"), chime.bLookaheadSafe);
			TestTrue(TEXT("chime empty stub"), chime.bStub);
			const FInkpotNativeBindingSourceFunction& scale = module.Functions[2];
			TestEqual(TEXT("scale signature"), scale.Signature(), FString(TEXT("float scale_price(float price, int count)")));
			TestFalse(TEXT("scale implemented"), scale.bStub);
		}
	}

	// A shipped module with the Prosetta provenance header.
	{
		const FString shipped = Shipped(ScaffoldSource, TEXT("Inventory"));
		const FInkpotNativeBindingSourceModule module = FInkpotNativeBindingSourceScanner::Scan(shipped);
		TestTrue(TEXT("shipped valid"), module.IsValid());
		TestTrue(TEXT("provenance present"), module.Provenance.bPresent);
		TestEqual(TEXT("provenance module"), module.Provenance.ModuleName, FString(TEXT("Inventory")));
		TestEqual(TEXT("provenance project"), module.Provenance.ProjectId, 42);
		TestEqual(TEXT("provenance script"), module.Provenance.ScriptId, 12);
		TestEqual(TEXT("authored sha matches header"), module.AuthoredSha256, module.Provenance.SourceSha256);
		TestEqual(TEXT("lines offset by header"), module.Functions.Num() == 3 ? module.Functions[0].Line : -1, 10);

		const FInkpotNativeBindingSourceModule edited = FInkpotNativeBindingSourceScanner::Scan(shipped + TEXT("\n// edited after sync\n"));
		TestFalse(TEXT("edited shipped file invalid"), edited.IsValid());
		TestTrue(TEXT("sha mismatch diagnosed"), edited.Diagnostics.ContainsByPredicate([](const FInkpotNativeBindingSourceDiagnostic& D) { return D.Code == TEXT("provenance-sha-mismatch"); }));

		const FInkpotNativeBindingSourceModule renamed = FInkpotNativeBindingSourceScanner::Scan(shipped, TEXT("Other"));
		TestTrue(TEXT("expected name mismatch diagnosed"), renamed.Diagnostics.ContainsByPredicate([](const FInkpotNativeBindingSourceDiagnostic& D) { return D.Code == TEXT("provenance-module"); }));
	}

	// Rule violations, each with its Prosetta diagnostic code.
	auto codes = [](const FString& Source, const FString& Expected = FString()) -> TArray<FString>
	{
		TArray<FString> result;
		for (const FInkpotNativeBindingSourceDiagnostic& diagnostic : FInkpotNativeBindingSourceScanner::Scan(Source, Expected).Diagnostics)
			result.Add(diagnostic.Code);
		return result;
	};
	TestTrue(TEXT("wrong namespace"), codes(TEXT("namespace Prosetta::Bindings::Other { int F() { return 1; } }"), TEXT("M")).Contains(TEXT("cpp-namespace")));
	TestTrue(TEXT("missing namespace"), codes(TEXT("int F() { return 1; }")).Contains(TEXT("cpp-namespace-missing")));
	TestTrue(TEXT("unbalanced"), codes(TEXT("namespace Prosetta::Bindings::M {"), TEXT("M")).Contains(TEXT("cpp-braces")));
	TestTrue(TEXT("engine header"), codes(TEXT("#include \"CoreMinimal.h\"\nnamespace Prosetta::Bindings::M { void F() {} }")).Contains(TEXT("cpp-engine-header")));
	TestTrue(TEXT("engine header in angle brackets"), codes(TEXT("#include <Engine/World.h>\nnamespace Prosetta::Bindings::M { void F() {} }")).Contains(TEXT("cpp-engine-header")));
	TestFalse(TEXT("standard header allowed"), codes(TEXT("#include <cstring>\nnamespace Prosetta::Bindings::M { void F() {} }")).Contains(TEXT("cpp-engine-header")));
	TestTrue(TEXT("return type"), codes(TEXT("namespace Prosetta::Bindings::M { double F() { return 1.0; } }")).Contains(TEXT("cpp-return-type")));
	TestTrue(TEXT("parameter type"), codes(TEXT("namespace Prosetta::Bindings::M { void F(double x) {} }")).Contains(TEXT("cpp-parameter-type")));
	TestTrue(TEXT("variadic"), codes(TEXT("namespace Prosetta::Bindings::M { void F(int a, ...) {} }")).Contains(TEXT("cpp-variadic")));
	TestTrue(TEXT("default parameter"), codes(TEXT("namespace Prosetta::Bindings::M { void F(int a = 1) {} }")).Contains(TEXT("cpp-default-parameter")));
	TestTrue(TEXT("overload"), codes(TEXT("namespace Prosetta::Bindings::M { void F(int a) {} void F(bool b) {} }")).Contains(TEXT("cpp-overload")));
	TestTrue(TEXT("entry point"), codes(TEXT("int main() { return 0; }\nnamespace Prosetta::Bindings::M { void F() {} }")).Contains(TEXT("cpp-entry-point")));
	TestTrue(TEXT("no functions"), codes(TEXT("namespace Prosetta::Bindings::M { }")).Contains(TEXT("cpp-no-functions")));
	TestTrue(TEXT("no functions is only a warning"), FInkpotNativeBindingSourceScanner::Scan(TEXT("namespace Prosetta::Bindings::M { }")).IsValid());

	// Nested blocks, lambdas and comments do not produce functions.
	{
		const FInkpotNativeBindingSourceModule module = FInkpotNativeBindingSourceScanner::Scan(TEXT(
			"namespace Prosetta::Bindings::M {\n"
			"    /* int commented_out(int x) { return x; } */\n"
			"    int outer(int x) {\n"
			"        auto inner = [](int y) { return y; };\n"
			"        if (x) { return inner(x); }\n"
			"        return 0;\n"
			"    }\n"
			"}\n"));
		TestTrue(TEXT("nested valid"), module.IsValid());
		TestEqual(TEXT("only outer function"), module.Functions.Num(), 1);
		TestEqual(TEXT("outer name"), module.Functions.Num() == 1 ? module.Functions[0].Name : FString(), FString(TEXT("outer")));
	}

	TestTrue(TEXT("module name valid"), FInkpotNativeBindingSourceScanner::IsValidModuleName(TEXT("_9lives")));
	TestFalse(TEXT("module name invalid"), FInkpotNativeBindingSourceScanner::IsValidModuleName(TEXT("9lives")));
	TestFalse(TEXT("module name with dash invalid"), FInkpotNativeBindingSourceScanner::IsValidModuleName(TEXT("my-module")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInkpotNativeBindingGeneratorTest, "Inkpot.Prosetta.NativeBindings.Generator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInkpotNativeBindingGeneratorTest::RunTest(const FString& InParameters)
{
	const FString directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NativeBindingGeneratorTest"));
	IFileManager::Get().DeleteDirectory(*directory, false, true);
	IFileManager::Get().MakeDirectory(*(directory / TEXT("cpp")), true);

	const FString inventoryAuthored = ScaffoldSource;
	const FString inventoryFile = directory / TEXT("cpp/Inventory.cpp");
	FFileHelper::SaveStringToFile(Shipped(inventoryAuthored, TEXT("Inventory")), *inventoryFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const FString cheatsFile = directory / TEXT("Cheats.cpp");
	FFileHelper::SaveStringToFile(TEXT("namespace Prosetta::Bindings::Cheats {\n    // @lookaheadSafe\n    bool god_mode(bool enabled) { return enabled; }\n}\n"), *cheatsFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(TEXT("// unrelated\nvoid NotAModule() {}\n"), *(directory / TEXT("Unrelated.cpp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	const TArray<FString> discovered = FInkpotNativeBindingGenerator::DiscoverModuleFiles(directory);
	TestEqual(TEXT("two modules discovered"), discovered.Num(), 2);

	const FString inventorySha = FInkpotNativeBindingSourceScanner::Sha256(inventoryAuthored);
	const FString manifestJSON = FString::Printf(TEXT(
		"{\"formatVersion\":1,\"projectId\":42,\"projectName\":\"Sandbox\",\"engineTargets\":[\"web\",\"unreal\"],"
		"\"externals\":[{\"name\":\"describe_item\",\"arity\":2,\"params\":[\"item_name\",\"quantity\"],\"declaredIn\":[]},{\"name\":\"play_chime\",\"arity\":0,\"params\":[],\"declaredIn\":[]},{\"name\":\"scale_price\",\"arity\":2,\"params\":[\"price\",\"count\"],\"declaredIn\":[]}],"
		"\"modules\":[{\"moduleName\":\"Inventory\",\"languages\":{\"cpp\":{\"status\":\"shipped\",\"scriptId\":12,\"sourceSha256\":\"%s\",\"functions\":["
		"{\"name\":\"describe_item\",\"arity\":2,\"lookaheadSafe\":true},{\"name\":\"play_chime\",\"arity\":0,\"lookaheadSafe\":true},{\"name\":\"scale_price\",\"arity\":2,\"lookaheadSafe\":false}],"
		"\"errorCount\":0,\"warningCount\":0,\"diagnostics\":[],\"updatedAt\":\"2026-10-01T00:00:00.000Z\",\"artifactKey\":\"native-binding:cpp/Inventory\",\"relativePath\":\"native-bindings/cpp/Inventory.cpp\",\"byteSize\":%d}}},"
		"{\"moduleName\":\"Broken\",\"languages\":{\"cpp\":{\"status\":\"excluded\",\"reason\":\"validation_errors\",\"scriptId\":13,\"sourceSha256\":\"%s\",\"functions\":[],\"errorCount\":1,\"warningCount\":0,\"diagnostics\":[],\"updatedAt\":\"2026-10-01T00:00:00.000Z\"}}},"
		"{\"moduleName\":\"NotImported\",\"languages\":{\"cpp\":{\"status\":\"shipped\",\"scriptId\":14,\"sourceSha256\":\"%s\",\"functions\":[{\"name\":\"later\",\"arity\":0,\"lookaheadSafe\":false}],\"errorCount\":0,\"warningCount\":0,\"diagnostics\":[],\"updatedAt\":\"2026-10-01T00:00:00.000Z\",\"artifactKey\":\"native-binding:cpp/NotImported\",\"relativePath\":\"native-bindings/cpp/NotImported.cpp\",\"byteSize\":1}}}],"
		"\"coverage\":{\"unbound\":[],\"byLanguage\":{\"cpp\":{\"bound\":3,\"missing\":0}}}}"), *inventorySha, inventoryAuthored.Len(), *inventorySha, *inventorySha);
	FProsettaNativeBindingsManifest manifest;
	FString error;
	if (!TestTrue(TEXT("generator manifest parses"), FProsettaNativeBindingsManifest::Parse(manifestJSON, manifest, error))) { AddError(error); return false; }

	// Dry run reports both loaders as out of date and writes nothing.
	FInkpotNativeBindingGenerationReport dry = FInkpotNativeBindingGenerator::Run(directory, &manifest, false);
	TestTrue(TEXT("dry run ok"), dry.bOk);
	TestEqual(TEXT("dry run modules"), dry.Modules.Num(), 2);
	TestEqual(TEXT("dry run loaders"), dry.Loaders.Num(), 2);
	TestEqual(TEXT("dry run writes nothing"), dry.NumWritten(), 0);
	TestTrue(TEXT("manifest module without source is a warning"), dry.Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("NotImported")); }));
	TestTrue(TEXT("local module not in manifest is a warning"), dry.Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("Cheats")); }));
	TestTrue(TEXT("lookahead difference is a warning"), dry.Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("describe_item")) && W.Contains(TEXT("lookaheadSafe")); }));
	TestFalse(TEXT("no loader written on disk"), FPaths::FileExists(FInkpotNativeBindingGenerator::LoaderFileFor(inventoryFile)));

	// Write.
	FInkpotNativeBindingGenerationReport written = FInkpotNativeBindingGenerator::Run(directory, &manifest, true);
	TestTrue(TEXT("write ok"), written.bOk);
	TestEqual(TEXT("two loaders written"), written.NumWritten(), 2);
	FString inventoryLoader;
	TestTrue(TEXT("inventory loader exists"), FFileHelper::LoadFileToString(inventoryLoader, *FInkpotNativeBindingGenerator::LoaderFileFor(inventoryFile)));
	TestTrue(TEXT("loader includes umbrella header"), inventoryLoader.Contains(TEXT("#include \"Inkpot/NativeBindings/InkpotNativeBinding.h\"")));
	TestTrue(TEXT("loader forward declares"), inventoryLoader.Contains(TEXT("\tconst char* describe_item(const char* item_name, int quantity);")));
	TestTrue(TEXT("loader namespace"), inventoryLoader.Contains(TEXT("namespace Prosetta::Bindings::Inventory\n{")));
	TestTrue(TEXT("loader is prosetta sourced with sha"), inventoryLoader.Contains(TEXT("INKPOT_NATIVE_BINDING_MODULE_BEGIN(Inventory, EInkpotNativeBindingSource::Prosetta, \"") + inventorySha + TEXT("\")")));
	TestTrue(TEXT("manifest lookahead wins"), inventoryLoader.Contains(TEXT("INKPOT_NATIVE_BINDING(describe_item, true)")));
	TestTrue(TEXT("source lookahead kept for others"), inventoryLoader.Contains(TEXT("INKPOT_NATIVE_BINDING(play_chime, true)")) && inventoryLoader.Contains(TEXT("INKPOT_NATIVE_BINDING(scale_price, false)")));
	TestTrue(TEXT("loader end"), inventoryLoader.Contains(TEXT("INKPOT_NATIVE_BINDING_MODULE_END(Inventory)")));
	FString cheatsLoader;
	TestTrue(TEXT("cheats loader exists"), FFileHelper::LoadFileToString(cheatsLoader, *FInkpotNativeBindingGenerator::LoaderFileFor(cheatsFile)));
	TestTrue(TEXT("local loader has no sha"), cheatsLoader.Contains(TEXT("INKPOT_NATIVE_BINDING_MODULE_BEGIN(Cheats, EInkpotNativeBindingSource::Local, \"\")")));
	TestTrue(TEXT("local lookahead from annotation"), cheatsLoader.Contains(TEXT("INKPOT_NATIVE_BINDING(god_mode, true)")));

	// Deterministic: a second run changes nothing and loaders are not rediscovered as modules.
	FInkpotNativeBindingGenerationReport again = FInkpotNativeBindingGenerator::Run(directory, &manifest, true);
	TestTrue(TEXT("second run ok"), again.bOk);
	TestEqual(TEXT("second run writes nothing"), again.NumWritten(), 0);
	TestEqual(TEXT("loaders not treated as modules"), again.Modules.Num(), 2);
	TestEqual(TEXT("generated bytes stable"), FInkpotNativeBindingGenerator::GenerateLoader(again.Modules[0], EInkpotNativeBindingSource::Local, nullptr), FInkpotNativeBindingGenerator::GenerateLoader(again.Modules[0], EInkpotNativeBindingSource::Local, nullptr));

	// Without a manifest every module is local unless it carries a provenance header.
	FInkpotNativeBindingGenerationReport unmanaged = FInkpotNativeBindingGenerator::Run(directory, nullptr, false);
	TestTrue(TEXT("unmanaged ok"), unmanaged.bOk);
	TestFalse(TEXT("no manifest used"), unmanaged.bManifestUsed);
	const FInkpotNativeBindingGeneratedLoader* inventoryUnmanaged = unmanaged.Loaders.FindByPredicate([](const FInkpotNativeBindingGeneratedLoader& L) { return L.ModuleName == TEXT("Inventory"); });
	TestTrue(TEXT("provenance header still classifies as prosetta"), inventoryUnmanaged && inventoryUnmanaged->Source == EInkpotNativeBindingSource::Prosetta);

	// A module Prosetta excludes, or whose bytes differ from the manifest, is an error and gets no loader.
	FFileHelper::SaveStringToFile(Shipped(TEXT("namespace Prosetta::Bindings::Broken { void broken() {} }\n"), TEXT("Broken"), 13), *(directory / TEXT("cpp/Broken.cpp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(Shipped(inventoryAuthored + TEXT("// drift\n"), TEXT("Inventory")), *inventoryFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FInkpotNativeBindingGenerationReport blocked = FInkpotNativeBindingGenerator::Run(directory, &manifest, true);
	TestFalse(TEXT("blocked run fails"), blocked.bOk);
	TestTrue(TEXT("excluded module error"), blocked.Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("Broken")) && E.Contains(TEXT("excludes")); }));
	TestTrue(TEXT("stale bytes error"), blocked.Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("Inventory")) && E.Contains(TEXT("sha256")); }));
	TestFalse(TEXT("no loader for excluded module"), FPaths::FileExists(FInkpotNativeBindingGenerator::LoaderFileFor(directory / TEXT("cpp/Broken.cpp"))));
	TestEqual(TEXT("only the local module gets a loader"), blocked.Loaders.Num(), 1);

	IFileManager::Get().DeleteDirectory(*directory, false, true);
	return true;
}
#endif
