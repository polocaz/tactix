#pragma once

#include "Enums/EDamageType.h"
#include "Enums/EDamageResponse.h"
#include "DamageInfo.generated.h"

USTRUCT(BlueprintType)
struct FDamageInfo
{
    GENERATED_BODY()

    /** How much damage to deal */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Damage")
    float DamageAmount = 0.f;

    /** What kind of damage this is */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Damage")
    EDamageType DamageType = EDamageType::None;

    /** How the hit should be responded to */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Damage")
    EDamageResponse DamageResponse = EDamageResponse::None;

    /** Should bypass invincibility frames? */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Damage")
    bool bShouldDamageInvincible = false;

    /** Can this hit be blocked? */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Damage")
    bool bCanBeBlocked = false;

    /** Can this hit be parried? */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Damage")
    bool bCanBeParried = false;

    /** Should force‐interrupt current action? */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Damage")
    bool bShouldForceInterrupt = false;
};