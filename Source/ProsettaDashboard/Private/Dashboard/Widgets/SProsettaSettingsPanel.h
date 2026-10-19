#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ProsettaDashboardModel.h"

class IStructureDetailsView;
class SVerticalBox;
class SExpandableArea;
class FStructOnScope;
struct FPropertyChangedEvent;
template <typename T> class SComboBox;

/**
 * Named profiles, the mode switch, the scope editor (channel + volume checkboxes) and a structure
 * details view over the remaining profile fields. Every edit goes through IProsettaDashboardModel::SetProfile.
 */
class PROSETTADASHBOARD_API SProsettaSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaSettingsPanel) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel);
	virtual ~SProsettaSettingsPanel() override;

	/** Label of the profile combo: the active name, with " •" while unsaved. */
	FText ProfileLabel() const;
	/** "Start watch" / "Sync now" / "Verify" by the profile's mode. */
	FText RunLabel() const;
	bool CanRun() const;
	/** Explains why Watch was refused (fixed tag) or empty. */
	FText ModeHint() const { return ModeHintText; }
	void SetMode(EProsettaSyncMode Mode);
	void SetChannel(EProsettaWatchChannel Channel);
	/** Adds or removes Slug from the profile's scope (single volume under latest-release). */
	void SetVolumeChecked(const FString& Slug, bool bChecked);
	void SetVolumeTag(const FString& Slug, const FString& Tag);
	/** Save as a new profile (used by the Save As dialog and tests). */
	bool SaveProfileAs(const FString& NewName, FString& OutError);
	/** Volume rows currently drawn (slug -> checked). */
	TMap<FString, bool> GetVolumeRows() const;
	/** The struct the details view edits; stays equal to the model's profile between edits. */
	const FProsettaSyncProfile* GetEditedProfile() const;

private:
	void HandleStateChanged();
	void RebuildProfileOptions();
	void RebuildVolumeRows();
	void SyncDetailsFromState();
	void HandleFinishedChangingProperties(const FPropertyChangedEvent& Event);
	void PromptSaveAs();
	void ConfirmDelete();
	FProsettaSyncProfile* EditedProfile() const;

	TSharedPtr<IProsettaDashboardModel> Model;
	FDelegateHandle StateHandle;
	TArray<TSharedPtr<FString>> ProfileOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ProfileCombo;
	TSharedPtr<SVerticalBox> VolumeRowsBox;
	TSharedPtr<SExpandableArea> ScopeArea;
	TSharedPtr<IStructureDetailsView> DetailsView;
	TSharedPtr<FStructOnScope> StructData;
	TArray<FString> DrawnVolumeSlugs;
	/** What the drawn rows show; rows are rebuilt only when it changes, so a tag being typed survives polls. */
	FString VolumeRowsSignature;
	FText ModeHintText;
	bool bApplyingEdit = false;
};
