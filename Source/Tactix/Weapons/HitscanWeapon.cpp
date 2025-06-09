// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapons/HitscanWeapon.h"
#include "DataAssets/WeaponData.h"
#include "Components/HitTraceComponent.h"
#include "Interfaces/ITraceProvider.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

AHitscanWeapon::AHitscanWeapon()
{
    //// Calculate time between shots
    //if (WeaponData && WeaponData->RateOfFire > 0.f)
    //{
    //    TimeBetweenShots = 60.f / WeaponData->RateOfFire;
    //}
}

void AHitscanWeapon::BeginPlay()
{
}

//void AHitscanWeapon::StartAttack()
//{
//    Server_StartAttack();  // call base RPC
//
//    if (!HasAuthority())
//    {
//        // Perform client side vfx here
//        return;
//    }
//
//    if (!GetWorldTimerManager().IsTimerActive(ShotTimerHandle))
//    {
//        HandleFiring();
//        BeginFireTimer();
//    }
//}

//void AHitscanWeapon::StopAttack()
//{
//    Server_StopAttack();
//
//    if (!HasAuthority())
//    {
//        return;
//    }
//    EndFire();
//}

void AHitscanWeapon::PerformAttackAction()
{
    FHitTraceRequest Req = CreateTraceRequest();
    Req.Radius = 0.f;
    Req.MaxDistance = WeaponData->MaxRange;

    if (HitTraceComponent)
    {
        // Bind once per shot (or bind in BeginPlay and unbind later)
        // TODO: Figure out why is this getting hit twice?
        /*
        if (!b_DelegateHooked)
        {
            FString message = FString::Printf(TEXT("Binding delegate"));

            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, message);
            }
            HitTraceComponent->OnHit.AddDynamic(this, &AWeaponBase::OnServerHit);
            b_DelegateHooked = true;
        }
        */
        HitTraceComponent->StartTrace();
    }
}

//void AHitscanWeapon::HandleFiring()
//{
//    if (!CanAttack()) return;
//
//    // Update state & ammo
//    SetWeaponState(EWeaponState::Attacking);
//    ConsumeAmmo();
//
//    // Build and perform trace
//    FHitTraceRequest Req = CreateTraceRequest();
//    Req.Radius = 0.f;
//    Req.MaxDistance = WeaponData->MaxRange;
//    HitTraceComponent->PerformTrace(Req);
//
//    // Effects
//    Multicast_PlayAttackEffects(Req.Start + Req.Direction * Req.MaxDistance);
//}

//void AHitscanWeapon::BeginFireTimer()
//{
//    //GetWorldTimerManager().SetTimer(ShotTimerHandle, this, &AHitscanWeapon::HandleFiring, TimeBetweenShots, true);
//}
//
//void AHitscanWeapon::EndFire()
//{
//    GetWorldTimerManager().ClearTimer(ShotTimerHandle);
//    SetWeaponState(EWeaponState::Idle);
//}

FHitTraceRequest AHitscanWeapon::CreateTraceRequest() const
{
    // ZZZZZZZZZZZZZZ need to have a valid trace provider
    /*FHitTraceRequest Req;
    const ITraceProvider* TP = Cast<ITraceProvider>(GetOwner());
    Req.Start = TP->GetTraceOrigin();
    Req.Dirsection = TP->GetTraceDirection();
    Req.InstigatorActor = GetInstigator();
    return Req;*/
    FHitTraceRequest Req;
    Req.Start = WeaponMesh->GetSocketLocation(MuzzleSocketName);
    Req.Direction = WeaponMesh->GetSocketRotation(MuzzleSocketName).Vector();
    Req.InstigatorActor = GetInstigator();
    return Req;
}
