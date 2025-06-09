// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponComponent.generated.h"

class AWeaponBase;

UCLASS(Blueprintable, BlueprintType, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TACTIX_API UWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
    UWeaponComponent();
    ~UWeaponComponent();

    // Called by character input
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void StartAttack();
    
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void StopAttack();

    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void Reload();

    // Equip by class or slot
    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void EquipWeaponByClass(TSubclassOf<AWeaponBase> WeaponClass);

    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void EquipWeaponBySlot(int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category = "Weapon")
    void UnEquipWeapon();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // Current equipped weapon (replicated)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeapon)
    AWeaponBase* CurrentWeapon;

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // Inventory of all spawned weapons (replicated)
    UPROPERTY(Replicated)
    TArray<AWeaponBase*> Inventory;

    // Called on clients when CurrentWeapon changes
    UFUNCTION()
    void OnRep_CurrentWeapon();

    // Server RPCs for equip/swap
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_EquipWeapon(TSubclassOf<AWeaponBase> WeaponClass);
    bool Server_EquipWeapon_Validate(TSubclassOf<AWeaponBase> WeaponClass);
    void Server_EquipWeapon_Implementation(TSubclassOf<AWeaponBase> WeaponClass);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_UnEquipWeapon();
    bool Server_UnEquipWeapon_Validate();
    void Server_UnEquipWeapon_Implementation();

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_EquipSlot(int32 SlotIndex);
    bool Server_EquipSlot_Validate(int32 SlotIndex);
    void Server_EquipSlot_Implementation(int32 SlotIndex);

    // Helper to attach weapon to owner mesh
    void AttachWeapon(AWeaponBase* Weapon);

    // Helper to detach current weapon
    void DetachCurrentWeapon();
};
