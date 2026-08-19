// Copyright Blueprint Merge Doctor. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"

struct FBlueprintMergeVariableSnapshot
{
    FName Name = NAME_None;
    FGuid Guid;
    FEdGraphPinType Type;
    FString TypeText;
    FString DefaultValue;
    FString Category;
    uint64 PropertyFlags = 0;
    FName RepNotifyFunction = NAME_None;
    TEnumAsByte<ELifetimeCondition> ReplicationCondition = COND_None;
    TArray<FBPVariableMetaDataEntry> Metadata;
    FString Canonical;
    FString Summary;
};

struct FBlueprintMergePinSnapshot
{
    FString Key;
    FString Name;
    FString Canonical;
    FString Summary;
};

struct FBlueprintMergeNodeSnapshot
{
    FString Key;
    FGuid Guid;
    FString ClassPath;
    FString Title;
    int32 PositionX = 0;
    int32 PositionY = 0;
    TMap<FString, FBlueprintMergePinSnapshot> Pins;
    FString Canonical;
    FString Summary;
};

struct FBlueprintMergeGraphSnapshot
{
    FName Name = NAME_None;
    FString GraphKind;
    FString FunctionSignature;
    TMap<FString, FBlueprintMergeNodeSnapshot> Nodes;
    FString Canonical;
    FString Summary;
};

struct FBlueprintMergeComponentSnapshot
{
    FName Name = NAME_None;
    FString ClassPath;
    FName ParentName = NAME_None;
    FString TemplateProperties;
    bool bHasBranchLocalObjectReferences = false;
    FString Canonical;
    FString Summary;
};

struct FBlueprintMergeInterfaceSnapshot
{
    FString ClassPath;
    FString Canonical;
    FString Summary;
};

struct FBlueprintMergeDefaultPropertySnapshot
{
    FString Key;
    FString Value;
    FString Canonical;
    FString Summary;
};

struct FBlueprintMergeSnapshot
{
    FString BlueprintPath;
    FString ParentClassPath;
    EBlueprintType BlueprintType = BPTYPE_Normal;
    TMap<FName, FBlueprintMergeVariableSnapshot> Variables;
    TMap<FName, FBlueprintMergeGraphSnapshot> Functions;
    TMap<FName, FBlueprintMergeGraphSnapshot> MacroGraphs;
    TMap<FName, FBlueprintMergeGraphSnapshot> EventGraphs;
    TMap<FName, FBlueprintMergeComponentSnapshot> Components;
    TMap<FString, FBlueprintMergeInterfaceSnapshot> Interfaces;
    TMap<FString, FBlueprintMergeDefaultPropertySnapshot> DefaultProperties;
};

class FBlueprintMergeSnapshotBuilder
{
public:
    FBlueprintMergeSnapshot Capture(const UBlueprint* Blueprint) const;

private:
    static FString PinTypeToCanonical(const FEdGraphPinType& PinType);
    static FString ExportPropertyValue(const FProperty* Property, const UObject* Container);
    static FString CaptureEditableProperties(const UObject* Object);
    static FName FindComponentParentName(const UBlueprint* Blueprint, const class USCS_Node* ChildNode);
    static FBlueprintMergeGraphSnapshot CaptureGraph(const class UEdGraph* Graph, const FString& GraphKind);
};
