using UnrealBuildTool;

public class BlueprintMergeDoctor : ModuleRules
{
    public BlueprintMergeDoctor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "ContentBrowser",
            "EditorSubsystem",
            "InputCore",
            "Kismet",
            "KismetCompiler",
            "LevelEditor",
            "PropertyEditor",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
