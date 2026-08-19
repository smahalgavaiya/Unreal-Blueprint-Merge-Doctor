// Copyright Blueprint Merge Doctor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintMergeDoctorTypes.h"

class UBlueprint;

class BLUEPRINTMERGEDOCTOR_API FBlueprintMergeService
{
public:
    FBlueprintMergeCreationResult CreateMergedBlueprint(
        const FBlueprintMergeAnalysis& Analysis,
        const FString& DesiredLongPackageName = FString()) const;

    FBlueprintMergeCreationResult CreateTransientMergedBlueprint(
        const FBlueprintMergeAnalysis& Analysis,
        FName ObjectName = NAME_None) const;

private:
    FBlueprintMergeCreationResult ApplyAndCompile(
        const FBlueprintMergeAnalysis& Analysis,
        UBlueprint* MergedBlueprint,
        bool bSaveOnSuccess) const;
};
