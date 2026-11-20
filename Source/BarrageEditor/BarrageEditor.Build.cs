using UnrealBuildTool;
public class BarrageEditor : ModuleRules
{
    public BarrageEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "BarrageRuntime", "UnrealEd", "AssetDefinition", "AssetTools", "Slate", "SlateCore", "PropertyEditor" });
    }
}
