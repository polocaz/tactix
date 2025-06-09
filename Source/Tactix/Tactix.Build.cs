// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Tactix : ModuleRules
{
    public Tactix(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "AIModule",
            "StateTreeModule",
            "GameplayStateTreeModule",
            "UMG"
        });

        PublicIncludePaths.AddRange(
            new string[] {
                "Tactix"
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
            }
        );
    }
}
