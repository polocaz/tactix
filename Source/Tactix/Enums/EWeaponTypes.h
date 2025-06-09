#pragma once

#include "CoreMinimal.h"
#include "EWeaponTypes.generated.h"

UENUM(BlueprintType)
enum class EFireMode : uint8
{
    SemiAuto     UMETA(DisplayName = "Semi-Auto"),
    FullAuto     UMETA(DisplayName = "Full-Auto"),
    ThreeRound   UMETA(DisplayName = "3-Round Burst"),
    BoltAction   UMETA(DisplayName = "Bolt-Action"),
    PumpAction   UMETA(DisplayName = "Pump-Action"),
    Revolver     UMETA(DisplayName = "Revolver"),
    Bow          UMETA(DisplayName = "Bow")
};