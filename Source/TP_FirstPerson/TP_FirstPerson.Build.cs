// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TP_FirstPerson : ModuleRules
{
    public TP_FirstPerson(ReadOnlyTargetRules Target) : base(Target)
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
                "TP_FirstPerson",
                "TP_FirstPerson/Variant_Horror",
                "TP_FirstPerson/Variant_Shooter",
                "TP_FirstPerson/Variant_Shooter/AI"
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
            }
        );
    }
}
