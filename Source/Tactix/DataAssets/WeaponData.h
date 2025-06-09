// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Enums/EWeaponTypes.h"
#include "WeaponData.generated.h"

class UCurveFloat;
class USoundBase;
class UParticleSystem;
class UAnimMontage;

/**
 * Data asset defining all tunable weapon parameters.
 * Supports both projectile and hitscan, as well as melee.
 */
UCLASS()
class TACTIX_API UWeaponData : public UDataAsset
{
	GENERATED_BODY()
	
public:
    /** Weapon display name */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
    FText DisplayName;

    /** Weapon icon for UI */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
    UTexture2D* Icon;

    /** Weapon class to spawn */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
    TSubclassOf<class AWeaponBase> WeaponClass;

    // --- Combat Stats -----------------------------------------------------
    /** Base damage per shot or hit */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    float Damage = 25.f;

    /** Damage type for handling custom types in the future */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    TSubclassOf<UDamageType> DamageTypeClass = UDamageType::StaticClass();

    /** Rate of fire in rounds per minute (for hitscan/projectile) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    float RateOfFire = 600.f;

    /** Firemode of ranged weapon */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    EFireMode FireMode = EFireMode::SemiAuto;

    /** Magazine size (for firearms) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    int32 MagazineSize = 30;

    /** Time (seconds) to fully reload */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    float ReloadTime = 2.f;

    /** Maximum effective range in units */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    float MaxRange = 10000.f;

    /** Cone half-angle in degrees for bullet spread */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = 0.f, ClampMax = 45.f))
    float SpreadConeAngle = 1.5f;

    /** Rate at which spread returns to zero when not firing */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
    float SpreadRecoveryRate = 5.f;

    // --- Melee Stats -----------------------------------------------------
    /** Swing range for melee weapons */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
    float SwingRange = 200.f;

    /** Radius for melee sweep */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
    float SwingRadius = 50.f;

    /** Delay between swing start and hit window (seconds) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee")
    float MeleeHitDelay = 0.2f;

    // --- Projectile Stats ------------------------------------------------
    /** Projectile class spawned when firing */
    //UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
    //TSubclassOf<class AProjectile> ProjectileClass;

    /** Initial speed of spawned projectile */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
    float ProjectileSpeed = 3000.f;

    /** Gravity scale applied to projectile */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
    float ProjectileGravityScale = 1.f;

    // --- Effects ---------------------------------------------------------
    /** Muzzle flash particle system */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
    UParticleSystem* MuzzleFX;

    /** Impact particle system */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
    UParticleSystem* ImpactFX;

    /** Fire sound */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
    USoundBase* FireSound;

    /** Reload sound */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
    USoundBase* ReloadSound;

    // --- Recoil & Animation ----------------------------------------------
    /** Recoil curve applied per fire */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
    UCurveFloat* RecoilPitchCurve;

    /** Anim montage for firing */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
    UAnimMontage* FireAnimMontage;

    /** Anim montage for reloading */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
    UAnimMontage* ReloadAnimMontage;
};
