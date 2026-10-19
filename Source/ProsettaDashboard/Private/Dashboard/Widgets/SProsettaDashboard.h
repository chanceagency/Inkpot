#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ProsettaDashboardModel.h"

class SWidgetSwitcher;
class SProsettaConnectionHeader;
class SProsettaSettingsPanel;
class SProsettaVolumesPanel;
class SProsettaStoryDebugPanel;
class SProsettaChangesLog;

/**
 * Root widget of the Prosetta Dashboard tab: a first-run checklist until a CLI, credentials and a
 * scope exist, then the connection header, banners, settings | volumes + story debug, and the log.
 */
class PROSETTADASHBOARD_API SProsettaDashboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaDashboard) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel);
	virtual ~SProsettaDashboard() override;

	/** 0 = first-run checklist, 1 = the dashboard. */
	int32 ActivePage() const;
	TSharedPtr<SProsettaSettingsPanel> GetSettingsPanel() const { return SettingsPanel; }
	TSharedPtr<SProsettaVolumesPanel> GetVolumesPanel() const { return VolumesPanel; }
	TSharedPtr<SProsettaChangesLog> GetChangesLog() const { return ChangesLog; }
	TSharedPtr<SProsettaConnectionHeader> GetHeader() const { return Header; }

	/** Checklist item texts, for the first-run page and tests. */
	FText CliItemText() const;
	FText CredentialsItemText() const;
	FText ScopeItemText() const;
	bool HasCli() const;
	bool HasCredentials() const;
	bool HasScope() const;
	FText ErrorBannerText() const;

private:
	void HandleStateChanged();
	TSharedRef<SWidget> MakeFirstRunPage();
	TSharedRef<SWidget> MakeDashboardPage();
	TSharedRef<SWidget> MakeChecklistRow(const FName& Tag, TAttribute<bool> bDone, TAttribute<FText> Text);
	void ConnectFromFirstRun();

	TSharedPtr<IProsettaDashboardModel> Model;
	FDelegateHandle StateHandle;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SProsettaConnectionHeader> Header;
	TSharedPtr<SProsettaSettingsPanel> SettingsPanel;
	TSharedPtr<SProsettaVolumesPanel> VolumesPanel;
	TSharedPtr<SProsettaStoryDebugPanel> StoryDebugPanel;
	TSharedPtr<SProsettaChangesLog> ChangesLog;
};
