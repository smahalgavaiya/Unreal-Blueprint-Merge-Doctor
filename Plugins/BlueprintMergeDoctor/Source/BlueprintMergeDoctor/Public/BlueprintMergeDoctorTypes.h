// Copyright Blueprint Merge Doctor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

enum class EBlueprintMergeChangeKind : uint8
{
    Added,
    Removed,
    Modified,
    Identical
};

enum class EBlueprintMergeClassification : uint8
{
    OursOnly,
    TheirsOnly,
    IdenticalChange,
    SafeNonOverlappingChange,
    PotentialConflict,
    UnsupportedManualReview
};

enum class EBlueprintMergeSource : uint8
{
    None,
    Ours,
    Theirs
};

enum class EBlueprintMergeOperation : uint8
{
    None,
    AddVariable,
    AddFunction,
    AddMacroGraph,
    AddComponent,
    AddInterface
};

struct BLUEPRINTMERGEDOCTOR_API FBlueprintMergeChange
{
    FString ObjectPath;
    FString DisplayName;
    FString Category;
    FString BaseValue;
    FString OursValue;
    FString TheirsValue;
    EBlueprintMergeChangeKind ChangeKind = EBlueprintMergeChangeKind::Modified;
    EBlueprintMergeClassification Classification = EBlueprintMergeClassification::UnsupportedManualReview;
    bool bSafeToMerge = false;
    FString Explanation;
};

struct BLUEPRINTMERGEDOCTOR_API FBlueprintMergePlannedAction
{
    EBlueprintMergeOperation Operation = EBlueprintMergeOperation::None;
    EBlueprintMergeSource Source = EBlueprintMergeSource::None;
    FName ObjectName = NAME_None;
    FString ObjectPath;
    FString Explanation;
};

struct BLUEPRINTMERGEDOCTOR_API FBlueprintMergeAnalysis
{
    TWeakObjectPtr<UBlueprint> BaseBlueprint;
    TWeakObjectPtr<UBlueprint> OursBlueprint;
    TWeakObjectPtr<UBlueprint> TheirsBlueprint;
    TArray<FBlueprintMergeChange> Changes;
    TArray<FBlueprintMergePlannedAction> SafeActions;
    bool bInputsCompatible = false;
    FString CompatibilityError;

    int32 GetSafeChangeCount() const;
    int32 GetConflictCount() const;
    int32 GetManualReviewCount() const;
    bool HasUnresolvedChanges() const;
};

struct BLUEPRINTMERGEDOCTOR_API FBlueprintMergeCreationResult
{
    TWeakObjectPtr<UBlueprint> MergedBlueprint;
    bool bCreated = false;
    bool bCompiled = false;
    bool bSaved = false;
    int32 CompileErrors = 0;
    int32 CompileWarnings = 0;
    FString AssetPath;
    FString Message;
};

BLUEPRINTMERGEDOCTOR_API FString LexToString(EBlueprintMergeChangeKind Value);
BLUEPRINTMERGEDOCTOR_API FString LexToString(EBlueprintMergeClassification Value);
