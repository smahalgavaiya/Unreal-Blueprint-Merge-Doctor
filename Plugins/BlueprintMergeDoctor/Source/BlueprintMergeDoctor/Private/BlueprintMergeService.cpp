// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "BlueprintMergeService.h"

#include "AssetToolsModule.h"
#include "BlueprintMergeEngineAdapter.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintMergeDoctorService, Log, All);

#define LOCTEXT_NAMESPACE "BlueprintMergeDoctorService"

namespace BlueprintMergeServicePrivate
{
    UBlueprint* ResolveSource(const FBlueprintMergeAnalysis& Analysis, const EBlueprintMergeSource Source)
    {
        if (Source == EBlueprintMergeSource::Ours)
        {
            return Analysis.OursBlueprint.Get();
        }
        if (Source == EBlueprintMergeSource::Theirs)
        {
            return Analysis.TheirsBlueprint.Get();
        }
        return nullptr;
    }

    bool ApplyAction(const FBlueprintMergePlannedAction& Action, UBlueprint* Source, UBlueprint* Target, FString& OutError)
    {
        switch (Action.Operation)
        {
        case EBlueprintMergeOperation::AddVariable:
            return FBlueprintMergeEngineAdapter::AddVariable(Source, Target, Action.ObjectName, OutError);
        case EBlueprintMergeOperation::AddFunction:
            return FBlueprintMergeEngineAdapter::AddFunction(Source, Target, Action.ObjectName, OutError);
        case EBlueprintMergeOperation::AddMacroGraph:
            return FBlueprintMergeEngineAdapter::AddMacroGraph(Source, Target, Action.ObjectName, OutError);
        case EBlueprintMergeOperation::AddComponent:
            return FBlueprintMergeEngineAdapter::AddComponent(Source, Target, Action.ObjectName, OutError);
        case EBlueprintMergeOperation::AddInterface:
            return FBlueprintMergeEngineAdapter::AddInterface(Source, Target, Action.ObjectName.ToString(), OutError);
        default:
            OutError = FString::Printf(TEXT("Unsupported merge operation for %s."), *Action.ObjectPath);
            return false;
        }
    }
}

FBlueprintMergeCreationResult FBlueprintMergeService::CreateMergedBlueprint(
    const FBlueprintMergeAnalysis& Analysis,
    const FString& DesiredLongPackageName) const
{
    FBlueprintMergeCreationResult Result;
    UBlueprint* Base = Analysis.BaseBlueprint.Get();
    UBlueprint* Ours = Analysis.OursBlueprint.Get();
    if (!Analysis.bInputsCompatible || !Base || !Ours)
    {
        Result.Message = Analysis.CompatibilityError.IsEmpty()
            ? TEXT("Run a compatible analysis before creating a merged Blueprint.")
            : Analysis.CompatibilityError;
        return Result;
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    FString RequestedPackage = DesiredLongPackageName;
    if (RequestedPackage.IsEmpty())
    {
        RequestedPackage = Ours->GetOutermost()->GetName() + TEXT("_Merged");
    }

    if (!FPackageName::IsValidLongPackageName(RequestedPackage))
    {
        Result.Message = FString::Printf(TEXT("Invalid output package path: %s"), *RequestedPackage);
        return Result;
    }

    FString UniquePackageName;
    FString UniqueAssetName;
    AssetTools.CreateUniqueAssetName(RequestedPackage, FString(), UniquePackageName, UniqueAssetName);
    const FString PackagePath = FPackageName::GetLongPackagePath(UniquePackageName);
    UObject* DuplicatedAsset = AssetTools.DuplicateAsset(UniqueAssetName, PackagePath, Base);
    UBlueprint* MergedBlueprint = Cast<UBlueprint>(DuplicatedAsset);
    if (!MergedBlueprint)
    {
        Result.Message = TEXT("Unreal could not duplicate BASE into a new Blueprint asset.");
        return Result;
    }

    return ApplyAndCompile(Analysis, MergedBlueprint, true);
}

FBlueprintMergeCreationResult FBlueprintMergeService::CreateTransientMergedBlueprint(
    const FBlueprintMergeAnalysis& Analysis,
    FName ObjectName) const
{
    FBlueprintMergeCreationResult Result;
    UBlueprint* Base = Analysis.BaseBlueprint.Get();
    if (!Analysis.bInputsCompatible || !Base)
    {
        Result.Message = Analysis.CompatibilityError.IsEmpty()
            ? TEXT("Run a compatible analysis before creating a merged Blueprint.")
            : Analysis.CompatibilityError;
        return Result;
    }

    if (ObjectName.IsNone())
    {
        ObjectName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_MergeDoctor_TransientMerged"));
    }
    UBlueprint* MergedBlueprint = DuplicateObject<UBlueprint>(Base, GetTransientPackage(), ObjectName);
    if (!MergedBlueprint)
    {
        Result.Message = TEXT("Could not create the transient BASE duplicate used by the merge.");
        return Result;
    }

    return ApplyAndCompile(Analysis, MergedBlueprint, false);
}

FBlueprintMergeCreationResult FBlueprintMergeService::ApplyAndCompile(
    const FBlueprintMergeAnalysis& Analysis,
    UBlueprint* MergedBlueprint,
    const bool bSaveOnSuccess) const
{
    FBlueprintMergeCreationResult Result;
    Result.MergedBlueprint = MergedBlueprint;
    Result.bCreated = MergedBlueprint != nullptr;
    Result.AssetPath = MergedBlueprint ? MergedBlueprint->GetPathName() : FString();
    if (!MergedBlueprint)
    {
        Result.Message = TEXT("Merged Blueprint duplicate is invalid.");
        return Result;
    }

    const FScopedTransaction Transaction(LOCTEXT("CreateMergedBlueprintTransaction", "Create Merged Blueprint"));
    MergedBlueprint->Modify();

    TArray<FBlueprintMergePlannedAction> RemainingActions = Analysis.SafeActions;
    RemainingActions.StableSort([](const FBlueprintMergePlannedAction& Left, const FBlueprintMergePlannedAction& Right)
    {
        auto Rank = [](const EBlueprintMergeOperation Operation)
        {
            switch (Operation)
            {
            case EBlueprintMergeOperation::AddVariable: return 0;
            case EBlueprintMergeOperation::AddInterface: return 1;
            case EBlueprintMergeOperation::AddComponent: return 2;
            case EBlueprintMergeOperation::AddFunction: return 3;
            case EBlueprintMergeOperation::AddMacroGraph: return 4;
            default: return 5;
            }
        };
        // Actions are consumed from the end of the array below, so keep lower
        // ranks (variables/interfaces/components before graph bodies) last.
        return Rank(Left.Operation) > Rank(Right.Operation);
    });

    // Component parents may themselves be safe additions, so retry deferred child
    // components after their parents have been applied.
    for (int32 Pass = 0; RemainingActions.Num() > 0 && Pass <= Analysis.SafeActions.Num(); ++Pass)
    {
        bool bMadeProgress = false;
        FString LastError;
        for (int32 Index = RemainingActions.Num() - 1; Index >= 0; --Index)
        {
            const FBlueprintMergePlannedAction& Action = RemainingActions[Index];
            UBlueprint* Source = BlueprintMergeServicePrivate::ResolveSource(Analysis, Action.Source);
            FString ActionError;
            if (BlueprintMergeServicePrivate::ApplyAction(Action, Source, MergedBlueprint, ActionError))
            {
                UE_LOG(LogBlueprintMergeDoctorService, Display, TEXT("Applied safe action: %s"), *Action.ObjectPath);
                RemainingActions.RemoveAt(Index);
                bMadeProgress = true;
            }
            else
            {
                LastError = FString::Printf(TEXT("%s: %s"), *Action.ObjectPath, *ActionError);
                if (Action.Operation != EBlueprintMergeOperation::AddComponent)
                {
                    Result.Message = TEXT("Safe merge action failed. The generated asset was preserved unsaved for inspection. ") + LastError;
                    UE_LOG(LogBlueprintMergeDoctorService, Error, TEXT("%s"), *Result.Message);
                    return Result;
                }
            }
        }

        if (!bMadeProgress)
        {
            Result.Message = TEXT("Safe component additions could not be ordered. The generated asset was preserved unsaved for inspection. ") + LastError;
            UE_LOG(LogBlueprintMergeDoctorService, Error, TEXT("%s"), *Result.Message);
            return Result;
        }
    }

    if (RemainingActions.Num() > 0)
    {
        Result.Message = TEXT("Not all safe actions could be applied. The generated asset was preserved unsaved for inspection.");
        return Result;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(MergedBlueprint);
    FString CompileLog;
    Result.bCompiled = FBlueprintMergeEngineAdapter::Compile(
        MergedBlueprint, Result.CompileErrors, Result.CompileWarnings, CompileLog);
    if (!Result.bCompiled)
    {
        Result.Message = FString::Printf(
            TEXT("Compile failed with %d error(s). No original asset was touched and the generated asset remains unsaved for inspection.%s%s"),
            Result.CompileErrors,
            CompileLog.IsEmpty() ? TEXT("") : TEXT("\n"),
            *CompileLog);
        return Result;
    }

    if (bSaveOnSuccess)
    {
        UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
        Result.bSaved = AssetSubsystem && AssetSubsystem->SaveLoadedAsset(MergedBlueprint, false);
        if (!Result.bSaved)
        {
            Result.Message = TEXT("The merged Blueprint compiled, but Unreal could not save the new asset. It remains loaded for inspection.");
            return Result;
        }
    }

    Result.Message = FString::Printf(
        TEXT("Merged Blueprint compiled successfully with %d warning(s). Applied %d deterministic change(s); %d item(s) remain for manual review."),
        Result.CompileWarnings,
        Analysis.SafeActions.Num(),
        Analysis.GetManualReviewCount());
    return Result;
}

#undef LOCTEXT_NAMESPACE
