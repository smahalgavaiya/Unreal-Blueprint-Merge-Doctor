// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "BlueprintMergeSnapshot.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Misc/Crc.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

namespace BlueprintMergeSnapshotPrivate
{
    FString Digest(const FString& Value)
    {
        return FString::Printf(TEXT("%08X"), FCrc::StrCrc32(*Value));
    }

    FString ObjectPathOrNone(const UObject* Object)
    {
        return Object ? Object->GetPathName() : TEXT("None");
    }

    FString PinKey(const UEdGraphPin* Pin, const int32 Index)
    {
        if (Pin->PinId.IsValid())
        {
            return Pin->PinId.ToString(EGuidFormats::DigitsWithHyphensLower);
        }

        return FString::Printf(TEXT("%d:%s:%d"), static_cast<int32>(Pin->Direction), *Pin->PinName.ToString(), Index);
    }

    FString NodeKey(const UEdGraphNode* Node, const int32 Index)
    {
        if (Node->NodeGuid.IsValid())
        {
            return Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
        }

        return FString::Printf(TEXT("%s:%d"), *Node->GetClass()->GetPathName(), Index);
    }

    bool IsDeterministicValueProperty(const FProperty* Property)
    {
        if (!Property
            || Property->IsA<FObjectPropertyBase>()
            || Property->IsA<FDelegateProperty>()
            || Property->IsA<FMulticastDelegateProperty>()
            || Property->IsA<FInterfaceProperty>())
        {
            return false;
        }

        if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            return IsDeterministicValueProperty(ArrayProperty->Inner);
        }
        if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
        {
            return IsDeterministicValueProperty(SetProperty->ElementProp);
        }
        if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
        {
            return IsDeterministicValueProperty(MapProperty->KeyProp)
                && IsDeterministicValueProperty(MapProperty->ValueProp);
        }

        return true;
    }

    bool ValueContainsPackageReference(const FProperty* Property, const void* Value, const UPackage* Package)
    {
        if (!Property || !Value || !Package)
        {
            return false;
        }

        if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            const UObject* ReferencedObject = ObjectProperty->GetObjectPropertyValue(Value);
            return ReferencedObject && ReferencedObject->GetOutermost() == Package;
        }
        if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
            {
                const FProperty* InnerProperty = *It;
                if (ValueContainsPackageReference(InnerProperty, InnerProperty->ContainerPtrToValuePtr<void>(Value), Package))
                {
                    return true;
                }
            }
        }
        else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Helper(ArrayProperty, Value);
            for (int32 Index = 0; Index < Helper.Num(); ++Index)
            {
                if (ValueContainsPackageReference(ArrayProperty->Inner, Helper.GetRawPtr(Index), Package))
                {
                    return true;
                }
            }
        }
        else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
        {
            FScriptSetHelper Helper(SetProperty, Value);
            for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
            {
                if (Helper.IsValidIndex(Index)
                    && ValueContainsPackageReference(SetProperty->ElementProp, Helper.GetElementPtr(Index), Package))
                {
                    return true;
                }
            }
        }
        else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
        {
            FScriptMapHelper Helper(MapProperty, Value);
            for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
            {
                if (Helper.IsValidIndex(Index)
                    && (ValueContainsPackageReference(MapProperty->KeyProp, Helper.GetKeyPtr(Index), Package)
                        || ValueContainsPackageReference(MapProperty->ValueProp, Helper.GetValuePtr(Index), Package)))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool HasBranchLocalObjectReferences(const UObject* Object, const UPackage* Package)
    {
        if (!Object || !Package)
        {
            return false;
        }

        for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
            {
                continue;
            }

            if (ValueContainsPackageReference(Property, Property->ContainerPtrToValuePtr<void>(Object), Package))
            {
                return true;
            }
        }
        return false;
    }
}

FString FBlueprintMergeSnapshotBuilder::PinTypeToCanonical(const FEdGraphPinType& PinType)
{
    return FString::Printf(
        TEXT("%s|%s|%s|%s|%d|ref=%d|const=%d|weak=%d|wrapped=%d"),
        *PinType.PinCategory.ToString(),
        *PinType.PinSubCategory.ToString(),
        *BlueprintMergeSnapshotPrivate::ObjectPathOrNone(PinType.PinSubCategoryObject.Get()),
        *UEdGraphSchema_K2::TypeToText(PinType).ToString(),
        static_cast<int32>(PinType.ContainerType),
        PinType.bIsReference,
        PinType.bIsConst,
        PinType.bIsWeakPointer,
        PinType.bIsUObjectWrapper);
}

FString FBlueprintMergeSnapshotBuilder::ExportPropertyValue(const FProperty* Property, const UObject* Container)
{
    if (!Property || !Container)
    {
        return FString();
    }

    FString Result;
    const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
    Property->ExportTextItem_Direct(Result, Value, nullptr, const_cast<UObject*>(Container), PPF_SerializedAsImportText);
    return Result;
}

FString FBlueprintMergeSnapshotBuilder::CaptureEditableProperties(const UObject* Object)
{
    if (!Object)
    {
        return FString();
    }

    TArray<FString> Values;
    for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)
            || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_Deprecated | CPF_EditorOnly)
            || !BlueprintMergeSnapshotPrivate::IsDeterministicValueProperty(Property))
        {
            continue;
        }

        Values.Add(Property->GetName() + TEXT("=") + ExportPropertyValue(Property, Object));
    }

    Values.Sort();
    return FString::Join(Values, TEXT("\n"));
}

FName FBlueprintMergeSnapshotBuilder::FindComponentParentName(const UBlueprint* Blueprint, const USCS_Node* ChildNode)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript || !ChildNode)
    {
        return NAME_None;
    }

    for (const USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Candidate && Candidate->GetChildNodes().Contains(ChildNode))
        {
            return Candidate->GetVariableName();
        }
    }

    return ChildNode->ParentComponentOrVariableName;
}

FBlueprintMergeGraphSnapshot FBlueprintMergeSnapshotBuilder::CaptureGraph(const UEdGraph* Graph, const FString& GraphKind)
{
    FBlueprintMergeGraphSnapshot Result;
    if (!Graph)
    {
        return Result;
    }

    Result.Name = Graph->GetFName();
    Result.GraphKind = GraphKind;
    TArray<FString> NodeCanonicalValues;
    TArray<FString> SignaturePins;

    for (int32 NodeIndex = 0; NodeIndex < Graph->Nodes.Num(); ++NodeIndex)
    {
        const UEdGraphNode* Node = Graph->Nodes[NodeIndex];
        if (!Node)
        {
            continue;
        }

        FBlueprintMergeNodeSnapshot NodeSnapshot;
        NodeSnapshot.Key = BlueprintMergeSnapshotPrivate::NodeKey(Node, NodeIndex);
        NodeSnapshot.Guid = Node->NodeGuid;
        NodeSnapshot.ClassPath = Node->GetClass()->GetPathName();
        NodeSnapshot.Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        NodeSnapshot.PositionX = Node->NodePosX;
        NodeSnapshot.PositionY = Node->NodePosY;

        TArray<FString> PinCanonicalValues;
        for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
        {
            const UEdGraphPin* Pin = Node->Pins[PinIndex];
            if (!Pin)
            {
                continue;
            }

            FBlueprintMergePinSnapshot PinSnapshot;
            PinSnapshot.Key = BlueprintMergeSnapshotPrivate::PinKey(Pin, PinIndex);
            PinSnapshot.Name = Pin->PinName.ToString();

            TArray<FString> Links;
            for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin || !LinkedPin->GetOwningNode())
                {
                    continue;
                }

                const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                Links.Add(FString::Printf(
                    TEXT("%s:%s:%s:%d"),
                    *LinkedNode->GetClass()->GetPathName(),
                    *LinkedNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                    *LinkedPin->PinName.ToString(),
                    static_cast<int32>(LinkedPin->Direction)));
            }
            Links.Sort();

            PinSnapshot.Canonical = FString::Printf(
                TEXT("%s|dir=%d|type=%s|default=%s|object=%s|text=%s|links=%s"),
                *PinSnapshot.Name,
                static_cast<int32>(Pin->Direction),
                *PinTypeToCanonical(Pin->PinType),
                *Pin->DefaultValue,
                *BlueprintMergeSnapshotPrivate::ObjectPathOrNone(Pin->DefaultObject),
                *Pin->DefaultTextValue.ToString(),
                *FString::Join(Links, TEXT(",")));
            PinSnapshot.Summary = FString::Printf(TEXT("%s (%s)"), *PinSnapshot.Name, *UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString());
            NodeSnapshot.Pins.Add(PinSnapshot.Key, PinSnapshot);
            PinCanonicalValues.Add(PinSnapshot.Canonical);

            if (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>())
            {
                SignaturePins.Add(FString::Printf(
                    TEXT("%s|%d|%s|%s"),
                    *Pin->PinName.ToString(),
                    static_cast<int32>(Pin->Direction),
                    *PinTypeToCanonical(Pin->PinType),
                    *Pin->DefaultValue));
            }
        }

        PinCanonicalValues.Sort();
        NodeSnapshot.Canonical = FString::Printf(
            TEXT("%s|x=%d|y=%d|pins=[%s]"),
            *NodeSnapshot.ClassPath,
            NodeSnapshot.PositionX,
            NodeSnapshot.PositionY,
            *FString::Join(PinCanonicalValues, TEXT(";")));
        NodeSnapshot.Summary = FString::Printf(
            TEXT("%s; %d pins; digest %s"),
            *NodeSnapshot.Title,
            NodeSnapshot.Pins.Num(),
            *BlueprintMergeSnapshotPrivate::Digest(NodeSnapshot.Canonical));
        Result.Nodes.Add(NodeSnapshot.Key, NodeSnapshot);
        // Node/pin GUIDs remain in the detailed maps for lineage-aware inspection,
        // but do not enter the whole-graph digest. Unreal may regenerate GUIDs when
        // users make side-by-side asset copies; the semantic graph is still equal.
        NodeCanonicalValues.Add(NodeSnapshot.Canonical);
    }

    NodeCanonicalValues.Sort();
    SignaturePins.Sort();
    Result.FunctionSignature = FString::Join(SignaturePins, TEXT(";"));
    Result.Canonical = FString::Printf(
        TEXT("kind=%s|signature=%s|nodes=%s"),
        *GraphKind,
        *Result.FunctionSignature,
        *FString::Join(NodeCanonicalValues, TEXT("||")));
    Result.Summary = FString::Printf(
        TEXT("%s; %d nodes; digest %s"),
        *GraphKind,
        Result.Nodes.Num(),
        *BlueprintMergeSnapshotPrivate::Digest(Result.Canonical));
    return Result;
}

FBlueprintMergeSnapshot FBlueprintMergeSnapshotBuilder::Capture(const UBlueprint* Blueprint) const
{
    FBlueprintMergeSnapshot Result;
    if (!Blueprint)
    {
        return Result;
    }

    Result.BlueprintPath = Blueprint->GetPathName();
    Result.ParentClassPath = BlueprintMergeSnapshotPrivate::ObjectPathOrNone(Blueprint->ParentClass);
    Result.BlueprintType = Blueprint->BlueprintType;

    const UObject* ClassDefaultObject = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    for (const FBPVariableDescription& Description : Blueprint->NewVariables)
    {
        FBlueprintMergeVariableSnapshot Variable;
        Variable.Name = Description.VarName;
        Variable.Guid = Description.VarGuid;
        Variable.Type = Description.VarType;
        Variable.TypeText = UEdGraphSchema_K2::TypeToText(Description.VarType).ToString();
        Variable.DefaultValue = Description.DefaultValue;
        Variable.Category = Description.Category.ToString();
        Variable.PropertyFlags = Description.PropertyFlags;
        Variable.RepNotifyFunction = Description.RepNotifyFunc;
        Variable.ReplicationCondition = Description.ReplicationCondition;
        Variable.Metadata = Description.MetaDataArray;

        if (ClassDefaultObject && Blueprint->GeneratedClass)
        {
            if (const FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, Description.VarName))
            {
                const FString CdoDefault = ExportPropertyValue(Property, ClassDefaultObject);
                if (!CdoDefault.IsEmpty())
                {
                    Variable.DefaultValue = CdoDefault;
                }
            }
        }

        TArray<FString> MetadataValues;
        for (const FBPVariableMetaDataEntry& Entry : Description.MetaDataArray)
        {
            MetadataValues.Add(Entry.DataKey.ToString() + TEXT("=") + Entry.DataValue);
        }
        MetadataValues.Sort();

        Variable.Canonical = FString::Printf(
            TEXT("type=%s|default=%s|category=%s|flags=%llu|rep=%s|condition=%d|metadata=%s"),
            *PinTypeToCanonical(Variable.Type),
            *Variable.DefaultValue,
            *Variable.Category,
            Variable.PropertyFlags,
            *Variable.RepNotifyFunction.ToString(),
            static_cast<int32>(Variable.ReplicationCondition.GetValue()),
            *FString::Join(MetadataValues, TEXT(";")));
        Variable.Summary = FString::Printf(
            TEXT("%s, Default %s"),
            *Variable.TypeText,
            Variable.DefaultValue.IsEmpty() ? TEXT("<unset>") : *Variable.DefaultValue);
        Result.Variables.Add(Variable.Name, Variable);
    }

    for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph)
        {
            Result.Functions.Add(Graph->GetFName(), CaptureGraph(Graph, TEXT("Function")));
        }
    }

    for (const UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (Graph)
        {
            Result.MacroGraphs.Add(Graph->GetFName(), CaptureGraph(Graph, TEXT("Macro Graph")));
        }
    }

    for (const UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph)
        {
            Result.EventGraphs.Add(Graph->GetFName(), CaptureGraph(Graph, TEXT("Event Graph")));
        }
    }

    if (Blueprint->SimpleConstructionScript)
    {
        for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node || Node == Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode())
            {
                continue;
            }

            FBlueprintMergeComponentSnapshot Component;
            Component.Name = Node->GetVariableName();
            Component.ClassPath = BlueprintMergeSnapshotPrivate::ObjectPathOrNone(Node->ComponentClass);
            Component.ParentName = FindComponentParentName(Blueprint, Node);
            Component.TemplateProperties = CaptureEditableProperties(Node->ComponentTemplate);
            Component.bHasBranchLocalObjectReferences = BlueprintMergeSnapshotPrivate::HasBranchLocalObjectReferences(
                Node->ComponentTemplate,
                Blueprint->GetOutermost());
            Component.Canonical = FString::Printf(
                TEXT("class=%s|parent=%s|branchLocalReferences=%d|properties=%s"),
                *Component.ClassPath,
                *Component.ParentName.ToString(),
                Component.bHasBranchLocalObjectReferences,
                *Component.TemplateProperties);
            Component.Summary = FString::Printf(
                TEXT("%s; parent %s; digest %s"),
                *Component.ClassPath,
                Component.ParentName.IsNone() ? TEXT("<root>") : *Component.ParentName.ToString(),
                *BlueprintMergeSnapshotPrivate::Digest(Component.Canonical));
            Result.Components.Add(Component.Name, Component);
        }
    }

    for (const FBPInterfaceDescription& Description : Blueprint->ImplementedInterfaces)
    {
        if (Description.Interface)
        {
            FBlueprintMergeInterfaceSnapshot Interface;
            Interface.ClassPath = Description.Interface->GetPathName();
            Interface.Canonical = Interface.ClassPath;
            Interface.Summary = Interface.ClassPath;
            Result.Interfaces.Add(Interface.ClassPath, Interface);
        }
    }

    if (ClassDefaultObject && Blueprint->GeneratedClass)
    {
        for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Property = *It;
            if (Property->GetOwnerClass() == Blueprint->GeneratedClass
                || !Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)
                || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_Deprecated | CPF_EditorOnly)
                || !BlueprintMergeSnapshotPrivate::IsDeterministicValueProperty(Property))
            {
                continue;
            }

            FBlueprintMergeDefaultPropertySnapshot DefaultProperty;
            DefaultProperty.Key = Property->GetOwnerClass()->GetPathName() + TEXT(".") + Property->GetName();
            DefaultProperty.Value = ExportPropertyValue(Property, ClassDefaultObject);
            DefaultProperty.Canonical = DefaultProperty.Value;
            DefaultProperty.Summary = DefaultProperty.Value;
            Result.DefaultProperties.Add(DefaultProperty.Key, DefaultProperty);
        }
    }

    return Result;
}
