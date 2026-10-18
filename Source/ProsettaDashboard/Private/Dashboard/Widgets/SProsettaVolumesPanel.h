#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "ProsettaDashboardModel.h"

class SSearchBox;
template <typename T> class SListView;
class ITableRow;
class STableViewBase;

/** One row of the volumes table: a volume plus how it relates to the mirror and the profile. */
struct FProsettaVolumeRow
{
	FProsettaVolumeSummary Volume;
	bool bMirrored = false;
	FString MirroredTag;
	bool bCanonical = false;
	bool bInProfile = false;
	FString Badges() const;
};

/** The modal that confirms a volume switch; Perform() is the shared action for the dialog and tests. */
class PROSETTADASHBOARD_API FProsettaVolumeSwitchDialog
{
public:
	/** Body text + tag combo + re-point box, usable without a window (tests). */
	class SBody : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBody) {}
		SLATE_END_ARGS()
		void Construct(const FArguments& InArgs, const FProsettaVolumeSummary& Volume, bool bPIE);
		FString GetSelectedTag() const;
		bool GetRepoint() const;
		void SetSelectedTag(const FString& Tag);
		void SetRepoint(bool bRepoint);
		static bool CanRepoint();
		static FText Explanation(const FProsettaVolumeSummary& Volume, bool bPIE);
	private:
		TArray<TSharedPtr<FString>> TagOptions;
		TSharedPtr<FString> SelectedTag;
		bool bRepointChecked = false;
		bool bCanRepoint = false;
	};

	/** Shows the modal; returns false when a modal cannot be shown (headless). */
	static bool Show(TSharedRef<IProsettaDashboardModel> Model, const FProsettaVolumeSummary& Volume);
	/**
	 * Switches the sync scope, during PIE after stopping Play (one-shot EndPIE hook). Returns false
	 * when the model refused; the error is toasted.
	 */
	static bool Perform(TSharedRef<IProsettaDashboardModel> Model, const FString& Slug, const FString& Tag, bool bRepoint, bool bPIE);
};

/** Searchable table of the project's volumes with mirror/profile badges and a Use button per row. */
class PROSETTADASHBOARD_API SProsettaVolumesPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaVolumesPanel) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel);
	virtual ~SProsettaVolumesPanel() override;

	/** Rows after the search filter, in table order. */
	const TArray<TSharedPtr<FProsettaVolumeRow>>& GetRows() const { return FilteredRows; }
	void SetFilter(const FString& Text);
	/** Builds the rows for a state (volumes listing + manifest scope + profile). */
	static TArray<TSharedPtr<FProsettaVolumeRow>> BuildRows(const FProsettaDashboardState& State);
	static bool MatchesFilter(const FProsettaVolumeRow& Row, const FString& Filter);

private:
	void HandleStateChanged();
	void Rebuild();
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FProsettaVolumeRow> Row, const TSharedRef<STableViewBase>& Owner);
	void UseVolume(const FProsettaVolumeSummary& Volume);

	TSharedPtr<IProsettaDashboardModel> Model;
	FDelegateHandle StateHandle;
	TArray<TSharedPtr<FProsettaVolumeRow>> AllRows;
	TArray<TSharedPtr<FProsettaVolumeRow>> FilteredRows;
	TSharedPtr<SListView<TSharedPtr<FProsettaVolumeRow>>> ListView;
	TSharedPtr<SSearchBox> SearchBox;
	FString Filter;
};
