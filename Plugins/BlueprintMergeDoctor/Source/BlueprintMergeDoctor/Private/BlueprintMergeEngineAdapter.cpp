// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "BlueprintMergeEngineAdapter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintMergeDoctorAdapter, Log, All);

namespace BlueprintMergeAdapterPrivate
{
    const FBPVariableDescription* FindVariable(const UBlueprint* Blueprint, const FName Name)
    {
        if (!Blueprint)
        {
            return nullptr;
        }

        return Blueprint->NewVariables.FindByPredicate([Name](const FBPVariableDescription& Description)
        {
            return Description.VarName == Name;
        });
    }

    FBPVariableDescription* FindVariable(UBlueprint* Blueprint, const FName Name)
    {
        return const_cast<FBPVariableDescription*>(FindVariable(static_cast<const UBlueprint*>(Blueprint), Name));
    }

    FString GetEffectiveVariableDefault(const UBlueprint* Blueprint, const FBPVariableDescription& Description)
    {
        if (Blueprint && Blueprint->GeneratedClass)
        {
            const UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject(false);
            if (DefaultObject)
            {
                if (const FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, Description.VarName))
                {
                    FString ExportedValue;
                    const void* Value = Property->ContainerPtrToValuePtr<void>(DefaultObject);
                    Property->ExportTextItem_Direct(
                        ExportedValue,
                        Value,
                        nullptr,
                        const_cast<UObject*>(DefaultObject),
                        PPF_SerializedAsImportText);
                    if (!ExportedValue.IsEmpty())
                    {
                        return ExportedValue;
                    }
                }
            }
        }
        return Description.DefaultValue;
    }

    USCS_Node* FindComponentNode(const UBlueprint* Blueprint, const FName Name)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
        {
            return nullptr;
        }

        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->GetVariableName() == Name)
            {
                return Node;
            }
        }
        return nullptr;
    }

    FName FindParentName(const UBlueprint* Blueprint, const USCS_Node* Child)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript || !Child)
        {
            return NAME_None;
        }

        for (const USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Candidate && Candidate->GetChildNodes().Contains(Child))
            {
                return Candidate->GetVariableName();
            }
        }
        return Child->ParentComponentOrVariableName;
    }

    void RefreshSelfMemberReferences(UEdGraph* Graph, const UBlueprint* Target)
    {
        if (!Graph || !Target)
        {
            return;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
            {
                if (VariableNode->VariableReference.IsSelfContext())
                {
                    const FName VariableName = VariableNode->GetVarName();
                    if (const FBPVariableDescription* Description = FindVariable(Target, VariableName))
                    {
                        VariableNode->VariableReference.SetSelfMember(VariableName, Description->VarGuid);
                    }
                }
            }

            if (UK2Node_CallFunction* FunctionNode = Cast<UK2Node_CallFunction>(Node))
            {
                if (FunctionNode->FunctionReference.IsSelfContext())
                {
                    FunctionNode->FunctionReference.SetSelfMember(FunctionNode->FunctionReference.GetMemberName());
                }
            }
        }
    }
}

bool FBlueprintMergeEngineAdapter::AddVariable(UBlueprint* Source, UBlueprint* Target, const FName VariableName, FString& OutError)
{
    const FBPVariableDescription* SourceDescription = BlueprintMergeAdapterPrivate::FindVariable(Source, VariableName);
    if (!SourceDescription || !Target)
    {
        OutError = FString::Printf(TEXT("Variable %s was not found in the selected source Blueprint."), *VariableName.ToString());
        return false;
    }

    if (BlueprintMergeAdapterPrivate::FindVariable(Target, VariableName))
    {
        OutError = FString::Printf(TEXT("Target already contains variable %s."), *VariableName.ToString());
        return false;
    }

    const FString EffectiveDefault = BlueprintMergeAdapterPrivate::GetEffectiveVariableDefault(Source, *SourceDescription);
    if (!FBlueprintEditorUtils::AddMemberVariable(Target, VariableName, SourceDescription->VarType, EffectiveDefault))
    {
        OutError = FString::Printf(TEXT("Unreal rejected variable %s while applying the safe merge plan."), *VariableName.ToString());
        return false;
    }

    FBPVariableDescription* TargetDescription = BlueprintMergeAdapterPrivate::FindVariable(Target, VariableName);
    if (!TargetDescription)
    {
        OutError = FString::Printf(TEXT("Variable %s was added but its description could not be located."), *VariableName.ToString());
        return false;
    }

    TargetDescription->VarGuid = SourceDescription->VarGuid;
    TargetDescription->FriendlyName = SourceDescription->FriendlyName;
    TargetDescription->Category = SourceDescription->Category;
    TargetDescription->PropertyFlags = SourceDescription->PropertyFlags;
    TargetDescription->RepNotifyFunc = SourceDescription->RepNotifyFunc;
    TargetDescription->ReplicationCondition = SourceDescription->ReplicationCondition;
    TargetDescription->MetaDataArray = SourceDescription->MetaDataArray;
    TargetDescription->DefaultValue = EffectiveDefault;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
    return true;
}

UEdGraph* FBlueprintMergeEngineAdapter::CloneGraph(
    UBlueprint* Source,
    UBlueprint* Target,
    const FName GraphName,
    const bool bFunctionGraph,
    FString& OutError)
{
    if (!Source || !Target)
    {
        OutError = TEXT("Source or target Blueprint is invalid.");
        return nullptr;
    }

    const TArray<TObjectPtr<UEdGraph>>& SourceGraphs = bFunctionGraph ? Source->FunctionGraphs : Source->MacroGraphs;
    const TArray<TObjectPtr<UEdGraph>>& TargetGraphs = bFunctionGraph ? Target->FunctionGraphs : Target->MacroGraphs;
    const TObjectPtr<UEdGraph>* SourceGraph = SourceGraphs.FindByPredicate([GraphName](const UEdGraph* Graph)
    {
        return Graph && Graph->GetFName() == GraphName;
    });

    if (!SourceGraph)
    {
        OutError = FString::Printf(TEXT("Graph %s was not found in the selected source Blueprint."), *GraphName.ToString());
        return nullptr;
    }

    if (TargetGraphs.ContainsByPredicate([GraphName](const UEdGraph* Graph) { return Graph && Graph->GetFName() == GraphName; }))
    {
        OutError = FString::Printf(TEXT("Target already contains graph %s."), *GraphName.ToString());
        return nullptr;
    }

    FCompilerResultsLog CloneLog;
    UEdGraph* ClonedGraph = FEdGraphUtilities::CloneGraph(SourceGraph->Get(), Target, &CloneLog, false);
    if (!ClonedGraph)
    {
        OutError = FString::Printf(TEXT("Unreal failed to clone graph %s."), *GraphName.ToString());
        return nullptr;
    }

    ClonedGraph->SetFlags(RF_Transactional);
    if (ClonedGraph->GetFName() != GraphName)
    {
        ClonedGraph->Rename(*GraphName.ToString(), Target, REN_DontCreateRedirectors | REN_DoNotDirty);
    }

    BlueprintMergeAdapterPrivate::RefreshSelfMemberReferences(ClonedGraph, Target);
    if (bFunctionGraph)
    {
        Target->FunctionGraphs.Add(ClonedGraph);
    }
    else
    {
        Target->MacroGraphs.Add(ClonedGraph);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
    return ClonedGraph;
}

bool FBlueprintMergeEngineAdapter::AddFunction(UBlueprint* Source, UBlueprint* Target, const FName FunctionName, FString& OutError)
{
    return CloneGraph(Source, Target, FunctionName, true, OutError) != nullptr;
}

bool FBlueprintMergeEngineAdapter::AddMacroGraph(UBlueprint* Source, UBlueprint* Target, const FName GraphName, FString& OutError)
{
    return CloneGraph(Source, Target, GraphName, false, OutError) != nullptr;
}

bool FBlueprintMergeEngineAdapter::AddComponent(UBlueprint* Source, UBlueprint* Target, const FName ComponentName, FString& OutError)
{
    if (!Source || !Target || !Source->SimpleConstructionScript || !Target->SimpleConstructionScript)
    {
        OutError = TEXT("Component merge requires actor Blueprints with Simple Construction Scripts.");
        return false;
    }

    USCS_Node* SourceNode = BlueprintMergeAdapterPrivate::FindComponentNode(Source, ComponentName);
    if (!SourceNode || !SourceNode->ComponentClass || !SourceNode->ComponentTemplate)
    {
        OutError = FString::Printf(TEXT("Component %s is incomplete or was not found in the source Blueprint."), *ComponentName.ToString());
        return false;
    }

    if (BlueprintMergeAdapterPrivate::FindComponentNode(Target, ComponentName))
    {
        OutError = FString::Printf(TEXT("Target already contains component %s."), *ComponentName.ToString());
        return false;
    }

    const FName ParentName = BlueprintMergeAdapterPrivate::FindParentName(Source, SourceNode);
    USCS_Node* TargetParent = ParentName.IsNone() ? nullptr : BlueprintMergeAdapterPrivate::FindComponentNode(Target, ParentName);
    if (!ParentName.IsNone() && !TargetParent)
    {
        OutError = FString::Printf(TEXT("Parent component %s must be merged before child component %s."), *ParentName.ToString(), *ComponentName.ToString());
        return false;
    }

    USCS_Node* NewNode = Target->SimpleConstructionScript->CreateNode(SourceNode->ComponentClass, ComponentName);
    if (!NewNode || !NewNode->ComponentTemplate)
    {
        OutError = FString::Printf(TEXT("Unreal could not create component %s."), *ComponentName.ToString());
        return false;
    }

    UEngine::CopyPropertiesForUnrelatedObjects(SourceNode->ComponentTemplate, NewNode->ComponentTemplate);
    NewNode->CategoryName = SourceNode->CategoryName;
    NewNode->AttachToName = SourceNode->AttachToName;
    NewNode->MetaDataArray = SourceNode->MetaDataArray;
    NewNode->VariableGuid = SourceNode->VariableGuid;

    if (TargetParent)
    {
        TargetParent->AddChildNode(NewNode);
    }
    else
    {
        Target->SimpleConstructionScript->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
    return true;
}

bool FBlueprintMergeEngineAdapter::AddInterface(
    UBlueprint* Source,
    UBlueprint* Target,
    const FString& InterfaceClassPath,
    FString& OutError)
{
    if (!Source || !Target)
    {
        OutError = TEXT("Source or target Blueprint is invalid.");
        return false;
    }

    const FBPInterfaceDescription* SourceInterface = Source->ImplementedInterfaces.FindByPredicate(
        [&InterfaceClassPath](const FBPInterfaceDescription& Description)
        {
            return Description.Interface && Description.Interface->GetPathName() == InterfaceClassPath;
        });

    if (!SourceInterface || !SourceInterface->Interface)
    {
        OutError = FString::Printf(TEXT("Interface %s was not found in the source Blueprint."), *InterfaceClassPath);
        return false;
    }

    if (!FBlueprintEditorUtils::ImplementNewInterface(Target, SourceInterface->Interface->GetClassPathName()))
    {
        OutError = FString::Printf(TEXT("Unreal rejected interface %s while applying the safe merge plan."), *InterfaceClassPath);
        return false;
    }
    return true;
}

bool FBlueprintMergeEngineAdapter::Compile(UBlueprint* Blueprint, int32& OutErrors, int32& OutWarnings, FString& OutLog)
{
    OutErrors = 0;
    OutWarnings = 0;
    OutLog.Reset();
    if (!Blueprint)
    {
        OutLog = TEXT("Merged Blueprint is invalid.");
        OutErrors = 1;
        return false;
    }

    FCompilerResultsLog Results;
    Results.bSilentMode = true;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &Results);
    OutErrors = Results.NumErrors;
    OutWarnings = Results.NumWarnings;

    TArray<FString> Lines;
    for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
    {
        Lines.Add(Message->ToText().ToString());
    }
    OutLog = FString::Join(Lines, TEXT("\n"));

    const bool bSuccess = Results.NumErrors == 0 && Blueprint->Status != BS_Error;
    UE_LOG(LogBlueprintMergeDoctorAdapter, Display,
        TEXT("Compiled merged Blueprint %s: success=%d errors=%d warnings=%d"),
        *Blueprint->GetPathName(), bSuccess, OutErrors, OutWarnings);
    return bSuccess;
}
