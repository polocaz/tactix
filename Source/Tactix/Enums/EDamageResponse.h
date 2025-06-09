#pragma once

#include "EDamageResponse.generated.h"

UENUM(BlueprintType)
enum class EDamageResponse : uint8
{
    None    UMETA(DisplayName = "None"),
    HitReaction     UMETA(DisplayName = "HitReaction"),
    DeathReaction   UMETA(DisplayName = "DeathReaction"),
    Knockback       UMETA(DisplayName = "Knockback"),
    Stun            UMETA(DisplayName = "Stun")
};