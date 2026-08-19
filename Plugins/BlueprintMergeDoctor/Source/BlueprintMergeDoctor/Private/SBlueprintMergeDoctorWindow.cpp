// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "SBlueprintMergeDoctorWindow.h"

#include "AssetRegistry/AssetData.h"
#include "BlueprintMergeAnalyzer.h"
#include "BlueprintMergeService.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "BlueprintMergeDoctorWindow"

namespace BlueprintMergeDoctorWindowPrivate
{
    FSlateColor ClassificationColor(const EBlueprintMergeClassification Classification)
    {
        switch (Classification)
        {
        case EBlueprintMergeClassification::OursOnly:
        case EBlueprintMergeClassification::TheirsOnly:
        case EBlueprintMergeClassification::IdenticalChange:
        case EBlueprintMergeClassification::SafeNonOverlappingChange:
            return FLinearColor(0.24f, 0.78f, 0.46f);
        case EBlueprintMergeClassification::PotentialConflict:
            return FLinearColor(1.0f, 0.42f, 0.25f);
        default:
            return FLinearColor(1.0f, 0.72f, 0.18f);
        }
    }

    FString SourceText(const EBlueprintMergeSource Source)
    {
        switch (Source)
        {
        case EBlueprintMergeSource::Ours: return TEXT("OURS");
        case EBlueprintMergeSource::Theirs: return TEXT("THEIRS");
        default: return TEXT("UNKNOWN");
        }
    }

    class SBlueprintMergeChangeRow : public SMultiColumnTableRow<TSharedPtr<FBlueprintMergeChange>>
    {
    public:
        SLATE_BEGIN_ARGS(SBlueprintMergeChangeRow) {}
            SLATE_ARGUMENT(TSharedPtr<FBlueprintMergeChange>, Item)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
        {
            Item = InArgs._Item;
            SMultiColumnTableRow::Construct(
                SMultiColumnTableRow::FArguments().Padding(FMargin(4.0f, 3.0f)),
                OwnerTable);
        }

        virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
        {
            FString Value;
            FSlateColor Color = FSlateColor::UseForeground();
            if (!Item)
            {
                return SNew(STextBlock).Text(LOCTEXT("InvalidChange", "Invalid change"));
            }

            if (ColumnName == TEXT("Item"))
            {
                Value = Item->Category + TEXT(": ") + Item->DisplayName;
            }
            else if (ColumnName == TEXT("Base"))
            {
                Value = Item->BaseValue;
            }
            else if (ColumnName == TEXT("Ours"))
            {
                Value = Item->OursValue;
            }
            else if (ColumnName == TEXT("Theirs"))
            {
                Value = Item->TheirsValue;
            }
            else if (ColumnName == TEXT("Classification"))
            {
                Value = LexToString(Item->Classification);
                Color = ClassificationColor(Item->Classification);
            }
            else if (ColumnName == TEXT("Explanation"))
            {
                Value = Item->Explanation;
            }

            return SNew(STextBlock)
                .Text(FText::FromString(Value))
                .ColorAndOpacity(Color)
                .AutoWrapText(true)
                .ToolTipText(FText::FromString(Value));
        }

    private:
        TSharedPtr<FBlueprintMergeChange> Item;
    };

    TSharedRef<SWidget> MakeAssetSelectorRow(
        const FText& Label,
        const FText& Help,
        const TAttribute<FString>& ObjectPath,
        const FOnSetObject& OnObjectChanged)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(SBox)
                .WidthOverride(72.0f)
                [
                    SNew(STextBlock)
                    .Text(Label)
                    .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                    .ToolTipText(Help)
                ]
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(UBlueprint::StaticClass())
                .ObjectPath(ObjectPath)
                .OnObjectChanged(OnObjectChanged)
                .DisplayUseSelected(true)
                .DisplayBrowse(true)
                .AllowClear(true)
            ];
    }
}

void SBlueprintMergeDoctorWindow::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
        .Padding(12.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Title", "Blueprint Merge Doctor"))
                .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f, 0.0f, 12.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Subtitle", "Compare three Blueprint versions and apply only deterministic, non-overlapping additions to a new asset."))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                .Padding(10.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 3.0f)
                    [
                        BlueprintMergeDoctorWindowPrivate::MakeAssetSelectorRow(
                            LOCTEXT("BaseLabel", "BASE"),
                            LOCTEXT("BaseHelp", "Common ancestor Blueprint"),
                            TAttribute<FString>::CreateSP(this, &SBlueprintMergeDoctorWindow::GetBaseObjectPath),
                            FOnSetObject::CreateSP(this, &SBlueprintMergeDoctorWindow::OnBaseChanged))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 3.0f)
                    [
                        BlueprintMergeDoctorWindowPrivate::MakeAssetSelectorRow(
                            LOCTEXT("OursLabel", "OURS"),
                            LOCTEXT("OursHelp", "Current branch Blueprint"),
                            TAttribute<FString>::CreateSP(this, &SBlueprintMergeDoctorWindow::GetOursObjectPath),
                            FOnSetObject::CreateSP(this, &SBlueprintMergeDoctorWindow::OnOursChanged))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 3.0f)
                    [
                        BlueprintMergeDoctorWindowPrivate::MakeAssetSelectorRow(
                            LOCTEXT("TheirsLabel", "THEIRS"),
                            LOCTEXT("TheirsHelp", "Incoming branch Blueprint"),
                            TAttribute<FString>::CreateSP(this, &SBlueprintMergeDoctorWindow::GetTheirsObjectPath),
                            FOnSetObject::CreateSP(this, &SBlueprintMergeDoctorWindow::OnTheirsChanged))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .HAlign(HAlign_Right)
                    .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("AnalyzeMerge", "Analyze Merge"))
                        .ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
                        .IsEnabled(this, &SBlueprintMergeDoctorWindow::CanAnalyze)
                        .OnClicked(this, &SBlueprintMergeDoctorWindow::OnAnalyzeClicked)
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f)
            [
                SAssignNew(StatusText, STextBlock)
                .Text(LOCTEXT("InitialStatus", "Select BASE, OURS, and THEIRS to begin."))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SNew(SVerticalBox)
                .Visibility(this, &SBlueprintMergeDoctorWindow::GetResultsVisibility)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                        .Padding(9.0f)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [ SNew(STextBlock).Text(LOCTEXT("OursSummaryTitle", "OURS")).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold"))) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
                            [ SNew(STextBlock).Text(this, &SBlueprintMergeDoctorWindow::GetOursSummaryText).AutoWrapText(true) ]
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(4.0f, 0.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                        .Padding(9.0f)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [ SNew(STextBlock).Text(LOCTEXT("TheirsSummaryTitle", "THEIRS")).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold"))) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
                            [ SNew(STextBlock).Text(this, &SBlueprintMergeDoctorWindow::GetTheirsSummaryText).AutoWrapText(true) ]
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                        .Padding(9.0f)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [ SNew(STextBlock).Text(LOCTEXT("CompatibilityTitle", "COMPATIBILITY")).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold"))) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
                            [ SNew(STextBlock).Text(this, &SBlueprintMergeDoctorWindow::GetSummaryText).AutoWrapText(true) ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                .Padding(0.0f, 8.0f)
                [
                    SAssignNew(ChangeList, SListView<TSharedPtr<FBlueprintMergeChange>>)
                    .ListItemsSource(&ChangeItems)
                    .OnGenerateRow(this, &SBlueprintMergeDoctorWindow::GenerateChangeRow)
                    .SelectionMode(ESelectionMode::Single)
                    .HeaderRow
                    (
                        SNew(SHeaderRow)
                        + SHeaderRow::Column(TEXT("Item")).DefaultLabel(LOCTEXT("ItemColumn", "Item")).FillWidth(0.16f)
                        + SHeaderRow::Column(TEXT("Base")).DefaultLabel(LOCTEXT("BaseColumn", "BASE")).FillWidth(0.14f)
                        + SHeaderRow::Column(TEXT("Ours")).DefaultLabel(LOCTEXT("OursColumn", "OURS")).FillWidth(0.14f)
                        + SHeaderRow::Column(TEXT("Theirs")).DefaultLabel(LOCTEXT("TheirsColumn", "THEIRS")).FillWidth(0.14f)
                        + SHeaderRow::Column(TEXT("Classification")).DefaultLabel(LOCTEXT("ClassificationColumn", "Classification")).FillWidth(0.17f)
                        + SHeaderRow::Column(TEXT("Explanation")).DefaultLabel(LOCTEXT("ExplanationColumn", "Why")).FillWidth(0.25f)
                    )
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                    .Padding(9.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(STextBlock).Text(LOCTEXT("ProposedMergeTitle", "PROPOSED MERGE")).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold"))) ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
                        [ SNew(STextBlock).Text(this, &SBlueprintMergeDoctorWindow::GetSafePreviewText).AutoWrapText(true) ]
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(STextBlock).Text(this, &SBlueprintMergeDoctorWindow::GetManualPreviewText).AutoWrapText(true).ColorAndOpacity(FLinearColor(1.0f, 0.72f, 0.18f)) ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
                            [ SNew(STextBlock).Text(LOCTEXT("OutputPathLabel", "New asset package:")) ]
                            + SHorizontalBox::Slot().FillWidth(1.0f)
                            [ SAssignNew(OutputPathTextBox, SEditableTextBox).HintText(LOCTEXT("OutputPathHint", "/Game/Blueprints/BP_Name_Merged")) ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 7.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
                            [ SNew(SButton).Text(LOCTEXT("Cancel", "Cancel")).OnClicked(this, &SBlueprintMergeDoctorWindow::OnCancelClicked) ]
                            + SHorizontalBox::Slot().AutoWidth()
                            [
                                SNew(SButton)
                                .Text(LOCTEXT("CreateMerged", "Create Merged Blueprint"))
                                .ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
                                .IsEnabled(this, &SBlueprintMergeDoctorWindow::CanCreateMerged)
                                .OnClicked(this, &SBlueprintMergeDoctorWindow::OnCreateMergedClicked)
                            ]
                        ]
                    ]
                ]
            ]
        ]
    ];
}

void SBlueprintMergeDoctorWindow::OnBaseChanged(const FAssetData& AssetData)
{
    BaseBlueprint = Cast<UBlueprint>(AssetData.GetAsset());
    Analysis.Reset();
    RebuildItems();
}

void SBlueprintMergeDoctorWindow::OnOursChanged(const FAssetData& AssetData)
{
    OursBlueprint = Cast<UBlueprint>(AssetData.GetAsset());
    Analysis.Reset();
    RebuildItems();
}

void SBlueprintMergeDoctorWindow::OnTheirsChanged(const FAssetData& AssetData)
{
    TheirsBlueprint = Cast<UBlueprint>(AssetData.GetAsset());
    Analysis.Reset();
    RebuildItems();
}

FReply SBlueprintMergeDoctorWindow::OnAnalyzeClicked()
{
    const FBlueprintMergeAnalyzer Analyzer;
    Analysis = Analyzer.Analyze(BaseBlueprint.Get(), OursBlueprint.Get(), TheirsBlueprint.Get());
    RebuildItems();
    SetDefaultOutputPath();
    if (StatusText)
    {
        StatusText->SetText(Analysis->bInputsCompatible
            ? FText::Format(LOCTEXT("AnalysisComplete", "Analysis complete: {0} safe change(s), {1} conflict(s), {2} total item(s) requiring review."),
                Analysis->GetSafeChangeCount(), Analysis->GetConflictCount(), Analysis->GetManualReviewCount())
            : FText::FromString(Analysis->CompatibilityError));
        StatusText->SetColorAndOpacity(Analysis->bInputsCompatible
            ? FSlateColor(FLinearColor(0.24f, 0.78f, 0.46f))
            : FSlateColor(FLinearColor(1.0f, 0.42f, 0.25f)));
    }
    return FReply::Handled();
}

FReply SBlueprintMergeDoctorWindow::OnCreateMergedClicked()
{
    if (!Analysis.IsSet() || !OutputPathTextBox)
    {
        return FReply::Handled();
    }

    const FBlueprintMergeService Service;
    const FBlueprintMergeCreationResult Result = Service.CreateMergedBlueprint(*Analysis, OutputPathTextBox->GetText().ToString());
    if (Result.MergedBlueprint.IsValid())
    {
        TArray<UObject*> AssetsToSync;
        AssetsToSync.Add(Result.MergedBlueprint.Get());
        GEditor->SyncBrowserToObjects(AssetsToSync);
    }

    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message),
        Result.bCompiled ? LOCTEXT("MergeSuccessTitle", "Blueprint Merge Doctor") : LOCTEXT("MergeFailureTitle", "Blueprint Merge Requires Inspection"));
    return FReply::Handled();
}

FReply SBlueprintMergeDoctorWindow::OnCancelClicked()
{
    Analysis.Reset();
    RebuildItems();
    if (StatusText)
    {
        StatusText->SetText(LOCTEXT("AnalysisCancelled", "Merge preview cleared. No assets were modified."));
        StatusText->SetColorAndOpacity(FSlateColor::UseForeground());
    }
    return FReply::Handled();
}

FString SBlueprintMergeDoctorWindow::GetBaseObjectPath() const { return BaseBlueprint.IsValid() ? BaseBlueprint->GetPathName() : FString(); }
FString SBlueprintMergeDoctorWindow::GetOursObjectPath() const { return OursBlueprint.IsValid() ? OursBlueprint->GetPathName() : FString(); }
FString SBlueprintMergeDoctorWindow::GetTheirsObjectPath() const { return TheirsBlueprint.IsValid() ? TheirsBlueprint->GetPathName() : FString(); }

FText SBlueprintMergeDoctorWindow::GetSummaryText() const
{
    if (!Analysis.IsSet()) { return FText::GetEmpty(); }
    if (!Analysis->bInputsCompatible) { return FText::FromString(Analysis->CompatibilityError); }
    return FText::Format(LOCTEXT("CompatibilitySummary", "✓ {0} safe changes\n⚠ {1} potential conflicts\n⚠ {2} manual-review entries"),
        Analysis->GetSafeChangeCount(), Analysis->GetConflictCount(), Analysis->GetManualReviewCount());
}

FText SBlueprintMergeDoctorWindow::GetOursSummaryText() const
{
    if (!Analysis.IsSet()) { return FText::GetEmpty(); }
    TArray<FString> Lines;
    for (const FBlueprintMergePlannedAction& Action : Analysis->SafeActions)
    {
        if (Action.Source == EBlueprintMergeSource::Ours) { Lines.Add(TEXT("• ") + Action.ObjectPath); }
    }
    return FText::FromString(Lines.Num() ? FString::Join(Lines, TEXT("\n")) : TEXT("No deterministic additions"));
}

FText SBlueprintMergeDoctorWindow::GetTheirsSummaryText() const
{
    if (!Analysis.IsSet()) { return FText::GetEmpty(); }
    TArray<FString> Lines;
    for (const FBlueprintMergePlannedAction& Action : Analysis->SafeActions)
    {
        if (Action.Source == EBlueprintMergeSource::Theirs) { Lines.Add(TEXT("• ") + Action.ObjectPath); }
    }
    return FText::FromString(Lines.Num() ? FString::Join(Lines, TEXT("\n")) : TEXT("No deterministic additions"));
}

FText SBlueprintMergeDoctorWindow::GetSafePreviewText() const
{
    if (!Analysis.IsSet()) { return FText::GetEmpty(); }
    TArray<FString> Lines { TEXT("Safe additions:") };
    for (const FBlueprintMergePlannedAction& Action : Analysis->SafeActions)
    {
        Lines.Add(FString::Printf(TEXT("✓ %s from %s"), *Action.ObjectPath, *BlueprintMergeDoctorWindowPrivate::SourceText(Action.Source)));
    }
    if (Analysis->SafeActions.IsEmpty()) { Lines.Add(TEXT("None")); }
    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

FText SBlueprintMergeDoctorWindow::GetManualPreviewText() const
{
    if (!Analysis.IsSet()) { return FText::GetEmpty(); }
    TArray<FString> Lines { TEXT("Manual review:") };
    for (const FBlueprintMergeChange& Change : Analysis->Changes)
    {
        if (!Change.bSafeToMerge) { Lines.Add(TEXT("⚠ ") + Change.ObjectPath); }
    }
    if (Lines.Num() == 1) { Lines.Add(TEXT("None")); }
    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

EVisibility SBlueprintMergeDoctorWindow::GetResultsVisibility() const
{
    return Analysis.IsSet() ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SBlueprintMergeDoctorWindow::CanAnalyze() const
{
    return BaseBlueprint.IsValid() && OursBlueprint.IsValid() && TheirsBlueprint.IsValid();
}

bool SBlueprintMergeDoctorWindow::CanCreateMerged() const
{
    return Analysis.IsSet() && Analysis->bInputsCompatible && OutputPathTextBox.IsValid()
        && !OutputPathTextBox->GetText().IsEmpty();
}

TSharedRef<ITableRow> SBlueprintMergeDoctorWindow::GenerateChangeRow(
    TSharedPtr<FBlueprintMergeChange> Item,
    const TSharedRef<STableViewBase>& OwnerTable) const
{
    return SNew(BlueprintMergeDoctorWindowPrivate::SBlueprintMergeChangeRow, OwnerTable).Item(Item);
}

void SBlueprintMergeDoctorWindow::RebuildItems()
{
    ChangeItems.Reset();
    if (Analysis.IsSet())
    {
        for (const FBlueprintMergeChange& Change : Analysis->Changes)
        {
            ChangeItems.Add(MakeShared<FBlueprintMergeChange>(Change));
        }
    }
    if (ChangeList) { ChangeList->RequestListRefresh(); }
}

void SBlueprintMergeDoctorWindow::SetDefaultOutputPath()
{
    if (!OutputPathTextBox || !OursBlueprint.IsValid())
    {
        return;
    }

    const FString SourcePackageName = OursBlueprint->GetOutermost()->GetName();
    OutputPathTextBox->SetText(FText::FromString(SourcePackageName + TEXT("_Merged")));
}

#undef LOCTEXT_NAMESPACE
