// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IWeapon.h"
#include "Enums/EWeaponState.h"
#include "WeaponBase.generated.h"

struct FHitTraceRequest;

UCLASS()
class TACTIX_API AWeaponBase : public AActor, public IWeapon
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWeaponBase();

    // IWeapon interface
    virtual void StartAttack() override;
    virtual void StopAttack() override;
    virtual bool CanAttack() const override;
    virtual void Reload() override;
    virtual bool CanReload() const override;
    virtual FOnAmmoChanged& OnAmmoChanged() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visual")
    FName AttachSocketName = TEXT("WeaponAttach");

    /** The visible weapon mesh (set per-weapon in Blueprint children) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Visual")
    USkeletalMeshComponent* WeaponMesh;  // ← our mesh component

    // Handler for trace results
    // UFUNCTION()
    // void OnServerHit(const FHitResult& Hit);

    // Used primarily to setup trace hit delegates
    void InitializeWeaponBase();
    void ExitWeaponBase();


protected:
    //--- Replicated State & Ammo --------------------------------------------
    /** Idle, Firing, Reloading, etc. Triggers OnRep_WeaponState() on clients. */
    UPROPERTY(ReplicatedUsing = OnRep_WeaponState)
    EWeaponState WeaponState;

    /** Current rounds in magazine; server-authoritative. */
    UPROPERTY(Replicated)
    int32 CurrentAmmo;

    // RepNotify for state changes (e.g. play or stop firing animation)
    UFUNCTION()
    void OnRep_WeaponState();


    //--- Core Attack Logic --------------------------------------------------
    /**  Called only on Authority(server or listen - server host) */
    void ExecuteAttack();                                    // ← shared flow: state, ammo, effects

    /** Subclass hook : child provides the actual firing logic */
    virtual void PerformAttackAction() PURE_VIRTUAL(AWeaponBase::PerformAttackAction, );

    /** For scheduling repeated shots */
    FTimerHandle    AutoFireTimerHandle;

    UFUNCTION()
    void OnTraceHit(const FHitResult& Hit);

    //--- Core Reload Logic --------------------------------------------------
    /** Timer handle for server‐side reload */
    FTimerHandle ReloadTimerHandle;

    /** Called on server when reload timer elapses */
    void FinishReload();

    /** Clean up montage end or timer, refill ammo and back to Idle */
    //UFUNCTION()
    //void OnReloadComplete();


    //--- Server RPCs --------------------------------------------------------
    /** Client → Server: request start firing (validation optional). */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_StartAttack();
    bool Server_StartAttack_Validate();
    void Server_StartAttack_Implementation();

    /** Client → Server: request stop firing. */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_StopAttack();
    bool Server_StopAttack_Validate();
    void Server_StopAttack_Implementation();

    /** Client → Server: request reload. */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_Reload();
    bool Server_Reload_Validate();
    void Server_Reload_Implementation();

    // Client → Server to perform an authoritative trace
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_PerformTrace(const FHitTraceRequest& Request);
    bool Server_PerformTrace_Validate(const FHitTraceRequest& Request);
    void Server_PerformTrace_Implementation(const FHitTraceRequest& Request);


    //--- Multicast RPCs ----------------------------------------------------
    /** Server → All: play VFX/SFX for firing. Unreliable OK for cosmetics. */
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayAttackEffects(const FVector& ImpactPoint);

    /** Server → All: play VFX/SFX for reloading. Unreliable OK for cosmetics. Most likely useless for melee, but we'll keep here for now */
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayReloadEffects();


    //--- Data & Components -------------------------------------------------
    /** DataAsset holding damage, fire rate, mag size, spread, etc. */
    UPROPERTY(EditDefaultsOnly, Category = "Weapon")
    UWeaponData* WeaponData;

    /** Shared line/sphere trace logic for hitscan & melee. */
    UPROPERTY(VisibleAnywhere, Category = "Weapon")
    UHitTraceComponent* HitTraceComponent;


    //--- In-Code Delegate -----------------------------------------------
    /** Notify UI or other systems when ammo count changes. */
    FOnAmmoChanged AmmoChangedDelegate;

    //--- Internal helpers --------------------------------------------------
    void SetWeaponState(EWeaponState NewState);
    void ConsumeAmmo();
};
