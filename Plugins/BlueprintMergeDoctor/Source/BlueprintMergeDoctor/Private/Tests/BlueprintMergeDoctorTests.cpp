// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "BlueprintMergeAnalyzer.h"
#include "BlueprintMergeService.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace BlueprintMergeDoctorTests
{
    constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    UBlueprint* CreateBlueprint(const TCHAR* BaseName)
    {
        const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(BaseName));
        UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(),
            GetTransientPackage(),
            UniqueName,
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            TEXT("BlueprintMergeDoctorTests"));
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
        return Blueprint;
    }

    UBlueprint* DuplicateBranch(UBlueprint* Source, const TCHAR* BaseName)
    {
        const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(BaseName));
        return DuplicateObject<UBlueprint>(Source, GetTransientPackage(), UniqueName);
    }

    FEdGraphPinType FloatType()
    {
        return FEdGraphPinType(
            UEdGraphSchema_K2::PC_Real,
            UEdGraphSchema_K2::PC_Float,
            nullptr,
            EPinContainerType::None,
            false,
            FEdGraphTerminalType());
    }

    FEdGraphPinType IntegerType()
    {
        return FEdGraphPinType(
            UEdGraphSchema_K2::PC_Int,
            NAME_None,
            nullptr,
            EPinContainerType::None,
            false,
            FEdGraphTerminalType());
    }

    bool AddVariable(UBlueprint* Blueprint, const FName Name, const FEdGraphPinType& Type, const FString& DefaultValue)
    {
        const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, DefaultValue);
        if (bAdded)
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
        }
        return bAdded;
    }

    bool SetVariableDefault(UBlueprint* Blueprint, const FName Name, const FString& DefaultValue)
    {
        FBPVariableDescription* Description = Blueprint->NewVariables.FindByPredicate([Name](const FBPVariableDescription& Candidate)
        {
            return Candidate.VarName == Name;
        });
        if (!Description)
        {
            return false;
        }

        Description->DefaultValue = DefaultValue;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
        return true;
    }

    bool AddFunction(UBlueprint* Blueprint, const FName Name)
    {
        if (!Blueprint)
        {
            return false;
        }

        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        if (!Graph)
        {
            return false;
        }

        FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, static_cast<UFunction*>(nullptr));
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
        return Blueprint->Status != BS_Error;
    }

    bool AddComponent(UBlueprint* Blueprint, const FName Name)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
        {
            return false;
        }

        USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(USceneComponent::StaticClass(), Name);
        if (!Node)
        {
            return false;
        }

        Blueprint->SimpleConstructionScript->AddNode(Node);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
        return Blueprint->Status != BS_Error;
    }

    const FBlueprintMergeChange* FindChange(
        const FBlueprintMergeAnalysis& Analysis,
        const FString& Category,
        const FString& Name)
    {
        return Analysis.Changes.FindByPredicate([&Category, &Name](const FBlueprintMergeChange& Change)
        {
            return Change.Category == Category && Change.DisplayName == Name;
        });
    }

    bool HasVariable(const UBlueprint* Blueprint, const FName Name)
    {
        const FBPVariableDescription* Description = Blueprint ? Blueprint->NewVariables.FindByPredicate([Name](const FBPVariableDescription& Candidate)
        {
            return Candidate.VarName == Name;
        }) : nullptr;
        return Description != nullptr;
    }

    bool HasNumericDefault(const UBlueprint* Blueprint, const FName Name, const double Expected)
    {
        if (!Blueprint || !Blueprint->GeneratedClass)
        {
            return false;
        }

        const FNumericProperty* Property = FindFProperty<FNumericProperty>(Blueprint->GeneratedClass, Name);
        const UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject(false);
        if (!Property || !DefaultObject)
        {
            return false;
        }

        const void* Value = Property->ContainerPtrToValuePtr<void>(DefaultObject);
        const double Actual = Property->IsFloatingPoint()
            ? Property->GetFloatingPointPropertyValue(Value)
            : static_cast<double>(Property->GetSignedIntPropertyValue(Value));
        return FMath::IsNearlyEqual(Actual, Expected);
    }

    bool HasGraph(const TArray<TObjectPtr<UEdGraph>>& Graphs, const FName Name)
    {
        return Graphs.ContainsByPredicate([Name](const UEdGraph* Graph) { return Graph && Graph->GetFName() == Name; });
    }

    bool HasComponent(const UBlueprint* Blueprint, const FName Name)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
        {
            return false;
        }
        return Blueprint->SimpleConstructionScript->GetAllNodes().ContainsByPredicate([Name](const USCS_Node* Node)
        {
            return Node && Node->GetVariableName() == Name;
        });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeVariableAddedOursTest,
    "BlueprintMergeDoctor.MergeRules.VariableAddedOnlyInOurs",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeVariableAddedOursTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_VarOurs"));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_VarOurs"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_VarOurs"));
    TestTrue(TEXT("Stamina fixture added"), AddVariable(Ours, TEXT("Stamina"), FloatType(), TEXT("100.0")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeChange* Change = FindChange(Analysis, TEXT("Variable"), TEXT("Stamina"));
    TestNotNull(TEXT("Stamina change detected"), Change);
    if (Change)
    {
        TestTrue(TEXT("Stamina is safe"), Change->bSafeToMerge);
        TestEqual(TEXT("Stamina classification"), Change->Classification, EBlueprintMergeClassification::OursOnly);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeVariableAddedTheirsTest,
    "BlueprintMergeDoctor.MergeRules.VariableAddedOnlyInTheirs",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeVariableAddedTheirsTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_VarTheirs"));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_VarTheirs"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_VarTheirs"));
    TestTrue(TEXT("Ammo fixture added"), AddVariable(Theirs, TEXT("Ammo"), IntegerType(), TEXT("30")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeChange* Change = FindChange(Analysis, TEXT("Variable"), TEXT("Ammo"));
    TestNotNull(TEXT("Ammo change detected"), Change);
    if (Change)
    {
        TestTrue(TEXT("Ammo is safe"), Change->bSafeToMerge);
        TestEqual(TEXT("Ammo classification"), Change->Classification, EBlueprintMergeClassification::TheirsOnly);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeIndependentVariablesTest,
    "BlueprintMergeDoctor.MergeRules.DifferentVariablesAddedOnBothSides",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeIndependentVariablesTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_IndependentVars"));
    TestTrue(TEXT("Health fixture added"), AddVariable(Base, TEXT("Health"), FloatType(), TEXT("100.0")));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_IndependentVars"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_IndependentVars"));
    TestTrue(TEXT("Stamina fixture added"), AddVariable(Ours, TEXT("Stamina"), FloatType(), TEXT("100.0")));
    TestTrue(TEXT("Ammo fixture added"), AddVariable(Theirs, TEXT("Ammo"), IntegerType(), TEXT("30")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    TestEqual(TEXT("Two safe variable actions"), Analysis.SafeActions.Num(), 2);
    TestEqual(TEXT("Independent variables create no conflicts"), Analysis.GetConflictCount(), 0);
    const FBlueprintMergeCreationResult Merge = FBlueprintMergeService().CreateTransientMergedBlueprint(Analysis);
    TestTrue(TEXT("Merged Blueprint created"), Merge.bCreated);
    TestTrue(TEXT("Merged Blueprint compiled"), Merge.bCompiled);
    TestTrue(TEXT("Health retained"), HasVariable(Merge.MergedBlueprint.Get(), TEXT("Health")));
    TestTrue(TEXT("Stamina merged"), HasVariable(Merge.MergedBlueprint.Get(), TEXT("Stamina")));
    TestTrue(TEXT("Ammo merged"), HasVariable(Merge.MergedBlueprint.Get(), TEXT("Ammo")));
    TestTrue(TEXT("Health default retained"), HasNumericDefault(Merge.MergedBlueprint.Get(), TEXT("Health"), 100.0));
    TestTrue(TEXT("Stamina default merged"), HasNumericDefault(Merge.MergedBlueprint.Get(), TEXT("Stamina"), 100.0));
    TestTrue(TEXT("Ammo default merged"), HasNumericDefault(Merge.MergedBlueprint.Get(), TEXT("Ammo"), 30.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeIdenticalVariableTest,
    "BlueprintMergeDoctor.MergeRules.IdenticalVariableAddedByBothSides",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeIdenticalVariableTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_IdenticalVar"));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_IdenticalVar"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_IdenticalVar"));
    TestTrue(TEXT("Ours variable added"), AddVariable(Ours, TEXT("Stamina"), FloatType(), TEXT("100.0")));
    TestTrue(TEXT("Theirs variable added"), AddVariable(Theirs, TEXT("Stamina"), FloatType(), TEXT("100.0")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeChange* Change = FindChange(Analysis, TEXT("Variable"), TEXT("Stamina"));
    TestNotNull(TEXT("Identical addition detected"), Change);
    if (Change)
    {
        TestEqual(TEXT("Identical classification"), Change->Classification, EBlueprintMergeClassification::IdenticalChange);
        TestTrue(TEXT("Identical addition safe"), Change->bSafeToMerge);
    }
    TestEqual(TEXT("One action for identical addition"), Analysis.SafeActions.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeVariableConflictTest,
    "BlueprintMergeDoctor.MergeRules.SameVariableModifiedDifferently",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeVariableConflictTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_VarConflict"));
    TestTrue(TEXT("MaxHealth fixture added"), AddVariable(Base, TEXT("MaxHealth"), FloatType(), TEXT("100.0")));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_VarConflict"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_VarConflict"));
    TestTrue(TEXT("Ours default changed"), SetVariableDefault(Ours, TEXT("MaxHealth"), TEXT("120.0")));
    TestTrue(TEXT("Theirs default changed"), SetVariableDefault(Theirs, TEXT("MaxHealth"), TEXT("150.0")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeChange* Change = FindChange(Analysis, TEXT("Variable"), TEXT("MaxHealth"));
    TestNotNull(TEXT("MaxHealth change detected"), Change);
    if (Change)
    {
        TestFalse(TEXT("Conflict is not safe"), Change->bSafeToMerge);
        TestEqual(TEXT("Conflict classification"), Change->Classification, EBlueprintMergeClassification::PotentialConflict);
    }
    TestEqual(TEXT("No automatic conflict action"), Analysis.SafeActions.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeFunctionAddedOursTest,
    "BlueprintMergeDoctor.MergeRules.FunctionAddedOnlyInOurs",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeFunctionAddedOursTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_FunctionOurs"));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_FunctionOurs"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_FunctionOurs"));
    TestTrue(TEXT("Function fixture added"), AddFunction(Ours, TEXT("ConsumeStamina")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeChange* Change = FindChange(Analysis, TEXT("Function"), TEXT("ConsumeStamina"));
    TestNotNull(TEXT("Function change detected"), Change);
    if (Change) { TestTrue(TEXT("Function addition safe"), Change->bSafeToMerge); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeIndependentFunctionsTest,
    "BlueprintMergeDoctor.MergeRules.FunctionsIndependentlyAdded",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeIndependentFunctionsTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_IndependentFunctions"));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_IndependentFunctions"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_IndependentFunctions"));
    TestTrue(TEXT("ConsumeStamina fixture added"), AddFunction(Ours, TEXT("ConsumeStamina")));
    TestTrue(TEXT("PerformAttack fixture added"), AddFunction(Theirs, TEXT("PerformAttack")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    TestEqual(TEXT("Two safe function actions"), Analysis.SafeActions.Num(), 2);
    TestEqual(TEXT("Independent functions create no conflicts"), Analysis.GetConflictCount(), 0);
    const FBlueprintMergeCreationResult Merge = FBlueprintMergeService().CreateTransientMergedBlueprint(Analysis);
    TestTrue(TEXT("Merged functions compile"), Merge.bCompiled);
    TestTrue(TEXT("ConsumeStamina merged"), Merge.MergedBlueprint.IsValid() && HasGraph(Merge.MergedBlueprint->FunctionGraphs, TEXT("ConsumeStamina")));
    TestTrue(TEXT("PerformAttack merged"), Merge.MergedBlueprint.IsValid() && HasGraph(Merge.MergedBlueprint->FunctionGraphs, TEXT("PerformAttack")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeIndependentComponentsTest,
    "BlueprintMergeDoctor.MergeRules.ComponentsIndependentlyAdded",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeIndependentComponentsTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_IndependentComponents"));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_IndependentComponents"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_IndependentComponents"));
    TestTrue(TEXT("Ours component added"), AddComponent(Ours, TEXT("OursScene")));
    TestTrue(TEXT("Theirs component added"), AddComponent(Theirs, TEXT("TheirsScene")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    TestEqual(TEXT("Independent components create no conflicts"), Analysis.GetConflictCount(), 0);
    const FBlueprintMergeCreationResult Merge = FBlueprintMergeService().CreateTransientMergedBlueprint(Analysis);
    TestTrue(TEXT("Merged components compile"), Merge.bCompiled);
    TestTrue(TEXT("Ours component merged"), HasComponent(Merge.MergedBlueprint.Get(), TEXT("OursScene")));
    TestTrue(TEXT("Theirs component merged"), HasComponent(Merge.MergedBlueprint.Get(), TEXT("TheirsScene")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeDefaultPropertyConflictTest,
    "BlueprintMergeDoctor.MergeRules.SameDefaultPropertyModifiedDifferently",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeDefaultPropertyConflictTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_DefaultConflict"));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_DefaultConflict"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_DefaultConflict"));
    CastChecked<AActor>(Ours->GeneratedClass->GetDefaultObject())->InitialLifeSpan = 12.0f;
    CastChecked<AActor>(Theirs->GeneratedClass->GetDefaultObject())->InitialLifeSpan = 24.0f;

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeChange* Change = Analysis.Changes.FindByPredicate([](const FBlueprintMergeChange& Candidate)
    {
        return Candidate.Category == TEXT("Default Property") && Candidate.DisplayName.EndsWith(TEXT(".InitialLifeSpan"));
    });
    TestNotNull(TEXT("InitialLifeSpan conflict detected"), Change);
    if (Change)
    {
        TestEqual(TEXT("Default conflict classification"), Change->Classification, EBlueprintMergeClassification::PotentialConflict);
        TestFalse(TEXT("Default conflict not safe"), Change->bSafeToMerge);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeResultCompileTest,
    "BlueprintMergeDoctor.MergeRules.ResultingBlueprintCompiles",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeResultCompileTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_Compile"));
    TestTrue(TEXT("Health fixture added"), AddVariable(Base, TEXT("Health"), FloatType(), TEXT("100.0")));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_Compile"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_Compile"));
    TestTrue(TEXT("Stamina fixture added"), AddVariable(Ours, TEXT("Stamina"), FloatType(), TEXT("100.0")));
    TestTrue(TEXT("Ours function added"), AddFunction(Ours, TEXT("ConsumeStamina")));
    TestTrue(TEXT("Ammo fixture added"), AddVariable(Theirs, TEXT("Ammo"), IntegerType(), TEXT("30")));
    TestTrue(TEXT("Theirs function added"), AddFunction(Theirs, TEXT("PerformAttack")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeCreationResult Merge = FBlueprintMergeService().CreateTransientMergedBlueprint(Analysis);
    TestTrue(TEXT("Combined deterministic merge created"), Merge.bCreated);
    TestTrue(TEXT("Combined deterministic merge compiled"), Merge.bCompiled);
    TestEqual(TEXT("No compile errors"), Merge.CompileErrors, 0);
    TestTrue(TEXT("All four additions planned"), Analysis.SafeActions.Num() == 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeExactPrototypeFlowTest,
    "BlueprintMergeDoctor.EndToEnd.FourSafeChangesOneConflict",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeExactPrototypeFlowTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_ExactFlow"));
    TestTrue(TEXT("Health fixture added"), AddVariable(Base, TEXT("Health"), FloatType(), TEXT("100.0")));
    TestTrue(TEXT("MaxHealth fixture added"), AddVariable(Base, TEXT("MaxHealth"), FloatType(), TEXT("100.0")));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_ExactFlow"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_ExactFlow"));

    TestTrue(TEXT("Stamina fixture added"), AddVariable(Ours, TEXT("Stamina"), FloatType(), TEXT("100.0")));
    TestTrue(TEXT("ConsumeStamina fixture added"), AddFunction(Ours, TEXT("ConsumeStamina")));
    TestTrue(TEXT("Ours MaxHealth changed"), SetVariableDefault(Ours, TEXT("MaxHealth"), TEXT("120.0")));
    TestTrue(TEXT("Ammo fixture added"), AddVariable(Theirs, TEXT("Ammo"), IntegerType(), TEXT("30")));
    TestTrue(TEXT("PerformAttack fixture added"), AddFunction(Theirs, TEXT("PerformAttack")));
    TestTrue(TEXT("Theirs MaxHealth changed"), SetVariableDefault(Theirs, TEXT("MaxHealth"), TEXT("150.0")));

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    TestEqual(TEXT("Exactly four deterministic actions"), Analysis.SafeActions.Num(), 4);
    const FBlueprintMergeChange* Conflict = FindChange(Analysis, TEXT("Variable"), TEXT("MaxHealth"));
    TestNotNull(TEXT("MaxHealth conflict isolated"), Conflict);
    if (Conflict)
    {
        TestEqual(TEXT("MaxHealth classified as conflict"), Conflict->Classification, EBlueprintMergeClassification::PotentialConflict);
        TestFalse(TEXT("MaxHealth is not auto-resolved"), Conflict->bSafeToMerge);
    }

    const FBlueprintMergeCreationResult Merge = FBlueprintMergeService().CreateTransientMergedBlueprint(Analysis);
    TestTrue(TEXT("Exact-flow merged Blueprint compiles"), Merge.bCompiled);
    TestTrue(TEXT("Stamina merged"), HasNumericDefault(Merge.MergedBlueprint.Get(), TEXT("Stamina"), 100.0));
    TestTrue(TEXT("Ammo merged"), HasNumericDefault(Merge.MergedBlueprint.Get(), TEXT("Ammo"), 30.0));
    TestTrue(TEXT("ConsumeStamina merged"), Merge.MergedBlueprint.IsValid() && HasGraph(Merge.MergedBlueprint->FunctionGraphs, TEXT("ConsumeStamina")));
    TestTrue(TEXT("PerformAttack merged"), Merge.MergedBlueprint.IsValid() && HasGraph(Merge.MergedBlueprint->FunctionGraphs, TEXT("PerformAttack")));
    TestTrue(TEXT("Conflicting MaxHealth remains at BASE"), HasNumericDefault(Merge.MergedBlueprint.Get(), TEXT("MaxHealth"), 100.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBlueprintMergeGraphNodeConflictTest,
    "BlueprintMergeDoctor.GraphInspection.SameNodeMovedDifferently",
    BlueprintMergeDoctorTests::TestFlags)

bool FBlueprintMergeGraphNodeConflictTest::RunTest(const FString& Parameters)
{
    using namespace BlueprintMergeDoctorTests;
    UBlueprint* Base = CreateBlueprint(TEXT("BP_BMD_Base_NodeConflict"));
    TestTrue(TEXT("Base function added"), AddFunction(Base, TEXT("CalculateValue")));
    UBlueprint* Ours = DuplicateBranch(Base, TEXT("BP_BMD_Ours_NodeConflict"));
    UBlueprint* Theirs = DuplicateBranch(Base, TEXT("BP_BMD_Theirs_NodeConflict"));

    UEdGraph* BaseGraph = Base->FunctionGraphs.FindByPredicate([](const UEdGraph* Graph)
    {
        return Graph && Graph->GetFName() == TEXT("CalculateValue");
    })->Get();
    UEdGraph* OursGraph = Ours->FunctionGraphs.FindByPredicate([](const UEdGraph* Graph)
    {
        return Graph && Graph->GetFName() == TEXT("CalculateValue");
    })->Get();
    UEdGraph* TheirsGraph = Theirs->FunctionGraphs.FindByPredicate([](const UEdGraph* Graph)
    {
        return Graph && Graph->GetFName() == TEXT("CalculateValue");
    })->Get();

    TestTrue(TEXT("Fixture graphs have entry nodes"), BaseGraph->Nodes.Num() > 0 && OursGraph->Nodes.Num() > 0 && TheirsGraph->Nodes.Num() > 0);
    if (BaseGraph->Nodes.Num() == 0 || OursGraph->Nodes.Num() == 0 || TheirsGraph->Nodes.Num() == 0)
    {
        return false;
    }

    // Preserve a common lineage GUID explicitly so this test exercises the
    // stable-ID path even though DuplicateObject may regenerate copy GUIDs.
    OursGraph->Nodes[0]->NodeGuid = BaseGraph->Nodes[0]->NodeGuid;
    TheirsGraph->Nodes[0]->NodeGuid = BaseGraph->Nodes[0]->NodeGuid;
    OursGraph->Nodes[0]->NodePosX = 120;
    TheirsGraph->Nodes[0]->NodePosX = 240;

    const FBlueprintMergeAnalysis Analysis = FBlueprintMergeAnalyzer().Analyze(Base, Ours, Theirs);
    const FBlueprintMergeChange* FunctionConflict = FindChange(Analysis, TEXT("Function"), TEXT("CalculateValue"));
    TestNotNull(TEXT("Function body conflict detected"), FunctionConflict);
    if (FunctionConflict)
    {
        TestEqual(TEXT("Function body classified as conflict"), FunctionConflict->Classification, EBlueprintMergeClassification::PotentialConflict);
    }

    const FString NodeGuid = BaseGraph->Nodes[0]->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
    const FBlueprintMergeChange* NodeConflict = FindChange(Analysis, TEXT("Graph Node"), NodeGuid);
    TestNotNull(TEXT("GUID-addressed node conflict detected"), NodeConflict);
    if (NodeConflict)
    {
        TestEqual(TEXT("Node classified as conflict"), NodeConflict->Classification, EBlueprintMergeClassification::PotentialConflict);
        TestFalse(TEXT("Node edit not auto-merged"), NodeConflict->bSafeToMerge);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
