// Copyright Blueprint Merge Doctor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;

/**
 * Small boundary around Unreal editor APIs that are most likely to vary between
 * UE5 minor releases. The analyzer and merge policy do not depend on these APIs.
 */
class FBlueprintMergeEngineAdapter
{
public:
    static bool AddVariable(UBlueprint* Source, UBlueprint* Target, FName VariableName, FString& OutError);
    static bool AddFunction(UBlueprint* Source, UBlueprint* Target, FName FunctionName, FString& OutError);
    static bool AddMacroGraph(UBlueprint* Source, UBlueprint* Target, FName GraphName, FString& OutError);
    static bool AddComponent(UBlueprint* Source, UBlueprint* Target, FName ComponentName, FString& OutError);
    static bool AddInterface(UBlueprint* Source, UBlueprint* Target, const FString& InterfaceClassPath, FString& OutError);
    static bool Compile(UBlueprint* Blueprint, int32& OutErrors, int32& OutWarnings, FString& OutLog);

private:
    static class UEdGraph* CloneGraph(UBlueprint* Source, UBlueprint* Target, FName GraphName, bool bFunctionGraph, FString& OutError);
};
