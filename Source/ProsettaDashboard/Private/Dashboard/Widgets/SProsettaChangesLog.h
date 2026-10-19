#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ProsettaDashboardModel.h"

class SExpandableArea;
class ITableRow;
class STableViewBase;
template <typename T> class SListView;

/** Collapsible "Recent changes" list beside a filtered Output Log; expansion persists in the user settings. */
class PROSETTADASHBOARD_API SProsettaChangesLog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProsettaChangesLog) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, TSharedRef<IProsettaDashboardModel> InModel);
	virtual ~SProsettaChangesLog() override;

	void Expand();
	bool IsExpanded() const;
	FText Title() const;
	int32 NumRows() const { return Rows.Num(); }
	static const FSlateBrush* IconFor(EProsettaChangeKind Kind);

private:
	void HandleStateChanged();
	void Rebuild();
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FProsettaChangeEntry> Entry, const TSharedRef<STableViewBase>& Owner);

	TSharedPtr<IProsettaDashboardModel> Model;
	FDelegateHandle StateHandle;
	TArray<TSharedPtr<FProsettaChangeEntry>> Rows;
	TSharedPtr<SListView<TSharedPtr<FProsettaChangeEntry>>> ListView;
	TSharedPtr<SExpandableArea> Area;
};
