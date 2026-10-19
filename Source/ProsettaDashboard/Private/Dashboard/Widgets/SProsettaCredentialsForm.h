#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IProsettaDashboardModel;
class SEditableTextBox;
class STextBlock;

/**
 * Base URL / project id / token form. The token lives only in the password box until Submit() moves it
 * into IProsettaDashboardModel::WriteCredentials (`init --token-stdin`); it is never logged or echoed.
 */
class PROSETTADASHBOARD_API SProsettaCredentialsForm : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaCredentialsForm) {}
		SLATE_EVENT(FSimpleDelegate, OnSubmitted)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel);

	static constexpr const TCHAR* LocalDevUrl = TEXT("http://localhost:3000");
	static constexpr const TCHAR* StagingRelayUrl = TEXT("http://127.0.0.1:61417");

	/** http(s) URL, a project id and a token are all present. */
	bool IsValid() const;
	/** The redacted command line that will run (`init --base-url … --project-id … --token-stdin`). */
	FString CommandPreview() const;
	/** Writes the credentials through the model and clears the token box; false when invalid. */
	bool Submit();
	static bool IsValidBaseUrl(const FString& Url);
	/** Where the staging relay keeps its state file; its presence is shown next to the preset. */
	static FString RelayStatePath();

	void SetBaseUrl(const FString& Url);
	void SetProjectId(const FString& Id);
	void SetToken(const FString& Token);
	FString GetBaseUrl() const;
	FString GetProjectId() const;
	bool HasToken() const;

private:
	FText PresetNote() const;
	TSharedPtr<IProsettaDashboardModel> Model;
	TSharedPtr<SEditableTextBox> BaseUrlBox;
	TSharedPtr<SEditableTextBox> ProjectIdBox;
	TSharedPtr<SEditableTextBox> TokenBox;
	FSimpleDelegate OnSubmitted;
};

/** Modal wrapper around SProsettaCredentialsForm; skipped (returns false) when no modal can be shown. */
struct PROSETTADASHBOARD_API FProsettaCredentialsDialog
{
	static bool Show(TSharedRef<IProsettaDashboardModel> Model);
};
