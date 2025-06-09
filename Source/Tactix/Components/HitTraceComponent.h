// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/ITraceProvider.h"
#include "HitTraceComponent.generated.h"

class AActor;

/**
 * Describes a single trace request: start location, direction, max distance, sphere radius (0=line), and instigating actor.
 */
USTRUCT(BlueprintType)
struct FHitTraceRequest
{
    GENERATED_BODY()

    /** Where the trace starts */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
    FVector Start = FVector::ZeroVector;

    /** Normalized direction or (End - Start). Unit vector */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
    FVector Direction = FVector::ZeroVector;

    /** How far to trace */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
    float MaxDistance = 10000.f;

    /** If >0, does a sphere trace of this radius; otherwise a line trace */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
    float Radius = 0.f;

    /** Who fired—used to ignore self or teammates */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
    TWeakObjectPtr<AActor> InstigatorActor;
};

/**
 * Shared component for weapons to perform hit‐scan or melee traces.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TACTIX_API UHitTraceComponent : public UActorComponent, public ITraceProvider
{
	GENERATED_BODY()

public:
    UHitTraceComponent();

    // ITraceProvider overrides
    virtual FVector GetTraceOrigin()   const override;
    virtual FVector GetTraceDirection() const override;


    /** Uses the basic */
    UFUNCTION(BlueprintCallable, Category="Trace")
    void StartTrace();

    /**
      * Request a trace. On server, performs authoritative trace and broadcasts OnHit.
      * On client, optionally can perform cosmetic trace via PerformCosmeticTrace().
      */
    UFUNCTION(BlueprintCallable, Category = "Trace")
    void StartTraceCustom(const FHitTraceRequest& Request);

    /**
     * Perform a client-only cosmetic trace for VFX feedback.
     */
    UFUNCTION(BlueprintCallable, Category = "Trace")
    void PerformCosmeticTrace(const FHitTraceRequest& Request);

    /** Fired once the server trace is done, even if it hit nothing */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHitResult, const FHitResult&, Hit);
    UPROPERTY(BlueprintAssignable, Category = "Trace")
    FOnHitResult OnHit;

protected:
    /** Client → Server: request start bullet trace (validation optional). */
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_PerformTrace(const FHitTraceRequest& Request);
    bool Server_PerformTrace_Validate(const FHitTraceRequest& Request);
    void Server_PerformTrace_Implementation(const FHitTraceRequest& Request);

    /** Internal helper for client cosmetic traces */
    void ExecuteCosmeticTrace(const FHitTraceRequest& Req);

    /** Internal helper to get player controllers viewpoint*/
    bool GetViewPoint(FVector& OutLoc, FVector& OutDir) const;

    // expose any offsets you need
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace|Settings")
    FVector OriginOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace|Settings")
    FVector DirectionOffset = FVector::ZeroVector;

    /** defaults based on weapon data from owner */
    float DefaultMaxDistance = 10000.f;
    float DefaultRadius = 0.f;

    /** Internal Helpers */
    void PerformTrace(const FHitTraceRequest& Request);
};
