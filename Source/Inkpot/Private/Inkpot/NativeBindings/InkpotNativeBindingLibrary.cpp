#include "Inkpot/NativeBindings/InkpotNativeBindingLibrary.h"
#include "Inkpot/NativeBindings/InkpotNativeBindingRegistry.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/Inkpot.h"
#include "Utility/InkpotLog.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"

void FInkpotNativeBindingAudit::Add(const FString& InCode, EInkpotNativeBindingAuditSeverity InSeverity, const FString& InModule, const FString& InFunction, const FString& InMessage)
{
	FInkpotNativeBindingAuditFinding finding;
	finding.Code = InCode;
	finding.Severity = InSeverity;
	finding.ModuleName = InModule;
	finding.FunctionName = InFunction;
	finding.Message = InMessage;
	Findings.Add(MoveTemp(finding));
}

void FInkpotNativeBindingAudit::Finalise()
{
	ErrorCount = 0;
	WarningCount = 0;
	for (const FInkpotNativeBindingAuditFinding& finding : Findings)
	{
		if (finding.Severity == EInkpotNativeBindingAuditSeverity::Error) ++ErrorCount;
		else if (finding.Severity == EInkpotNativeBindingAuditSeverity::Warning) ++WarningCount;
	}
	Findings.StableSort([](const FInkpotNativeBindingAuditFinding& A, const FInkpotNativeBindingAuditFinding& B)
	{
		if (A.Severity != B.Severity) return A.Severity > B.Severity;
		if (A.ModuleName != B.ModuleName) return A.ModuleName < B.ModuleName;
		return A.FunctionName < B.FunctionName;
	});
	bPassed = ErrorCount == 0;
}

FString FInkpotNativeBindingAudit::ToString() const
{
	FString result = FString::Printf(TEXT("Native binding audit: %s ( %d error(s), %d warning(s), %d finding(s) )"), bPassed ? TEXT("passed") : TEXT("FAILED"), ErrorCount, WarningCount, Findings.Num());
	for (const FInkpotNativeBindingAuditFinding& finding : Findings)
	{
		const TCHAR* severity = finding.Severity == EInkpotNativeBindingAuditSeverity::Error ? TEXT("error") : finding.Severity == EInkpotNativeBindingAuditSeverity::Warning ? TEXT("warning") : TEXT("info");
		result += FString::Printf(TEXT("\n  [%s] %s %s%s%s: %s"), severity, *finding.Code, *finding.ModuleName, finding.FunctionName.IsEmpty() ? TEXT("") : TEXT("::"), *finding.FunctionName, *finding.Message);
	}
	return result;
}

TArray<FInkpotNativeBindingModuleInfo> UInkpotNativeBindingLibrary::GetRegisteredModules()
{
	return FInkpotNativeBindingRegistry::Get().GetModuleInfos();
}

bool UInkpotNativeBindingLibrary::FindRegisteredFunction(const FString& FunctionName, FInkpotNativeFunctionDescriptor& OutFunction)
{
	const FInkpotNativeFunction* function = FInkpotNativeBindingRegistry::Get().FindFunction(FunctionName);
	if (!function)
		return false;
	OutFunction = function->Descriptor;
	return true;
}

TArray<FString> UInkpotNativeBindingLibrary::GetDuplicateFunctionNames()
{
	return FInkpotNativeBindingRegistry::Get().FindDuplicateFunctionNames();
}

FInkpotNativeBindingReport UInkpotNativeBindingLibrary::BindRegisteredNativeFunctions(UInkpotStory* Story)
{
	if (!IsValid(Story))
	{
		FInkpotNativeBindingReport report;
		report.Messages.Add(TEXT("No story."));
		return report;
	}
	return Story->BindRegisteredNativeFunctions(TArray<FString>(), false);
}

bool UInkpotNativeBindingLibrary::ParseManifest(const FString& JSON, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	return FProsettaNativeBindingsManifest::Parse(JSON, OutManifest, OutError);
}

bool UInkpotNativeBindingLibrary::LoadManifestFile(const FString& Filename, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	return FProsettaNativeBindingsManifest::LoadFile(Filename, OutManifest, OutError);
}

bool UInkpotNativeBindingLibrary::LoadManifestFromArtifact(const UProsettaSyncedArtifact* Artifact, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	return FProsettaNativeBindingsManifest::LoadFromArtifact(Artifact, OutManifest, OutError);
}

bool UInkpotNativeBindingLibrary::LoadManifestFromGeneration(const UProsettaGenerationAsset* Generation, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	return FProsettaNativeBindingsManifest::LoadFromGeneration(Generation, OutManifest, OutError);
}

FString UInkpotNativeBindingLibrary::GetDefaultMirrorManifestPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Prosetta") / FProsettaNativeBindingsManifest::RelativePath());
}

FInkpotNativeBindingAudit UInkpotNativeBindingLibrary::AuditRegistryAgainstManifest(const FProsettaNativeBindingsManifest& Manifest)
{
	using ESeverity = EInkpotNativeBindingAuditSeverity;
	FInkpotNativeBindingAudit audit;
	const FInkpotNativeBindingRegistry& registry = FInkpotNativeBindingRegistry::Get();

	if (!Manifest.bValid)
	{
		audit.Add(TEXT("manifest_invalid"), ESeverity::Error, FString(), FString(), TEXT("The manifest did not parse; nothing to audit against."));
		audit.Finalise();
		return audit;
	}

	for (const FString& duplicate : registry.FindDuplicateFunctionNames())
		audit.Add(TEXT("duplicate_function"), ESeverity::Error, FString(), duplicate, TEXT("Implemented by more than one registered module; Ink binds one name to one function."));

	// Manifest -> registry: every shipped C++ module must be present and identical.
	for (const FProsettaNativeBindingModule* module : Manifest.GetShippedCppModules())
	{
		const FProsettaNativeBindingLanguageEntry* cpp = module->FindCpp();
		const FInkpotNativeBindingModule* registered = registry.FindModule(module->ModuleName);
		if (!registered)
		{
			audit.Add(TEXT("module_not_registered"), ESeverity::Error, module->ModuleName, FString(),
				FString::Printf(TEXT("Shipped C++ module is not registered. Import %s and generate its loader ( Prosetta.NativeBindings.Generate ), then rebuild."), *cpp->RelativePath));
			continue;
		}
		if (!registered->SourceSha256.IsEmpty() && !cpp->SourceSha256.IsEmpty() && !registered->SourceSha256.Equals(cpp->SourceSha256, ESearchCase::IgnoreCase))
			audit.Add(TEXT("source_sha_mismatch"), ESeverity::Error, module->ModuleName, FString(),
				FString::Printf(TEXT("Registered source sha256 %s differs from the shipped %s; the compiled module is stale."), *registered->SourceSha256.Left(12), *cpp->SourceSha256.Left(12)));
		for (const FProsettaNativeBindingFunction& function : cpp->Functions)
		{
			const FInkpotNativeFunction* bound = registered->Functions.FindByPredicate([&function](const FInkpotNativeFunction& F) { return F.Descriptor.Name == function.Name; });
			if (!bound)
			{
				audit.Add(TEXT("function_missing"), ESeverity::Error, module->ModuleName, function.Name, TEXT("Listed in the manifest but not registered by the loader."));
				continue;
			}
			if (bound->Descriptor.Arity != function.Arity)
				audit.Add(TEXT("arity_mismatch"), ESeverity::Error, module->ModuleName, function.Name,
					FString::Printf(TEXT("Registered arity %d, manifest arity %d."), bound->Descriptor.Arity, function.Arity));
			if (bound->Descriptor.bLookaheadSafe != function.bLookaheadSafe)
				audit.Add(TEXT("lookahead_mismatch"), ESeverity::Warning, module->ModuleName, function.Name,
					FString::Printf(TEXT("Registered lookaheadSafe=%s, manifest lookaheadSafe=%s."), bound->Descriptor.bLookaheadSafe ? TEXT("true") : TEXT("false"), function.bLookaheadSafe ? TEXT("true") : TEXT("false")));
		}
		for (const FInkpotNativeFunction& function : registered->Functions)
			if (!cpp->FindFunction(function.Descriptor.Name))
				audit.Add(TEXT("function_extra"), ESeverity::Warning, module->ModuleName, function.Descriptor.Name, TEXT("Registered but absent from the manifest; the loader may be stale."));
	}

	// Registry -> manifest: Prosetta-sourced modules must still be shipped; local modules are noted.
	for (const FInkpotNativeBindingModule* registered : registry.GetModules())
	{
		const FProsettaNativeBindingModule* module = Manifest.FindModule(registered->Name);
		if (registered->Source != EInkpotNativeBindingSource::Prosetta)
		{
			if (!module)
				audit.Add(TEXT("local_module"), ESeverity::Info, registered->Name, FString(), TEXT("Local module; not in the Prosetta manifest, so it is not audited against it."));
			continue;
		}
		const FProsettaNativeBindingLanguageEntry* cpp = module ? module->FindCpp() : nullptr;
		if (!module || !cpp)
			audit.Add(TEXT("module_not_in_manifest"), ESeverity::Warning, registered->Name, FString(), TEXT("Registered as a Prosetta module but the manifest has no C++ entry for it; it may have been deleted in Prosetta."));
		else if (!cpp->IsShipped())
			audit.Add(TEXT("module_not_shipped"), ESeverity::Warning, registered->Name, FString(),
				FString::Printf(TEXT("The manifest excludes this module ( %s ); the registered copy is from an earlier sync."), *cpp->Reason));
	}

	// Externals the manifest reports as unimplemented: a local module may cover them.
	for (const FString& external : Manifest.Unbound)
	{
		const FInkpotNativeBindingModule* owner = nullptr;
		if (registry.FindFunction(external, &owner))
			audit.Add(TEXT("external_unbound"), ESeverity::Info, owner ? owner->Name : FString(), external, TEXT("No shipped Prosetta module implements this EXTERNAL; a registered module covers it."));
		else
			audit.Add(TEXT("external_unbound"), ESeverity::Warning, FString(), external, TEXT("No shipped Prosetta module and no registered module implements this EXTERNAL."));
	}

	audit.Finalise();
	return audit;
}

FInkpotNativeBindingAudit UInkpotNativeBindingLibrary::AuditStory(UInkpotStory* Story, const FProsettaNativeBindingsManifest& Manifest, bool bUseManifest)
{
	using ESeverity = EInkpotNativeBindingAuditSeverity;
	FInkpotNativeBindingAudit audit;
	if (!IsValid(Story) || !Story->IsValid())
	{
		audit.Add(TEXT("story_invalid"), ESeverity::Error, FString(), FString(), TEXT("No valid story."));
		audit.Finalise();
		return audit;
	}
	const bool bFallbacks = Story->GetAllowExternalFunctionFallbacks();
	for (const FInkpotExternalDeclaration& external : Story->GetDeclaredExternalFunctions())
	{
		if (!Story->IsExternalFunctionBound(external.Name))
		{
			if (bFallbacks && external.bHasFallback)
				audit.Add(TEXT("external_fallback"), ESeverity::Info, FString(), external.Name, TEXT("Unbound; the Ink fallback function will run."));
			else
				audit.Add(TEXT("external_unbound"), ESeverity::Error, FString(), external.Name, FString::Printf(TEXT("The story calls this EXTERNAL ( arity %d ) and nothing is bound; Continue will fail when it is reached."), external.Arity));
		}
		if (bUseManifest && Manifest.bValid)
		{
			const FProsettaExternalDeclaration* declared = Manifest.FindExternal(external.Name);
			if (declared && declared->Arity != external.Arity)
				audit.Add(TEXT("arity_mismatch"), ESeverity::Warning, FString(), external.Name, FString::Printf(TEXT("The compiled story calls with arity %d; the manifest declares %d. The story and the manifest may be from different syncs."), external.Arity, declared->Arity));
		}
	}
	if (bUseManifest && Manifest.bValid)
	{
		FInkpotNativeBindingAudit registryAudit = AuditRegistryAgainstManifest(Manifest);
		audit.Findings.Append(registryAudit.Findings);
	}
	audit.Finalise();
	return audit;
}

FString UInkpotNativeBindingLibrary::AuditToString(const FInkpotNativeBindingAudit& Audit)
{
	return Audit.ToString();
}

namespace
{
	FString DescribeFunction(const FInkpotNativeFunctionDescriptor& D)
	{
		return FString::Printf(TEXT("%s %s(%s)%s"), *D.ReturnType, *D.Name, *FString::Join(D.ParameterTypes, TEXT(", ")), D.bLookaheadSafe ? TEXT(" // @lookaheadSafe") : TEXT(""));
	}

	FAutoConsoleCommand ListCommand(TEXT("Prosetta.NativeBindings.List"), TEXT("List the native binding modules registered in this process."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const TArray<FInkpotNativeBindingModuleInfo> modules = UInkpotNativeBindingLibrary::GetRegisteredModules();
			UE_LOG(Inkpot, Display, TEXT("Prosetta native bindings: %d registered module(s)."), modules.Num());
			for (const FInkpotNativeBindingModuleInfo& module : modules)
			{
				UE_LOG(Inkpot, Display, TEXT("  %s ( %s%s )"), *module.Name, module.Source == EInkpotNativeBindingSource::Prosetta ? TEXT("prosetta") : TEXT("local"), module.SourceSha256.IsEmpty() ? TEXT("") : *(TEXT(", sha256 ") + module.SourceSha256.Left(12)));
				for (const FInkpotNativeFunctionDescriptor& function : module.Functions)
					UE_LOG(Inkpot, Display, TEXT("    %s"), *DescribeFunction(function));
			}
			for (const FString& duplicate : UInkpotNativeBindingLibrary::GetDuplicateFunctionNames())
				UE_LOG(Inkpot, Warning, TEXT("  duplicate function '%s' is implemented by more than one module."), *duplicate);
		}));

	FAutoConsoleCommand AuditCommand(TEXT("Prosetta.NativeBindings.Audit"),
		TEXT("Audit registered modules and running stories against a native-bindings manifest: Prosetta.NativeBindings.Audit [\"absolute manifest.json\"] ( default: Saved/Prosetta/native-bindings/manifest.json )."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString path = Args.Num() > 0 ? Args[0] : UInkpotNativeBindingLibrary::GetDefaultMirrorManifestPath();
			FProsettaNativeBindingsManifest manifest;
			FString error;
			if (!UInkpotNativeBindingLibrary::LoadManifestFile(path, manifest, error))
			{
				UE_LOG(Inkpot, Error, TEXT("Prosetta native bindings: %s"), *error);
				return;
			}
			UE_LOG(Inkpot, Display, TEXT("Prosetta native bindings: manifest %s ( project %d '%s', %d module(s), %d external(s), %d unbound )."), *path, manifest.ProjectId, *manifest.ProjectName, manifest.Modules.Num(), manifest.Externals.Num(), manifest.Unbound.Num());
			UE_LOG(Inkpot, Display, TEXT("%s"), *UInkpotNativeBindingLibrary::AuditRegistryAgainstManifest(manifest).ToString());
			if (UInkpot* inkpot = GEngine ? GEngine->GetEngineSubsystem<UInkpot>() : nullptr)
			{
				for (UInkpotStory* story : inkpot->GetStories())
				{
					if (!IsValid(story) || !story->IsValid()) continue;
					UE_LOG(Inkpot, Display, TEXT("[Inkpot %s] %s"), *story->GetLogContext(), *UInkpotNativeBindingLibrary::AuditStory(story, manifest, false).ToString());
				}
			}
		}));
}
