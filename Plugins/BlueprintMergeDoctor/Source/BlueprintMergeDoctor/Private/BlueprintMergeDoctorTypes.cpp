// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "BlueprintMergeDoctorTypes.h"

int32 FBlueprintMergeAnalysis::GetSafeChangeCount() const
{
    int32 Count = 0;
    for (const FBlueprintMergeChange& Change : Changes)
    {
        Count += Change.bSafeToMerge ? 1 : 0;
    }
    return Count;
}

int32 FBlueprintMergeAnalysis::GetConflictCount() const
{
    int32 Count = 0;
    for (const FBlueprintMergeChange& Change : Changes)
    {
        Count += Change.Classification == EBlueprintMergeClassification::PotentialConflict ? 1 : 0;
    }
    return Count;
}

int32 FBlueprintMergeAnalysis::GetManualReviewCount() const
{
    int32 Count = 0;
    for (const FBlueprintMergeChange& Change : Changes)
    {
        Count += Change.bSafeToMerge ? 0 : 1;
    }
    return Count;
}

bool FBlueprintMergeAnalysis::HasUnresolvedChanges() const
{
    return GetManualReviewCount() > 0;
}

FString LexToString(const EBlueprintMergeChangeKind Value)
{
    switch (Value)
    {
    case EBlueprintMergeChangeKind::Added: return TEXT("Added");
    case EBlueprintMergeChangeKind::Removed: return TEXT("Removed");
    case EBlueprintMergeChangeKind::Modified: return TEXT("Modified");
    case EBlueprintMergeChangeKind::Identical: return TEXT("Identical");
    default: return TEXT("Unknown");
    }
}

FString LexToString(const EBlueprintMergeClassification Value)
{
    switch (Value)
    {
    case EBlueprintMergeClassification::OursOnly: return TEXT("OURS ONLY");
    case EBlueprintMergeClassification::TheirsOnly: return TEXT("THEIRS ONLY");
    case EBlueprintMergeClassification::IdenticalChange: return TEXT("IDENTICAL CHANGE");
    case EBlueprintMergeClassification::SafeNonOverlappingChange: return TEXT("SAFE NON-OVERLAPPING CHANGE");
    case EBlueprintMergeClassification::PotentialConflict: return TEXT("POTENTIAL CONFLICT");
    case EBlueprintMergeClassification::UnsupportedManualReview: return TEXT("UNSUPPORTED / MANUAL REVIEW");
    default: return TEXT("UNKNOWN");
    }
}
