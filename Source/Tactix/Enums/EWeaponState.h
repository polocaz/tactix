#pragma once

#include "EWeaponState.generated.h"

UENUM(BlueprintType)
enum class EWeaponState : uint8
{
    Idle       UMETA(DisplayName = "Idle"),
    Attacking  UMETA(DisplayName = "Attacking"),
    Reloading  UMETA(DisplayName = "Reloading"),
    Equipping  UMETA(DisplayName = "Equipping")
};