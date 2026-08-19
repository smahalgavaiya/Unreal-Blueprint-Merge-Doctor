// Copyright Blueprint Merge Doctor. All Rights Reserved.

#pragma once

#include "BlueprintMergeDoctorTypes.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;
template <typename ItemType> class SListView;
struct FAssetData;
class UBlueprint;

class SBlueprintMergeDoctorWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SBlueprintMergeDoctorWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    void OnBaseChanged(const FAssetData& AssetData);
    void OnOursChanged(const FAssetData& AssetData);
    void OnTheirsChanged(const FAssetData& AssetData);
    FReply OnAnalyzeClicked();
    FReply OnCreateMergedClicked();
    FReply OnCancelClicked();

    FString GetBaseObjectPath() const;
    FString GetOursObjectPath() const;
    FString GetTheirsObjectPath() const;
    FText GetSummaryText() const;
    FText GetOursSummaryText() const;
    FText GetTheirsSummaryText() const;
    FText GetSafePreviewText() const;
    FText GetManualPreviewText() const;
    EVisibility GetResultsVisibility() const;
    bool CanAnalyze() const;
    bool CanCreateMerged() const;

    TSharedRef<ITableRow> GenerateChangeRow(
        TSharedPtr<FBlueprintMergeChange> Item,
        const TSharedRef<STableViewBase>& OwnerTable) const;
    void RebuildItems();
    void SetDefaultOutputPath();

    TWeakObjectPtr<UBlueprint> BaseBlueprint;
    TWeakObjectPtr<UBlueprint> OursBlueprint;
    TWeakObjectPtr<UBlueprint> TheirsBlueprint;
    TOptional<FBlueprintMergeAnalysis> Analysis;
    TArray<TSharedPtr<FBlueprintMergeChange>> ChangeItems;
    TSharedPtr<SListView<TSharedPtr<FBlueprintMergeChange>>> ChangeList;
    TSharedPtr<SEditableTextBox> OutputPathTextBox;
    TSharedPtr<STextBlock> StatusText;
};
