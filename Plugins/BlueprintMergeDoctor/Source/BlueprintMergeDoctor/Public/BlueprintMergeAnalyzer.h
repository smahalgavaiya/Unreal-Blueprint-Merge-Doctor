// Copyright Blueprint Merge Doctor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintMergeDoctorTypes.h"

class UBlueprint;

class BLUEPRINTMERGEDOCTOR_API FBlueprintMergeAnalyzer
{
public:
    FBlueprintMergeAnalysis Analyze(UBlueprint* Base, UBlueprint* Ours, UBlueprint* Theirs) const;
};
