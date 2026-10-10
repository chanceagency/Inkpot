#include "Inkpot/NativeBindings/InkpotNativeBindingRegistry.h"
#include "Utility/InkpotLog.h"

FInkpotNativeBindingModuleInfo FInkpotNativeBindingModule::ToInfo() const
{
	FInkpotNativeBindingModuleInfo info;
	info.Name = Name;
	info.Source = Source;
	info.SourceSha256 = SourceSha256;
	for (const FInkpotNativeFunction& function : Functions)
		info.Functions.Add(function.Descriptor);
	return info;
}

FInkpotNativeBindingRegistry& FInkpotNativeBindingRegistry::Get()
{
	// Never destroyed: module registrars unregister during static destruction in any order.
	static FInkpotNativeBindingRegistry* instance = new FInkpotNativeBindingRegistry();
	return *instance;
}

uint64 FInkpotNativeBindingRegistry::RegisterModule(FInkpotNativeBindingModule&& InModule)
{
	const FString name = InModule.Name;
	for (FInkpotNativeFunction& function : InModule.Functions)
		function.Descriptor.ModuleName = name;

	if (Modules.Contains(name))
		INKPOT_LOG("Native binding module '%s' re-registered ( %d functions ).", *name, InModule.Functions.Num());

	FEntry entry;
	entry.Token = NextToken++;
	entry.Module = MakeShared<FInkpotNativeBindingModule>(MoveTemp(InModule));
	const uint64 token = entry.Token;
	Modules.Add(name, MoveTemp(entry));
	OnChanged.Broadcast();
	return token;
}

void FInkpotNativeBindingRegistry::UnregisterModule(const FString& InModuleName, uint64 InToken)
{
	const FEntry* entry = Modules.Find(InModuleName);
	if (!entry || entry->Token != InToken)
		return;
	Modules.Remove(InModuleName);
	OnChanged.Broadcast();
}

const FInkpotNativeBindingModule* FInkpotNativeBindingRegistry::FindModule(const FString& InModuleName) const
{
	const FEntry* entry = Modules.Find(InModuleName);
	return entry ? entry->Module.Get() : nullptr;
}

const FInkpotNativeFunction* FInkpotNativeBindingRegistry::FindFunction(const FString& InFunctionName, const FInkpotNativeBindingModule** OutModule) const
{
	for (const FInkpotNativeBindingModule* module : GetModules())
	{
		for (const FInkpotNativeFunction& function : module->Functions)
		{
			if (function.Descriptor.Name == InFunctionName)
			{
				if (OutModule)
					*OutModule = module;
				return &function;
			}
		}
	}
	return nullptr;
}

TArray<const FInkpotNativeBindingModule*> FInkpotNativeBindingRegistry::GetModules() const
{
	TArray<const FInkpotNativeBindingModule*> modules;
	for (const auto& pair : Modules)
		modules.Add(pair.Value.Module.Get());
	modules.Sort([](const FInkpotNativeBindingModule& A, const FInkpotNativeBindingModule& B) { return A.Name < B.Name; });
	return modules;
}

TArray<FInkpotNativeBindingModuleInfo> FInkpotNativeBindingRegistry::GetModuleInfos() const
{
	TArray<FInkpotNativeBindingModuleInfo> infos;
	for (const FInkpotNativeBindingModule* module : GetModules())
		infos.Add(module->ToInfo());
	return infos;
}

TArray<FString> FInkpotNativeBindingRegistry::FindDuplicateFunctionNames() const
{
	TMap<FString, int32> counts;
	for (const FInkpotNativeBindingModule* module : GetModules())
		for (const FInkpotNativeFunction& function : module->Functions)
			counts.FindOrAdd(function.Descriptor.Name)++;
	TArray<FString> duplicates;
	for (const auto& pair : counts)
		if (pair.Value > 1)
			duplicates.Add(pair.Key);
	duplicates.Sort();
	return duplicates;
}
