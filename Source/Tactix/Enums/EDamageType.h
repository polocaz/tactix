#pragma once

#include "EDamageType.generated.h"

UENUM(BlueprintType)
enum class EDamageType : uint8
{
    None        UMETA(DisplayName = "None"),
    Slash       UMETA(DisplayName = "Slash"),
    Stab        UMETA(DisplayName = "Stab"),
    Blunt       UMETA(DisplayName = "Blunt"),
    Fire        UMETA(DisplayName = "Fire"),
    Explosion   UMETA(DisplayName = "Explosion"),
    Bullet      UMETA(DisplayName = "Bullet"),
    Arrow       UMETA(DisplayName = "Arrow")
};