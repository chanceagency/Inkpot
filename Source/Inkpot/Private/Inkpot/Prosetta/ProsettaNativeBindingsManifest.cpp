#include "Inkpot/Prosetta/ProsettaNativeBindingsManifest.h"
#include "Inkpot/Prosetta/ProsettaSyncedArtifact.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

namespace
{
	bool ReadInt(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField, int32& OutValue, bool bRequired, FString& OutError)
	{
		double value = 0;
		if (!InObject->TryGetNumberField(InField, value))
		{
			if (bRequired)
			{
				OutError = FString::Printf(TEXT("Missing numeric field '%s'."), InField);
				return false;
			}
			return true;
		}
		OutValue = static_cast<int32>(value);
		return true;
	}

	bool ParseFunctions(const TSharedPtr<FJsonObject>& InEntry, TArray<FProsettaNativeBindingFunction>& OutFunctions, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* functions = nullptr;
		if (!InEntry->TryGetArrayField(TEXT("functions"), functions))
		{
			OutError = TEXT("Language entry is missing 'functions'.");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& value : *functions)
		{
			const TSharedPtr<FJsonObject>* object = nullptr;
			FProsettaNativeBindingFunction function;
			if (!value->TryGetObject(object) || !(*object)->TryGetStringField(TEXT("name"), function.Name) || function.Name.IsEmpty() ||
				!ReadInt(*object, TEXT("arity"), function.Arity, true, OutError))
			{
				if (OutError.IsEmpty()) OutError = TEXT("Invalid function entry.");
				return false;
			}
			(*object)->TryGetBoolField(TEXT("lookaheadSafe"), function.bLookaheadSafe);
			OutFunctions.Add(MoveTemp(function));
		}
		return true;
	}

	bool ParseLanguageEntry(const TSharedPtr<FJsonObject>& InEntry, FProsettaNativeBindingLanguageEntry& OutEntry, FString& OutError)
	{
		FString status;
		if (!InEntry->TryGetStringField(TEXT("status"), status))
		{
			OutError = TEXT("Language entry is missing 'status'.");
			return false;
		}
		if (status == TEXT("shipped"))
		{
			OutEntry.Status = EProsettaNativeBindingStatus::Shipped;
			if (!InEntry->TryGetStringField(TEXT("artifactKey"), OutEntry.ArtifactKey) || !InEntry->TryGetStringField(TEXT("relativePath"), OutEntry.RelativePath))
			{
				OutError = TEXT("Shipped entry is missing 'artifactKey' or 'relativePath'.");
				return false;
			}
			if (!ReadInt(InEntry, TEXT("byteSize"), OutEntry.ByteSize, false, OutError)) return false;
		}
		else if (status == TEXT("excluded"))
		{
			OutEntry.Status = EProsettaNativeBindingStatus::Excluded;
			InEntry->TryGetStringField(TEXT("reason"), OutEntry.Reason);
			if (OutEntry.Reason.IsEmpty()) OutEntry.Reason = TEXT("unknown");
		}
		else
		{
			// Statuses are a closed enum; anything else is treated as absent ( never served ).
			OutEntry.Status = EProsettaNativeBindingStatus::Excluded;
			OutEntry.Reason = TEXT("unknown_status:") + status;
		}
		if (!ReadInt(InEntry, TEXT("scriptId"), OutEntry.ScriptId, false, OutError)) return false;
		InEntry->TryGetStringField(TEXT("sourceSha256"), OutEntry.SourceSha256);
		OutEntry.SourceSha256.ToLowerInline();
		if (!ParseFunctions(InEntry, OutEntry.Functions, OutError)) return false;
		if (!ReadInt(InEntry, TEXT("errorCount"), OutEntry.ErrorCount, false, OutError)) return false;
		if (!ReadInt(InEntry, TEXT("warningCount"), OutEntry.WarningCount, false, OutError)) return false;
		InEntry->TryGetStringField(TEXT("updatedAt"), OutEntry.UpdatedAt);
		const TArray<TSharedPtr<FJsonValue>>* diagnostics = nullptr;
		if (InEntry->TryGetArrayField(TEXT("diagnostics"), diagnostics))
		{
			for (const TSharedPtr<FJsonValue>& value : *diagnostics)
			{
				const TSharedPtr<FJsonObject>* object = nullptr;
				if (!value->TryGetObject(object)) continue;
				FProsettaNativeBindingDiagnostic diagnostic;
				(*object)->TryGetStringField(TEXT("severity"), diagnostic.Severity);
				(*object)->TryGetStringField(TEXT("message"), diagnostic.Message);
				(*object)->TryGetStringField(TEXT("code"), diagnostic.Code);
				double line = 0;
				if ((*object)->TryGetNumberField(TEXT("line"), line)) diagnostic.Line = static_cast<int32>(line);
				OutEntry.Diagnostics.Add(MoveTemp(diagnostic));
			}
		}
		return true;
	}
}

const FProsettaNativeBindingFunction* FProsettaNativeBindingLanguageEntry::FindFunction(const FString& InName) const
{
	return Functions.FindByPredicate([&InName](const FProsettaNativeBindingFunction& F) { return F.Name == InName; });
}

const FProsettaNativeBindingModule* FProsettaNativeBindingsManifest::FindModule(const FString& InModuleName) const
{
	// Prosetta groups siblings case-insensitively by module name.
	return Modules.FindByPredicate([&InModuleName](const FProsettaNativeBindingModule& M) { return M.ModuleName.Equals(InModuleName, ESearchCase::IgnoreCase); });
}

const FProsettaExternalDeclaration* FProsettaNativeBindingsManifest::FindExternal(const FString& InName) const
{
	return Externals.FindByPredicate([&InName](const FProsettaExternalDeclaration& E) { return E.Name == InName; });
}

TArray<const FProsettaNativeBindingModule*> FProsettaNativeBindingsManifest::GetShippedCppModules() const
{
	TArray<const FProsettaNativeBindingModule*> shipped;
	for (const FProsettaNativeBindingModule& module : Modules)
	{
		const FProsettaNativeBindingLanguageEntry* cpp = module.FindCpp();
		if (cpp && cpp->IsShipped())
			shipped.Add(&module);
	}
	shipped.Sort([](const FProsettaNativeBindingModule& A, const FProsettaNativeBindingModule& B) { return A.ModuleName < B.ModuleName; });
	return shipped;
}

bool FProsettaNativeBindingsManifest::Parse(const FString& InJSON, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	OutManifest = FProsettaNativeBindingsManifest();
	OutError.Reset();

	TSharedPtr<FJsonObject> root;
	const TSharedRef<TJsonReader<>> reader = TJsonReaderFactory<>::Create(InJSON);
	if (!FJsonSerializer::Deserialize(reader, root) || !root.IsValid())
	{
		OutError = TEXT("Native bindings manifest is not valid JSON.");
		return false;
	}

	FProsettaNativeBindingsManifest manifest;
	if (!ReadInt(root, TEXT("formatVersion"), manifest.FormatVersion, true, OutError)) return false;
	if (manifest.FormatVersion != 1)
	{
		OutError = FString::Printf(TEXT("Unsupported native bindings manifest formatVersion %d ( expected 1 )."), manifest.FormatVersion);
		return false;
	}
	if (!ReadInt(root, TEXT("projectId"), manifest.ProjectId, true, OutError)) return false;
	if (!root->TryGetStringField(TEXT("projectName"), manifest.ProjectName))
	{
		OutError = TEXT("Missing 'projectName'.");
		return false;
	}
	root->TryGetStringField(TEXT("generatedAt"), manifest.GeneratedAt);

	const TArray<TSharedPtr<FJsonValue>>* targets = nullptr;
	if (root->TryGetArrayField(TEXT("engineTargets"), targets))
	{
		for (const TSharedPtr<FJsonValue>& value : *targets)
		{
			FString target;
			if (value->TryGetString(target)) manifest.EngineTargets.Add(target);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* externals = nullptr;
	if (!root->TryGetArrayField(TEXT("externals"), externals))
	{
		OutError = TEXT("Missing 'externals'.");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& value : *externals)
	{
		const TSharedPtr<FJsonObject>* object = nullptr;
		FProsettaExternalDeclaration external;
		if (!value->TryGetObject(object) || !(*object)->TryGetStringField(TEXT("name"), external.Name) || external.Name.IsEmpty() ||
			!ReadInt(*object, TEXT("arity"), external.Arity, true, OutError))
		{
			if (OutError.IsEmpty()) OutError = TEXT("Invalid external declaration.");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* params = nullptr;
		if ((*object)->TryGetArrayField(TEXT("params"), params))
		{
			for (const TSharedPtr<FJsonValue>& param : *params)
			{
				FString name;
				if (param->TryGetString(name)) external.Params.Add(name);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* sites = nullptr;
		if ((*object)->TryGetArrayField(TEXT("declaredIn"), sites))
		{
			for (const TSharedPtr<FJsonValue>& siteValue : *sites)
			{
				const TSharedPtr<FJsonObject>* site = nullptr;
				if (!siteValue->TryGetObject(site)) continue;
				FProsettaExternalDeclarationSite declared;
				ReadInt(*site, TEXT("inkScriptId"), declared.InkScriptId, false, OutError);
				(*site)->TryGetStringField(TEXT("qualifiedName"), declared.QualifiedName);
				external.DeclaredIn.Add(MoveTemp(declared));
			}
		}
		manifest.Externals.Add(MoveTemp(external));
	}

	const TArray<TSharedPtr<FJsonValue>>* modules = nullptr;
	if (!root->TryGetArrayField(TEXT("modules"), modules))
	{
		OutError = TEXT("Missing 'modules'.");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& value : *modules)
	{
		const TSharedPtr<FJsonObject>* object = nullptr;
		const TSharedPtr<FJsonObject>* languages = nullptr;
		FProsettaNativeBindingModule module;
		if (!value->TryGetObject(object) || !(*object)->TryGetStringField(TEXT("moduleName"), module.ModuleName) || module.ModuleName.IsEmpty() ||
			!(*object)->TryGetObjectField(TEXT("languages"), languages))
		{
			OutError = TEXT("Invalid module entry.");
			return false;
		}
		for (const auto& pair : (*languages)->Values)
		{
			// JSON object keys are shared strings on 5.8; copy into an FString.
			const FString language(*pair.Key);
			const TSharedPtr<FJsonObject>* entryObject = nullptr;
			if (!pair.Value->TryGetObject(entryObject))
			{
				OutError = FString::Printf(TEXT("Invalid language entry '%s' in module '%s'."), *language, *module.ModuleName);
				return false;
			}
			FProsettaNativeBindingLanguageEntry entry;
			if (!ParseLanguageEntry(*entryObject, entry, OutError))
			{
				OutError = FString::Printf(TEXT("Module '%s' / %s: %s"), *module.ModuleName, *language, *OutError);
				return false;
			}
			module.Languages.Add(language, MoveTemp(entry));
		}
		manifest.Modules.Add(MoveTemp(module));
	}

	const TSharedPtr<FJsonObject>* coverage = nullptr;
	if (root->TryGetObjectField(TEXT("coverage"), coverage))
	{
		const TArray<TSharedPtr<FJsonValue>>* unbound = nullptr;
		if ((*coverage)->TryGetArrayField(TEXT("unbound"), unbound))
		{
			for (const TSharedPtr<FJsonValue>& value : *unbound)
			{
				FString name;
				if (value->TryGetString(name)) manifest.Unbound.Add(name);
			}
		}
		const TSharedPtr<FJsonObject>* byLanguage = nullptr;
		if ((*coverage)->TryGetObjectField(TEXT("byLanguage"), byLanguage))
		{
			for (const auto& pair : (*byLanguage)->Values)
			{
				const TSharedPtr<FJsonObject>* entry = nullptr;
				if (!pair.Value->TryGetObject(entry)) continue;
				FProsettaNativeBindingCoverage counts;
				ReadInt(*entry, TEXT("bound"), counts.Bound, false, OutError);
				ReadInt(*entry, TEXT("missing"), counts.Missing, false, OutError);
				manifest.CoverageByLanguage.Add(FString(*pair.Key), counts);
			}
		}
	}

	manifest.bValid = true;
	OutManifest = MoveTemp(manifest);
	return true;
}

bool FProsettaNativeBindingsManifest::LoadFile(const FString& InFilename, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	FString text;
	if (!FFileHelper::LoadFileToString(text, *InFilename))
	{
		OutManifest = FProsettaNativeBindingsManifest();
		OutError = FString::Printf(TEXT("Cannot read native bindings manifest '%s'."), *InFilename);
		return false;
	}
	return Parse(text, OutManifest, OutError);
}

bool FProsettaNativeBindingsManifest::LoadFromArtifact(const UProsettaSyncedArtifact* InArtifact, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	if (!InArtifact)
	{
		OutManifest = FProsettaNativeBindingsManifest();
		OutError = TEXT("No native bindings artifact.");
		return false;
	}
	return Parse(InArtifact->Text, OutManifest, OutError);
}

bool FProsettaNativeBindingsManifest::LoadFromGeneration(const UProsettaGenerationAsset* InGeneration, FProsettaNativeBindingsManifest& OutManifest, FString& OutError)
{
	if (!InGeneration)
	{
		OutManifest = FProsettaNativeBindingsManifest();
		OutError = TEXT("No Prosetta generation.");
		return false;
	}
	const TObjectPtr<UObject>* asset = InGeneration->Assets.Find(ArtifactKey());
	const UProsettaSyncedArtifact* artifact = asset ? Cast<UProsettaSyncedArtifact>(asset->Get()) : nullptr;
	if (!artifact)
	{
		OutManifest = FProsettaNativeBindingsManifest();
		OutError = TEXT("The generation has no native-bindings manifest artifact ( the project may not have game data scripts enabled ).");
		return false;
	}
	return LoadFromArtifact(artifact, OutManifest, OutError);
}
