// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/WeaponComponent.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/WeaponBase.h"
#include "Weapons/HitscanWeapon.h"
#include "GameFramework/Character.h"

UWeaponComponent::UWeaponComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    SetIsReplicated(true);  // Enable component replication :contentReference[oaicite:3]{index=3}
}

UWeaponComponent::~UWeaponComponent()
{
    // Destroy inventory?
    // TODO: integrate inventory system

    for (AWeaponBase* W : Inventory)
    {
        W->Destroy();
    }
}



void UWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    /// Print socket location for Debug
    //if (CurrentWeapon && CurrentWeapon->WeaponMesh)
    //{
    //    // Try casting CurrentWeapon to AHitScanWeapon
    //    AHitscanWeapon* HitScanWeapon = Cast<AHitscanWeapon>(CurrentWeapon);
    //    
    //    // If we are hitscan weapon we have access to MuzzleSocketName
    //    if (HitScanWeapon)
    //    {
    //        // Make sure the socket exists
    //        if (CurrentWeapon->WeaponMesh->DoesSocketExist(HitScanWeapon->MuzzleSocketName))
    //        {
    //            // Get the world position of the muzzle socket
    //            FVector MuzzleLoc =
    //                CurrentWeapon->WeaponMesh->GetSocketLocation(HitScanWeapon->MuzzleSocketName);

    //            // Draw a sphere at that location. 
    //            // Duration = DeltaTime so it only lives until the next frame
    //            const float Radius = 4.0f;
    //            const int32 Segments = 8;
    //            const FColor DebugCol = FColor::Red;
    //            DrawDebugSphere(
    //                GetWorld(),
    //                MuzzleLoc,
    //                Radius,
    //                Segments,
    //                DebugCol,
    //                /* bPersistentLines = */ false,
    //                /* LifeTime = */ DeltaTime
    //            );
    //        }
    //    }
    //}
}


void UWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UWeaponComponent, CurrentWeapon);
    DOREPLIFETIME(UWeaponComponent, Inventory);
}

void UWeaponComponent::EquipWeaponByClass(TSubclassOf<AWeaponBase> WeaponClass)
{
    if (GetOwnerRole() < ROLE_Authority)
    {
        Server_EquipWeapon(WeaponClass);  // Client → Server :contentReference[oaicite:4]{index=4}
        return;
    }
    Server_EquipWeapon_Implementation(WeaponClass);
}

void UWeaponComponent::EquipWeaponBySlot(int32 SlotIndex)
{
    if (GetOwnerRole() < ROLE_Authority)
    {
        Server_EquipSlot(SlotIndex);
        return;
    }
    Server_EquipSlot_Implementation(SlotIndex);
}

bool UWeaponComponent::Server_EquipSlot_Validate(int32 SlotIndex)
{
    return Inventory.IsValidIndex(SlotIndex);
}

void UWeaponComponent::Server_EquipSlot_Implementation(int32 SlotIndex)
{
    if (!Inventory.IsValidIndex(SlotIndex))
    {
        return;
    }
    AWeaponBase* NewWeapon = Inventory[SlotIndex];
    if (NewWeapon && NewWeapon != CurrentWeapon)
    {
        DetachCurrentWeapon();
        CurrentWeapon = NewWeapon;
        AttachWeapon(CurrentWeapon);
    }
}

void UWeaponComponent::UnEquipWeapon()
{
    if (GetOwnerRole() < ROLE_Authority)
    {
        Server_UnEquipWeapon();
    }
    Server_UnEquipWeapon_Implementation();
}

bool UWeaponComponent::Server_EquipWeapon_Validate(TSubclassOf<AWeaponBase> WeaponClass) { return WeaponClass != nullptr; }
void UWeaponComponent::Server_EquipWeapon_Implementation(TSubclassOf<AWeaponBase> WeaponClass)
{
    // Find existing in inventory
    for (AWeaponBase* W : Inventory)
    {
        if (W->GetClass() == WeaponClass)
        {
            DetachCurrentWeapon();
            CurrentWeapon = W;
            AttachWeapon(CurrentWeapon);
            return;
        }
    }

    // Not found: spawn & add
    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    FActorSpawnParameters Params;
    Params.Owner = OwnerChar;
    Params.Instigator = OwnerChar->GetInstigator();
    AWeaponBase* NewWeap = GetWorld()->SpawnActor<AWeaponBase>(WeaponClass, Params);  // Server‐side spawn :contentReference[oaicite:5]{index=5}
    if (NewWeap)
    {
        Inventory.Add(NewWeap);
        DetachCurrentWeapon();
        CurrentWeapon = NewWeap;
        AttachWeapon(CurrentWeapon);
    }
}

bool UWeaponComponent::Server_UnEquipWeapon_Validate() { return true; }
void UWeaponComponent::Server_UnEquipWeapon_Implementation()
{
    DetachCurrentWeapon();
}

void UWeaponComponent::OnRep_CurrentWeapon()
{
    // Client: detach old visuals, attach new weapon mesh
    DetachCurrentWeapon();
    if (CurrentWeapon)
    {
        AttachWeapon(CurrentWeapon);  // Update socket attachments :contentReference[oaicite:6]{index=6}
    }
}

void UWeaponComponent::AttachWeapon(AWeaponBase* Weapon)
{
    if (!Weapon) return;
    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    FName SocketName = Weapon->AttachSocketName;
    Weapon->AttachToComponent(OwnerChar->GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, SocketName);

    CurrentWeapon->InitializeWeaponBase();
}

void UWeaponComponent::DetachCurrentWeapon()
{
    if (!CurrentWeapon) return;


    CurrentWeapon->ExitWeaponBase();
    
    CurrentWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    CurrentWeapon = nullptr;

}

// StartFire, StopFire, Reload forward to weapon
void UWeaponComponent::StartAttack()
{
    if (CurrentWeapon)
        CurrentWeapon->StartAttack();
}
void UWeaponComponent::StopAttack()
{
    if (CurrentWeapon)
        CurrentWeapon->StopAttack();
}
void UWeaponComponent::Reload()
{
    if (CurrentWeapon)
        CurrentWeapon->Reload();
}

