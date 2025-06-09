// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "WeaponBase.h"
#include "Components/HitTraceComponent.h"
#include "HitscanWeapon.generated.h"

class UWeaponData;
class UHitTraceComponent;

/**
 * Instant-hit firearm using line traces.
 */
UCLASS()
class TACTIX_API AHitscanWeapon : public AWeaponBase
{
	GENERATED_BODY()

public:
    AHitscanWeapon();

    // IWeapon interface override
    //virtual void StartAttack() override;
    //virtual void StopAttack() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visual")
    FName MuzzleSocketName = TEXT("weap_muzzle_ak");

protected:
    virtual void PerformAttackAction() override;
    virtual void BeginPlay() override;

    ///** Time between shots, derived from RateOfFire */
    //float TimeBetweenShots;
    //FTimerHandle ShotTimerHandle;

    ///** Perform a single shot trace */
    //void HandleFiring();

    ///** Begin next shot timer */
    //void BeginFireTimer();

    ///** Stop repeating fire */
    //void EndFire();

    // Helper to get trace request from owner
    FHitTraceRequest CreateTraceRequest() const;

    // Components
    //UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
    //UHitTraceComponent* HitTraceComponent;

private:
    bool b_DelegateHooked = false;
};
