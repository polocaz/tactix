// Fill out your copyright notice in the Description page of Project Settings.


#include "WeaponBase.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Interfaces/IWeapon.h"
#include "Interfaces/IDamageable.h"
#include "DataAssets/WeaponData.h"
#include "Components/HitTraceComponent.h" 

// Sets default values
AWeaponBase::AWeaponBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// Enable replication
	bReplicates = true;
    AActor::SetReplicateMovement(true);

    // Create and attach the mesh component
    WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    WeaponMesh->SetupAttachment(RootComponent);
    WeaponMesh->SetIsReplicated(true);  // replicate mesh transforms automatically if needed

	// Create a shared trace component
	HitTraceComponent = CreateDefaultSubobject<UHitTraceComponent>(TEXT("HitTrace"));

    // Init ammo for debug
    if (WeaponData)
        CurrentAmmo = WeaponData->MagazineSize;
    else
        CurrentAmmo = 30;
}

void AWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeaponBase, WeaponState);
	DOREPLIFETIME(AWeaponBase, CurrentAmmo);
}

void AWeaponBase::InitializeWeaponBase()
{
    if (HitTraceComponent)
    {
        // bind only once
        HitTraceComponent->OnHit.AddUniqueDynamic(this, &AWeaponBase::OnTraceHit);
    }
}

void AWeaponBase::ExitWeaponBase()
{
    if (HitTraceComponent)
    {
        HitTraceComponent->OnHit.RemoveDynamic(this, &AWeaponBase::OnTraceHit);
    }
}

//------------------------------------------------------------------------------
// IWeapon API
//------------------------------------------------------------------------------
void AWeaponBase::StartAttack()
{
    if (GetOwner())
    {
		FString myName = GetOwner()->GetName();
		FString message = FString::Printf(TEXT("%s: wants to atack"), *myName);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, message);
		}
    }

    if (!HasAuthority())
    {
        Server_StartAttack();                               // client asks server
        //PlayClientVFX();                                  // optional cosmetic feedback
        return;
    }

    // Authority (dedicated or listen-server’s local player)
    ExecuteAttack();

    // Then schedule subsequent shots at RateOfFire interval
    if (WeaponData && WeaponData->FireMode == EFireMode::FullAuto)
    {
		const float SecondsPerShot = 60.0f / WeaponData->RateOfFire;
		GetWorldTimerManager().SetTimer(
			AutoFireTimerHandle,
			this,
			&AWeaponBase::ExecuteAttack,
			SecondsPerShot,
			true  // loop
		);
    }
}

void AWeaponBase::StopAttack()
{
    if (!HasAuthority())
    {
        Server_StopAttack();
        return;
    }

    // Clear the timer so we stop firing
    GetWorldTimerManager().ClearTimer(AutoFireTimerHandle);
}


bool AWeaponBase::CanAttack() const
{
    return CurrentAmmo > 0 && WeaponState == EWeaponState::Idle;
}

bool AWeaponBase::CanReload() const
{
    return CurrentAmmo >= 0 && WeaponState == EWeaponState::Idle;
}

IWeapon::FOnAmmoChanged& AWeaponBase::OnAmmoChanged()
{
    return AmmoChangedDelegate;
}

//------------------------------------------------------------------------------
// RepNotify & Multicast
//------------------------------------------------------------------------------
void AWeaponBase::OnRep_WeaponState()
{
    // Clients react to state change (e.g. start/stop firing anim)
}

void AWeaponBase::Multicast_PlayAttackEffects_Implementation(const FVector& ImpactPoint)
{
    if (!WeaponData || !WeaponData->FireAnimMontage)
    {
        UE_LOG(LogTemp, Error, TEXT("No FireMontage assigned on %s"), *GetName());
        return;
    }

    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (!OwnerChar)
    {
        UE_LOG(LogTemp, Error, TEXT("No owning character on %s"), *GetName());
        return;
    }

    UAnimInstance* AnimInst = OwnerChar->GetMesh()->GetAnimInstance();
    if (!AnimInst)
    {
        UE_LOG(LogTemp, Error, TEXT("No AnimInstance on mesh of %s"), *OwnerChar->GetName());
        return;
    }

    float PlayedLength = AnimInst->Montage_Play(WeaponData->FireAnimMontage);
    UE_LOG(LogTemp, Warning, TEXT("Montage_Play returned %f"), PlayedLength);
}

void AWeaponBase::Multicast_PlayReloadEffects_Implementation()
{
    // Reload anims? Spawn empty mag, etc.
}

//------------------------------------------------------------------------------
// Core attack logic
//------------------------------------------------------------------------------
void AWeaponBase::ExecuteAttack()
{
    if (!CanAttack()) return;
    SetWeaponState(EWeaponState::Attacking);
    ConsumeAmmo();
    PerformAttackAction();                                              // virtual call into child
    Multicast_PlayAttackEffects(/*ImpactPoint=*/FVector::ZeroVector);   // fire VFX for all clients
    SetWeaponState(EWeaponState::Idle);
}

void AWeaponBase::OnTraceHit(const FHitResult& Hit)
{
    AActor* HitActor = Hit.GetActor();
    if (HitActor && HitActor->GetClass()->ImplementsInterface(UDamageable::StaticClass()))
    {
        // pull your damage info out of your DataAsset
        float DamageAmount = WeaponData->Damage;
        TSubclassOf<UDamageType> DamageType = WeaponData->DamageTypeClass;

        FDamageInfo Info;
        Info.DamageAmount = WeaponData->Damage;
        Info.DamageType = EDamageType::Bullet;       // EDamageType
        Info.DamageResponse = EDamageResponse::None;   // EDamageResponse
        Info.bShouldDamageInvincible = true;

        // call the Interface function
        bool wasDamageTaken = false;
        IDamageable::Execute_ReceiveDamage(HitActor, Info, wasDamageTaken);
        
        // TODO: Incorporate unreal engine damage system
        UGameplayStatics::ApplyPointDamage(
            Hit.GetActor(),
            WeaponData->Damage,
            Hit.TraceStart,
            Hit,
            GetInstigatorController(),
            this,
            WeaponData->DamageTypeClass
        );
    }
}

//------------------------------------------------------------------------------
// Core reload logic
//------------------------------------------------------------------------------
void AWeaponBase::FinishReload()
{
    // refill
    CurrentAmmo = WeaponData ? WeaponData->MagazineSize : CurrentAmmo;
    AmmoChangedDelegate.Broadcast(CurrentAmmo);

    // play reload VFX/anim everywhere
    Multicast_PlayReloadEffects();

    // transition back
    SetWeaponState(EWeaponState::Idle);
}

void AWeaponBase::Reload()
{
    if (!CanReload())
        return;

    if (!HasAuthority())
    {
        Server_Reload();
        return;
    }

    SetWeaponState(EWeaponState::Reloading);

    const float ReloadTime = WeaponData ? WeaponData->ReloadTime : 2.0f;
    GetWorldTimerManager().SetTimer(
        ReloadTimerHandle,
        this, &AWeaponBase::FinishReload,
        ReloadTime,
        false
    );
}

//------------------------------------------------------------------------------
// Server RPC implementations
//------------------------------------------------------------------------------
bool AWeaponBase::Server_StartAttack_Validate() { return true; }
void AWeaponBase::Server_StartAttack_Implementation()
{
    StartAttack();
    //ExecuteAttack(); // server runs the same shared flow
}

bool AWeaponBase::Server_StopAttack_Validate() { return true; }
void AWeaponBase::Server_StopAttack_Implementation()
{
    SetWeaponState(EWeaponState::Idle);
    StopAttack();
}

bool AWeaponBase::Server_Reload_Validate() { return true; }
void AWeaponBase::Server_Reload_Implementation()
{
    // How to sync animation?
    Reload();
}

void AWeaponBase::Server_PerformTrace_Implementation(const FHitTraceRequest& Request)
{
    if (!CanAttack()) return;
    // Ensure server authority

    // Perform the trace
    HitTraceComponent->StartTrace();
}

bool AWeaponBase::Server_PerformTrace_Validate(const FHitTraceRequest& Request)
{
    // Example: limit max range
    //return Request.MaxDistance <= WeaponData->MaxRange;
    return true;
}

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------
void AWeaponBase::SetWeaponState(EWeaponState NewState)
{
    WeaponState = NewState;                                         // RepNotify triggers OnRep_WeaponState :contentReference[oaicite:11]{index=11}
}

void AWeaponBase::ConsumeAmmo()
{
    --CurrentAmmo;                                                  // Ammo replicated to clients :contentReference[oaicite:12]{index=12}
    AmmoChangedDelegate.Broadcast(CurrentAmmo);
}
