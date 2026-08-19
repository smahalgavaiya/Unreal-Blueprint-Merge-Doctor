// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "BlueprintMergeAnalyzer.h"

#include "BlueprintMergeSnapshot.h"
#include "Engine/Blueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintMergeDoctorAnalyzer, Log, All);

namespace BlueprintMergeAnalyzerPrivate
{
    const TCHAR* MissingValue = TEXT("Does not exist");

    template <typename KeyType>
    FString KeyToString(const KeyType& Key)
    {
        return LexToString(Key);
    }

    template <>
    FString KeyToString<FName>(const FName& Key)
    {
        return Key.ToString();
    }

    template <>
    FString KeyToString<FString>(const FString& Key)
    {
        return Key;
    }

    struct FCompareOptions
    {
        FString Category;
        EBlueprintMergeOperation SafeAddOperation = EBlueprintMergeOperation::None;
        bool bSafeAdditions = false;
        bool bEmitAction = true;
    };

    template <typename KeyType, typename ValueType>
    void CompareMap(
        const TMap<KeyType, ValueType>& Base,
        const TMap<KeyType, ValueType>& Ours,
        const TMap<KeyType, ValueType>& Theirs,
        const FCompareOptions& Options,
        TArray<FBlueprintMergeChange>& OutChanges,
        TArray<FBlueprintMergePlannedAction>& OutActions)
    {
        TSet<KeyType> Keys;
        for (const TPair<KeyType, ValueType>& Pair : Base) { Keys.Add(Pair.Key); }
        for (const TPair<KeyType, ValueType>& Pair : Ours) { Keys.Add(Pair.Key); }
        for (const TPair<KeyType, ValueType>& Pair : Theirs) { Keys.Add(Pair.Key); }

        TArray<KeyType> SortedKeys = Keys.Array();
        SortedKeys.Sort([](const KeyType& Left, const KeyType& Right)
        {
            return KeyToString(Left) < KeyToString(Right);
        });

        for (const KeyType& Key : SortedKeys)
        {
            const ValueType* BaseValue = Base.Find(Key);
            const ValueType* OursValue = Ours.Find(Key);
            const ValueType* TheirsValue = Theirs.Find(Key);
            const bool bOursMatchesBase = BaseValue && OursValue && BaseValue->Canonical == OursValue->Canonical;
            const bool bTheirsMatchesBase = BaseValue && TheirsValue && BaseValue->Canonical == TheirsValue->Canonical;

            if ((BaseValue && OursValue && TheirsValue && bOursMatchesBase && bTheirsMatchesBase)
                || (!BaseValue && !OursValue && !TheirsValue))
            {
                continue;
            }

            FBlueprintMergeChange Change;
            const FString KeyString = KeyToString(Key);
            Change.ObjectPath = Options.Category + TEXT("/") + KeyString;
            Change.DisplayName = KeyString;
            Change.Category = Options.Category;
            Change.BaseValue = BaseValue ? BaseValue->Summary : MissingValue;
            Change.OursValue = OursValue ? OursValue->Summary : MissingValue;
            Change.TheirsValue = TheirsValue ? TheirsValue->Summary : MissingValue;

            EBlueprintMergeSource ActionSource = EBlueprintMergeSource::None;

            if (!BaseValue)
            {
                Change.ChangeKind = EBlueprintMergeChangeKind::Added;
                if (OursValue && !TheirsValue)
                {
                    Change.Classification = Options.bSafeAdditions
                        ? EBlueprintMergeClassification::OursOnly
                        : EBlueprintMergeClassification::UnsupportedManualReview;
                    Change.bSafeToMerge = Options.bSafeAdditions;
                    ActionSource = EBlueprintMergeSource::Ours;
                    Change.Explanation = Options.bSafeAdditions
                        ? TEXT("Added only in OURS. The object name is absent from BASE and THEIRS, so copying the whole addition is deterministic.")
                        : TEXT("Added only in OURS, but this object category is inspection-only in V1 and requires manual review.");
                }
                else if (!OursValue && TheirsValue)
                {
                    Change.Classification = Options.bSafeAdditions
                        ? EBlueprintMergeClassification::TheirsOnly
                        : EBlueprintMergeClassification::UnsupportedManualReview;
                    Change.bSafeToMerge = Options.bSafeAdditions;
                    ActionSource = EBlueprintMergeSource::Theirs;
                    Change.Explanation = Options.bSafeAdditions
                        ? TEXT("Added only in THEIRS. The object name is absent from BASE and OURS, so copying the whole addition is deterministic.")
                        : TEXT("Added only in THEIRS, but this object category is inspection-only in V1 and requires manual review.");
                }
                else if (OursValue && TheirsValue && OursValue->Canonical == TheirsValue->Canonical)
                {
                    Change.Classification = EBlueprintMergeClassification::IdenticalChange;
                    Change.bSafeToMerge = Options.bSafeAdditions;
                    ActionSource = EBlueprintMergeSource::Ours;
                    Change.Explanation = Options.bSafeAdditions
                        ? TEXT("Both branches added an equivalent object. One copy can be applied deterministically.")
                        : TEXT("Both branches added an equivalent object, but this category is not automatically applied in V1.");
                }
                else
                {
                    Change.Classification = EBlueprintMergeClassification::PotentialConflict;
                    Change.Explanation = TEXT("Both branches added the same object name with different definitions. No automatic choice is safe.");
                }
            }
            else if (!OursValue && !TheirsValue)
            {
                Change.ChangeKind = EBlueprintMergeChangeKind::Removed;
                Change.Classification = EBlueprintMergeClassification::IdenticalChange;
                Change.Explanation = TEXT("Both branches removed this object. V1 does not apply removals because dependent graphs or assets may still reference it.");
            }
            else if (!OursValue || !TheirsValue)
            {
                Change.ChangeKind = EBlueprintMergeChangeKind::Removed;
                const bool bRemovedInOurs = !OursValue;
                const ValueType* RemainingValue = bRemovedInOurs ? TheirsValue : OursValue;
                const bool bRemainingMatchesBase = RemainingValue && RemainingValue->Canonical == BaseValue->Canonical;

                if (bRemainingMatchesBase)
                {
                    Change.Classification = EBlueprintMergeClassification::UnsupportedManualReview;
                    Change.Explanation = FString::Printf(
                        TEXT("Removed only in %s. V1 never applies removals automatically because references may exist."),
                        bRemovedInOurs ? TEXT("OURS") : TEXT("THEIRS"));
                }
                else
                {
                    Change.Classification = EBlueprintMergeClassification::PotentialConflict;
                    Change.Explanation = TEXT("One branch removed this object while the other modified it. This requires manual review.");
                }
            }
            else if (bOursMatchesBase && !bTheirsMatchesBase)
            {
                Change.ChangeKind = EBlueprintMergeChangeKind::Modified;
                Change.Classification = EBlueprintMergeClassification::UnsupportedManualReview;
                Change.Explanation = TEXT("Modified only in THEIRS. V1 reports modifications but only auto-applies whole-object additions.");
            }
            else if (!bOursMatchesBase && bTheirsMatchesBase)
            {
                Change.ChangeKind = EBlueprintMergeChangeKind::Modified;
                Change.Classification = EBlueprintMergeClassification::UnsupportedManualReview;
                Change.Explanation = TEXT("Modified only in OURS. V1 reports modifications but only auto-applies whole-object additions.");
            }
            else if (OursValue->Canonical == TheirsValue->Canonical)
            {
                Change.ChangeKind = EBlueprintMergeChangeKind::Identical;
                Change.Classification = EBlueprintMergeClassification::IdenticalChange;
                Change.Explanation = TEXT("Both branches made the same modification. V1 leaves existing-object modifications for manual review.");
            }
            else
            {
                Change.ChangeKind = EBlueprintMergeChangeKind::Modified;
                Change.Classification = EBlueprintMergeClassification::PotentialConflict;
                Change.Explanation = TEXT("Both branches changed the same object differently. No automatic choice is made.");
            }

            OutChanges.Add(Change);
            if (Change.bSafeToMerge && Options.bEmitAction)
            {
                FBlueprintMergePlannedAction Action;
                Action.Operation = Options.SafeAddOperation;
                Action.Source = ActionSource;
                Action.ObjectName = FName(*KeyString);
                Action.ObjectPath = Change.ObjectPath;
                Action.Explanation = Change.Explanation;
                OutActions.Add(Action);
            }
        }
    }

    template <typename KeyType, typename ValueType>
    void CompareInspectionOnlyMap(
        const TMap<KeyType, ValueType>& Base,
        const TMap<KeyType, ValueType>& Ours,
        const TMap<KeyType, ValueType>& Theirs,
        const FString& Category,
        const FString& ObjectPrefix,
        TArray<FBlueprintMergeChange>& OutChanges)
    {
        FCompareOptions Options;
        Options.Category = Category;
        Options.bSafeAdditions = false;
        Options.bEmitAction = false;

        TArray<FBlueprintMergePlannedAction> IgnoredActions;
        const int32 FirstNewIndex = OutChanges.Num();
        CompareMap(Base, Ours, Theirs, Options, OutChanges, IgnoredActions);
        for (int32 Index = FirstNewIndex; Index < OutChanges.Num(); ++Index)
        {
            OutChanges[Index].ObjectPath = ObjectPrefix + TEXT("/") + OutChanges[Index].DisplayName;
        }
    }

    void InspectGraphDetails(
        const FBlueprintMergeGraphSnapshot& Base,
        const FBlueprintMergeGraphSnapshot& Ours,
        const FBlueprintMergeGraphSnapshot& Theirs,
        const FString& GraphPath,
        TArray<FBlueprintMergeChange>& OutChanges)
    {
        if (Base.Canonical == Ours.Canonical && Base.Canonical == Theirs.Canonical)
        {
            return;
        }

        CompareInspectionOnlyMap(Base.Nodes, Ours.Nodes, Theirs.Nodes, TEXT("Graph Node"), GraphPath + TEXT("/Nodes"), OutChanges);

        TSet<FString> NodeKeys;
        for (const TPair<FString, FBlueprintMergeNodeSnapshot>& Pair : Base.Nodes) { NodeKeys.Add(Pair.Key); }
        for (const TPair<FString, FBlueprintMergeNodeSnapshot>& Pair : Ours.Nodes) { NodeKeys.Add(Pair.Key); }
        for (const TPair<FString, FBlueprintMergeNodeSnapshot>& Pair : Theirs.Nodes) { NodeKeys.Add(Pair.Key); }

        for (const FString& NodeKey : NodeKeys)
        {
            const FBlueprintMergeNodeSnapshot* BaseNode = Base.Nodes.Find(NodeKey);
            const FBlueprintMergeNodeSnapshot* OursNode = Ours.Nodes.Find(NodeKey);
            const FBlueprintMergeNodeSnapshot* TheirsNode = Theirs.Nodes.Find(NodeKey);
            if (!BaseNode || !OursNode || !TheirsNode)
            {
                continue;
            }

            if (BaseNode->Canonical != OursNode->Canonical || BaseNode->Canonical != TheirsNode->Canonical)
            {
                CompareInspectionOnlyMap(
                    BaseNode->Pins,
                    OursNode->Pins,
                    TheirsNode->Pins,
                    TEXT("Graph Pin / Link"),
                    GraphPath + TEXT("/Nodes/") + NodeKey + TEXT("/Pins"),
                    OutChanges);
            }
        }
    }

    void InspectChangedGraphs(
        const TMap<FName, FBlueprintMergeGraphSnapshot>& Base,
        const TMap<FName, FBlueprintMergeGraphSnapshot>& Ours,
        const TMap<FName, FBlueprintMergeGraphSnapshot>& Theirs,
        const FString& Prefix,
        TArray<FBlueprintMergeChange>& OutChanges)
    {
        TSet<FName> Keys;
        for (const TPair<FName, FBlueprintMergeGraphSnapshot>& Pair : Base) { Keys.Add(Pair.Key); }
        for (const TPair<FName, FBlueprintMergeGraphSnapshot>& Pair : Ours) { Keys.Add(Pair.Key); }
        for (const TPair<FName, FBlueprintMergeGraphSnapshot>& Pair : Theirs) { Keys.Add(Pair.Key); }

        for (const FName Key : Keys)
        {
            const FBlueprintMergeGraphSnapshot* BaseGraph = Base.Find(Key);
            const FBlueprintMergeGraphSnapshot* OursGraph = Ours.Find(Key);
            const FBlueprintMergeGraphSnapshot* TheirsGraph = Theirs.Find(Key);
            if (BaseGraph && OursGraph && TheirsGraph)
            {
                InspectGraphDetails(*BaseGraph, *OursGraph, *TheirsGraph, Prefix + TEXT("/") + Key.ToString(), OutChanges);
            }
        }
    }

    void MarkCrossBranchAdditionsAsNonOverlapping(TArray<FBlueprintMergeChange>& Changes)
    {
        TSet<FString> OursCategories;
        TSet<FString> TheirsCategories;
        for (const FBlueprintMergeChange& Change : Changes)
        {
            if (!Change.bSafeToMerge || Change.ChangeKind != EBlueprintMergeChangeKind::Added)
            {
                continue;
            }

            if (Change.Classification == EBlueprintMergeClassification::OursOnly) { OursCategories.Add(Change.Category); }
            if (Change.Classification == EBlueprintMergeClassification::TheirsOnly) { TheirsCategories.Add(Change.Category); }
        }

        for (FBlueprintMergeChange& Change : Changes)
        {
            const bool bCrossBranchCategory = OursCategories.Contains(Change.Category) && TheirsCategories.Contains(Change.Category);
            if (bCrossBranchCategory && Change.bSafeToMerge
                && (Change.Classification == EBlueprintMergeClassification::OursOnly
                    || Change.Classification == EBlueprintMergeClassification::TheirsOnly))
            {
                Change.Classification = EBlueprintMergeClassification::SafeNonOverlappingChange;
                Change.Explanation += TEXT(" The other branch adds different names in the same category; the additions do not overlap.");
            }
        }
    }

    void DemoteUnsafeComponentAdditions(
        const FBlueprintMergeSnapshot& Base,
        const FBlueprintMergeSnapshot& Ours,
        const FBlueprintMergeSnapshot& Theirs,
        TArray<FBlueprintMergeChange>& Changes,
        TArray<FBlueprintMergePlannedAction>& Actions)
    {
        for (int32 ActionIndex = Actions.Num() - 1; ActionIndex >= 0; --ActionIndex)
        {
            const FBlueprintMergePlannedAction& Action = Actions[ActionIndex];
            if (Action.Operation != EBlueprintMergeOperation::AddComponent)
            {
                continue;
            }

            const TMap<FName, FBlueprintMergeComponentSnapshot>& SourceComponents =
                Action.Source == EBlueprintMergeSource::Ours ? Ours.Components : Theirs.Components;
            const FBlueprintMergeComponentSnapshot* Component = SourceComponents.Find(Action.ObjectName);
            bool bUnsafe = !Component || Component->bHasBranchLocalObjectReferences;
            FString Reason = TEXT("The component template contains references to objects owned by the branch Blueprint. V1 cannot safely remap those references.");

            if (Component && !Component->ParentName.IsNone() && !Base.Components.Contains(Component->ParentName))
            {
                const bool bParentPlanned = Actions.ContainsByPredicate([&Action, Component](const FBlueprintMergePlannedAction& Candidate)
                {
                    return Candidate.Operation == EBlueprintMergeOperation::AddComponent
                        && Candidate.Source == Action.Source
                        && Candidate.ObjectName == Component->ParentName;
                });
                if (!bParentPlanned)
                {
                    bUnsafe = true;
                    Reason = TEXT("The component parent is not present in BASE and is not another deterministic addition from the same branch.");
                }
            }

            if (!bUnsafe)
            {
                continue;
            }

            if (FBlueprintMergeChange* Change = Changes.FindByPredicate([&Action](const FBlueprintMergeChange& Candidate)
            {
                return Candidate.ObjectPath == Action.ObjectPath;
            }))
            {
                Change->bSafeToMerge = false;
                Change->Classification = EBlueprintMergeClassification::UnsupportedManualReview;
                Change->Explanation = Reason;
            }
            Actions.RemoveAt(ActionIndex);
        }
    }
}

FBlueprintMergeAnalysis FBlueprintMergeAnalyzer::Analyze(UBlueprint* Base, UBlueprint* Ours, UBlueprint* Theirs) const
{
    FBlueprintMergeAnalysis Result;
    Result.BaseBlueprint = Base;
    Result.OursBlueprint = Ours;
    Result.TheirsBlueprint = Theirs;

    if (!Base || !Ours || !Theirs)
    {
        Result.CompatibilityError = TEXT("BASE, OURS, and THEIRS must all be selected.");
        return Result;
    }

    const FBlueprintMergeSnapshotBuilder SnapshotBuilder;
    const FBlueprintMergeSnapshot BaseSnapshot = SnapshotBuilder.Capture(Base);
    const FBlueprintMergeSnapshot OursSnapshot = SnapshotBuilder.Capture(Ours);
    const FBlueprintMergeSnapshot TheirsSnapshot = SnapshotBuilder.Capture(Theirs);

    if (BaseSnapshot.ParentClassPath != OursSnapshot.ParentClassPath
        || BaseSnapshot.ParentClassPath != TheirsSnapshot.ParentClassPath)
    {
        Result.CompatibilityError = FString::Printf(
            TEXT("Blueprint parent classes differ. BASE=%s, OURS=%s, THEIRS=%s"),
            *BaseSnapshot.ParentClassPath,
            *OursSnapshot.ParentClassPath,
            *TheirsSnapshot.ParentClassPath);
        return Result;
    }

    if (BaseSnapshot.BlueprintType != OursSnapshot.BlueprintType
        || BaseSnapshot.BlueprintType != TheirsSnapshot.BlueprintType)
    {
        Result.CompatibilityError = TEXT("Blueprint types differ. Only three versions of the same Blueprint kind can be compared.");
        return Result;
    }

    Result.bInputsCompatible = true;

    using namespace BlueprintMergeAnalyzerPrivate;

    CompareMap(BaseSnapshot.Variables, OursSnapshot.Variables, TheirsSnapshot.Variables,
        { TEXT("Variable"), EBlueprintMergeOperation::AddVariable, true, true }, Result.Changes, Result.SafeActions);
    CompareMap(BaseSnapshot.Functions, OursSnapshot.Functions, TheirsSnapshot.Functions,
        { TEXT("Function"), EBlueprintMergeOperation::AddFunction, true, true }, Result.Changes, Result.SafeActions);
    CompareMap(BaseSnapshot.MacroGraphs, OursSnapshot.MacroGraphs, TheirsSnapshot.MacroGraphs,
        { TEXT("Macro Graph"), EBlueprintMergeOperation::AddMacroGraph, true, true }, Result.Changes, Result.SafeActions);
    CompareMap(BaseSnapshot.EventGraphs, OursSnapshot.EventGraphs, TheirsSnapshot.EventGraphs,
        { TEXT("Event Graph"), EBlueprintMergeOperation::None, false, false }, Result.Changes, Result.SafeActions);
    CompareMap(BaseSnapshot.Components, OursSnapshot.Components, TheirsSnapshot.Components,
        { TEXT("Component"), EBlueprintMergeOperation::AddComponent, true, true }, Result.Changes, Result.SafeActions);
    CompareMap(BaseSnapshot.Interfaces, OursSnapshot.Interfaces, TheirsSnapshot.Interfaces,
        { TEXT("Interface"), EBlueprintMergeOperation::AddInterface, true, true }, Result.Changes, Result.SafeActions);
    CompareMap(BaseSnapshot.DefaultProperties, OursSnapshot.DefaultProperties, TheirsSnapshot.DefaultProperties,
        { TEXT("Default Property"), EBlueprintMergeOperation::None, false, false }, Result.Changes, Result.SafeActions);

    DemoteUnsafeComponentAdditions(
        BaseSnapshot, OursSnapshot, TheirsSnapshot, Result.Changes, Result.SafeActions);

    InspectChangedGraphs(BaseSnapshot.Functions, OursSnapshot.Functions, TheirsSnapshot.Functions, TEXT("Functions"), Result.Changes);
    InspectChangedGraphs(BaseSnapshot.MacroGraphs, OursSnapshot.MacroGraphs, TheirsSnapshot.MacroGraphs, TEXT("Macros"), Result.Changes);
    InspectChangedGraphs(BaseSnapshot.EventGraphs, OursSnapshot.EventGraphs, TheirsSnapshot.EventGraphs, TEXT("EventGraphs"), Result.Changes);
    MarkCrossBranchAdditionsAsNonOverlapping(Result.Changes);

    for (const FBlueprintMergeChange& Change : Result.Changes)
    {
        if (Change.Classification == EBlueprintMergeClassification::PotentialConflict)
        {
            UE_LOG(LogBlueprintMergeDoctorAnalyzer, Warning,
                TEXT("Conflict %s: BASE=[%s] OURS=[%s] THEIRS=[%s] Reason=%s"),
                *Change.ObjectPath, *Change.BaseValue, *Change.OursValue, *Change.TheirsValue, *Change.Explanation);
        }
        else
        {
            UE_LOG(LogBlueprintMergeDoctorAnalyzer, Verbose,
                TEXT("Change %s classified %s (safe=%d): %s"),
                *Change.ObjectPath, *LexToString(Change.Classification), Change.bSafeToMerge, *Change.Explanation);
        }
    }

    UE_LOG(LogBlueprintMergeDoctorAnalyzer, Display,
        TEXT("Analyzed merge BASE=%s OURS=%s THEIRS=%s: %d safe actions, %d conflicts, %d manual-review entries"),
        *Base->GetPathName(), *Ours->GetPathName(), *Theirs->GetPathName(),
        Result.SafeActions.Num(), Result.GetConflictCount(), Result.GetManualReviewCount());

    return Result;
}
