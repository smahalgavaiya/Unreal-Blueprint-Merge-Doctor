// Copyright Blueprint Merge Doctor. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "SBlueprintMergeDoctorWindow.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "BlueprintMergeDoctorModule"

class FBlueprintMergeDoctorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,
            FOnSpawnTab::CreateRaw(this, &FBlueprintMergeDoctorModule::SpawnPluginTab))
            .SetDisplayName(LOCTEXT("TabTitle", "Blueprint Merge Doctor"))
            .SetTooltipText(LOCTEXT("TabTooltip", "Analyze and safely merge three Blueprint versions"))
            .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("BlueprintEditor.Diff")));

        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlueprintMergeDoctorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
    }

private:
    TSharedRef<SDockTab> SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SBlueprintMergeDoctorWindow)
            ];
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
        UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("SourceControl"));
        Section.AddMenuEntry(
            TEXT("OpenBlueprintMergeDoctor"),
            LOCTEXT("MenuLabel", "Blueprint Merge Doctor"),
            LOCTEXT("MenuTooltip", "Compare BASE, OURS, and THEIRS Blueprint assets and create a deterministic safe merge."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("BlueprintEditor.Diff")),
            FUIAction(FExecuteAction::CreateLambda([]
            {
                FGlobalTabmanager::Get()->TryInvokeTab(TabName);
            })));
    }

    static const FName TabName;
};

const FName FBlueprintMergeDoctorModule::TabName(TEXT("BlueprintMergeDoctor"));

IMPLEMENT_MODULE(FBlueprintMergeDoctorModule, BlueprintMergeDoctor)

#undef LOCTEXT_NAMESPACE
