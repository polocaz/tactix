// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/HitTraceComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "Characters/CharacterBase.h"
#include "Components/WeaponComponent.h"
#include "Weapons/HitscanWeapon.h"
#include "GameFramework/Actor.h"

// Sets default values for this component's properties
UHitTraceComponent::UHitTraceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	//SetIsReplicated(true);
    
    // TODO: Get DefaultMaxDistance and DefaultRadius from weapon data from owner
}

void UHitTraceComponent::StartTrace()
{
    FHitTraceRequest Req;
    Req.Start = GetTraceOrigin();
    Req.Direction = GetTraceDirection();
    Req.MaxDistance = DefaultMaxDistance;
    Req.Radius = DefaultRadius;
    Req.InstigatorActor = GetOwner();

    // The GetTrace* functions will return zero if something fails
    if (Req.Start == FVector::ZeroVector || Req.Direction == FVector::ZeroVector)
    {
        FString mess = FString::Printf(TEXT("Failed to build trace!"));

		if (GEngine)
		{
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, mess);
        }
        return;
    }

    ExecuteCosmeticTrace(Req);
    StartTraceCustom(Req);
}

void UHitTraceComponent::StartTraceCustom(const FHitTraceRequest& Request)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        // Tell server to trace
        Server_PerformTrace(Request);
    }
    else
    {
        // We have authority, so trace now
        PerformTrace(Request);
    }
}

bool UHitTraceComponent::Server_PerformTrace_Validate(const FHitTraceRequest& Request) 
{
    // Example: limit max range
   //return Request.MaxDistance <= WeaponData->MaxRange;
   return true; 
}

void UHitTraceComponent::Server_PerformTrace_Implementation(const FHitTraceRequest& Request)
{
    PerformTrace(Request); // server runs the same shared flow
}

void UHitTraceComponent::PerformCosmeticTrace(const FHitTraceRequest& Request)
{
    // Clients can call this for immediate VFX feedback
    ExecuteCosmeticTrace(Request);
}

void UHitTraceComponent::ExecuteCosmeticTrace(const FHitTraceRequest& Req)
{
    if (!GetWorld())
    {
        FString message = FString::Printf(TEXT("trace failed because getworld failed"));

        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, message);
        }
        return;
    }

    FVector End = Req.Start + Req.Direction.GetSafeNormal() * Req.MaxDistance;
    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Req.InstigatorActor.Get());

    if (ACharacterBase* Ch = Cast<ACharacterBase>(GetOwner()->GetInstigator()))
    {
        Params.AddIgnoredComponent(Ch->GetMesh()); // if you want to ignore the entire skeletal mesh
    }

    if (Req.Radius > KINDA_SMALL_NUMBER)
    {
        GetWorld()->SweepSingleByChannel(Hit, Req.Start, End, FQuat::Identity, ECC_GameTraceChannel1,
            FCollisionShape::MakeSphere(Req.Radius), Params);
    }
    else
    {
        GetWorld()->LineTraceSingleByChannel(Hit, Req.Start, End, ECC_GameTraceChannel1, Params);
    }

    const FColor DrawColor = Hit.bBlockingHit ? FColor::Red : FColor::Green;
    DrawDebugLine(GetWorld(), Req.Start, End, DrawColor, false, 1.f, 0, 1.f);
    if (Hit.bBlockingHit)
    {
        DrawDebugSphere(GetWorld(), Hit.ImpactPoint, Req.Radius > 0 ? Req.Radius : 5.f,
            12, DrawColor, false, 1.f);
    }
}

void UHitTraceComponent::PerformTrace(const FHitTraceRequest& Req)
{
    ExecuteCosmeticTrace(Req);
    
    if (!GetWorld()) return;

    FCollisionQueryParams Params;
    Params.bReturnPhysicalMaterial = false;
    Params.AddIgnoredActor(Req.InstigatorActor.Get());

    if (ACharacterBase* Ch = Cast<ACharacterBase>(GetOwner()->GetInstigator()))
    {
        Params.AddIgnoredComponent(Ch->GetMesh()); // if you want to ignore the entire skeletal mesh
    }

    FVector End = Req.Start + Req.Direction.GetSafeNormal() * Req.MaxDistance;
    FHitResult Hit;

    if (Req.Radius > KINDA_SMALL_NUMBER)
    {
        GetWorld()->SweepSingleByChannel(
            Hit,
            Req.Start,
            End,
            FQuat::Identity,
            ECC_GameTraceChannel1,
            FCollisionShape::MakeSphere(Req.Radius),
            Params
        );
    }
    else
    {
        GetWorld()->LineTraceSingleByChannel(
            Hit,
            Req.Start,
            End,
            ECC_GameTraceChannel1,
            Params
        );
    }

    // Broadcast to listeners (weapons) on server
    OnHit.Broadcast(Hit);
}

bool UHitTraceComponent::GetViewPoint(FVector& OutLoc, FVector& OutDir) const
{
    // Find your character and its controller
    if (AHitscanWeapon* Weapon = Cast<AHitscanWeapon>(GetOwner()))
    {
        if (ACharacterBase* Ch = Cast<ACharacterBase>(Weapon->GetOwner()))
        {
            if (APlayerController* PC = Cast<APlayerController>(Ch->GetController()))
            {
                FRotator ViewRot;
                PC->GetPlayerViewPoint(OutLoc, ViewRot);
                OutDir = ViewRot.Vector();
                return true;
            }
        }
    }
    return false;
}

FVector UHitTraceComponent::GetTraceOrigin() const
{
    FVector ViewLoc, ViewDir;
    constexpr float TraceForwardOffset = 5.0f; 
    return GetViewPoint(ViewLoc, ViewDir) ? ViewLoc + ViewDir * TraceForwardOffset : FVector::ZeroVector;

    // HitScan logic from muzzle?
    //if (AHitscanWeapon* Weapon = Cast<AHitscanWeapon>(GetOwner()))
    //{
    //    if (ACharacterBase* Ch = Cast<ACharacterBase>(Weapon->GetOwner()))
    //    {
    //        if (Ch->FirstPersonCamera)
    //        {
    //            return Ch->FirstPersonCamera->GetComponentLocation();
    //        }
    //    }
    //}

    //if (AHitscanWeapon* Weapon = Cast<AHitscanWeapon>(GetOwner()))
    //{
    //    // Camera trace forward
    //    // 1) Get the owning weapon actor
    //    UWeaponComponent* WeaponComp = Cast<UWeaponComponent>(GetOwner());
    //    if (!WeaponComp) return FVector::ZeroVector;

    //    // 2) Its owner should be your Character
    //    ACharacter* OwnerChar = Cast<ACharacterBase>(WeaponComp->GetOwner());
    //    if (!OwnerChar) return FVector::ZeroVector;




        //if (Weapon->WeaponMesh && Weapon->WeaponMesh->DoesSocketExist(Weapon->MuzzleSocketName))
        //{
        //    return Weapon->WeaponMesh->GetSocketLocation(Weapon->MuzzleSocketName);
        //}
    //}


    // Fallback
    //return GetOwner() ? GetOwner()->GetActorLocation() + OriginOffset : FVector::ZeroVector;
}

FVector UHitTraceComponent::GetTraceDirection() const
{
    FVector ViewLoc, ViewDir;
    return GetViewPoint(ViewLoc, ViewDir) ? ViewDir : FVector::ZeroVector;

    //if (AHitscanWeapon* Weapon = Cast<AHitscanWeapon>(GetOwner()))
    //{
    //    if (ACharacterBase* Ch = Cast<ACharacterBase>(Weapon->GetOwner()))
    //    {


    //        if (Ch->FirstPersonCamera)
    //        {
    //            return Ch->FirstPersonCamera->GetComponentRotation().Vector();
    //        }
    //    }
    //}

    //if (AHitscanWeapon* Weapon = Cast<AHitscanWeapon>(GetOwner()))
    //{
    //    if (Weapon->WeaponMesh && Weapon->WeaponMesh->DoesSocketExist(Weapon->MuzzleSocketName))
    //    {
    //        const FRotator SocketRot = Weapon->WeaponMesh->GetSocketRotation(Weapon->MuzzleSocketName);
    //        return (SocketRot.Vector() + DirectionOffset).GetSafeNormal();
    //    }
    //}

    // Fallback: actor's forward
    //return (GetOwner()->GetActorForwardVector() + DirectionOffset).GetSafeNormal();
}
