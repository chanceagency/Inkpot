#pragma once
#include "CoreMinimal.h"
#include "ProsettaSyncProfile.h"

class FJsonObject;

/**
 * The tracked profile files under Config/ProsettaSync/Profiles. Writes go through source control when
 * a provider is enabled, and any JSON with a token-like key is refused so a secret can never be
 * committed by this path. Game thread only.
 */
class PROSETTADASHBOARD_API FProsettaProfileStore
{
public:
	explicit FProsettaProfileStore(FString InDirectory);
	~FProsettaProfileStore();
	UE_NONCOPYABLE(FProsettaProfileStore);

	/** <Project>/Config/ProsettaSync/Profiles. */
	static FString DefaultDirectory();
	const FString& GetDirectory() const { return Directory; }
	FString PathFor(const FString& Name) const;
	/** Letters, digits, space, '-', '_' and '.'; no path separators. */
	static bool IsValidName(const FString& Name);

	/** Profile names, sorted, from the *.json files present. */
	TArray<FString> List() const;
	bool Load(const FString& Name, FProsettaSyncProfile& Out, FString& OutError) const;
	/** Writes Profile under Profile.Name. */
	bool Save(const FProsettaSyncProfile& Profile, FString& OutError);
	/** Writes a copy under NewName (Profile.Name is replaced). */
	bool SaveAs(FProsettaSyncProfile Profile, const FString& NewName, FString& OutError);
	bool Delete(const FString& Name, FString& OutError);
	/** Loads DefaultName, creating it once when no profile file exists at all. */
	FProsettaSyncProfile EnsureDefault(const FString& DefaultName = TEXT("Default"));
	/** True when Edited differs from what its file holds (or the file is missing). */
	bool IsDirty(const FProsettaSyncProfile& Edited) const;

	/** Any key named like a credential (token, secret, password, apiKey…) at any depth. */
	static bool ContainsSecretKey(const TSharedPtr<FJsonObject>& Json, FString& OutKey);
	static bool ContainsSecretKey(const FString& Json, FString& OutKey);

	/** Refreshes OnListChanged when files change under the directory (editor only; not used by tests). */
	void WatchDirectory();
	/** Source control is only touched for files inside the project and outside Saved/; tests turn it off entirely. */
	void SetUseSourceControl(bool bEnabled) { bUseSourceControl = bEnabled; }
	FSimpleMulticastDelegate OnListChanged;

private:
	bool WriteFile(const FString& Path, const FString& Contents, FString& OutError);
	bool ShouldUseSourceControl(const FString& Path) const;
	FString Directory;
	bool bUseSourceControl = true;
	FDelegateHandle WatchHandle;
};
